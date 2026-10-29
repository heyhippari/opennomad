#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Core/Omikron/IamZone.hpp"
#include "Core/Scenario/RuntimeAreaSlot.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/TriggerProxyService.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"

namespace App {

/// Origin of one transient, residency-derived active spatial-zone record.
enum class ActiveZoneSource : std::uint8_t {
  k_area,
  k_scene,
};

/// Identity of one authored IAM table-2 record within the two-resident namespace. Two records
/// with the same ZONE ID but different event entries are different zones.
struct ZoneIdentity {
  std::size_t resident_slot{0};
  ActiveZoneSource source{ActiveZoneSource::k_area};
  std::int32_t area_id{-1};
  std::int32_t scene_id{-1};
  std::int16_t zone_id{0};
  std::array<std::uint32_t, 3> event_offsets{};

  [[nodiscard]] bool operator==(const ZoneIdentity&) const = default;
};

/// One enabled IAM table-2 record materialized from a resident AREA or SCENE.
/// This deliberately retains the raw authored record and never claims
/// collision or event-trigger semantics for its three event offsets.
struct ActiveZoneRef {
  std::size_t resident_slot{0};
  ActiveZoneSource source{ActiveZoneSource::k_area};
  std::int32_t area_id{-1};
  std::int32_t scene_id{-1};
  Omikron::IamZoneRecord zone;

  [[nodiscard]] ZoneIdentity identity() const {
    return {.resident_slot = resident_slot,
        .source = source,
        .area_id = area_id,
        .scene_id = scene_id,
        .zone_id = zone.zone_id,
        .event_offsets = zone.event_offsets};
  }
};

/// Semantic ordinary heartbeat states corresponding to native states 1/2/3.
/// Departure pending is OpenNomad context retention after the spatial slot is free;
/// it is unrelated to the deferred native interaction states 4/5.
enum class ZoneContactHeartbeatState : std::uint8_t {
  k_entry_pending,
  k_refreshed,
  k_awaiting_refresh,
  k_departure_pending,
};

/// One transient zone compact context. Spatial registration may retire while
/// this context remains alive to drain queued departure work.
struct ZoneContactContext {
  std::size_t resident_slot{0};
  ActiveZoneSource source{ActiveZoneSource::k_area};
  std::int32_t area_id{-1};
  std::int32_t scene_id{-1};
  Omikron::IamZoneRecord zone;
  std::size_t program_record_origin{0};
  std::unique_ptr<Script::AreaScriptRuntime> script;
  ZoneContactHeartbeatState heartbeat_state{ZoneContactHeartbeatState::k_entry_pending};
  std::uint64_t last_heartbeat_generation{0};

  [[nodiscard]] bool spatially_registered() const {
    return heartbeat_state != ZoneContactHeartbeatState::k_departure_pending;
  }
  [[nodiscard]] ZoneIdentity identity() const {
    return {.resident_slot = resident_slot,
        .source = source,
        .area_id = area_id,
        .scene_id = scene_id,
        .zone_id = zone.zone_id,
        .event_offsets = zone.event_offsets};
  }
};

struct ZoneQualificationDiagnostic {
  ZoneIdentity identity;
  bool qualifies{false};
};

struct OrdinarySpatialSampleResult {
  std::size_t qualifying_zone_count{0};
};

/// The three spatial tests a zone applies to the current-character trigger proxy.
struct ZoneSpatialQualification {
  bool broadphase{false};
  bool polygon{false};
  bool heading{false};

  [[nodiscard]] bool qualifies() const {
    return broadphase && polygon && heading;
  }
};

/// Evaluates a zone against a registered, contact-ready proxy; all tests fail otherwise.
[[nodiscard]] ZoneSpatialQualification evaluate_zone_spatial_qualification(
    const CurrentCharacterTriggerProxy* proxy, const Omikron::IamZoneRecord& zone);

/// Services the controller must provide to the zone-contact service.
class ZoneContactHost {
 public:
  ZoneContactHost() = default;
  ZoneContactHost(const ZoneContactHost&) = delete;
  ZoneContactHost(ZoneContactHost&&) = delete;
  ZoneContactHost& operator=(const ZoneContactHost&) = delete;
  ZoneContactHost& operator=(ZoneContactHost&&) = delete;

  /// Null before a session is initialized.
  [[nodiscard]] virtual ScenarioManager* zone_contact_manager() const = 0;
  [[nodiscard]] virtual std::span<const RuntimeAreaSlot> zone_contact_slots() const = 0;
  /// Wires a freshly created zone-owned compact context to the session services.
  virtual void bind_zone_contact_script(
      Script::AreaScriptRuntime& script, std::size_t resident_slot, bool scene_source) = 0;
  /// Polls tracked SCX/character-script waits of a zone-owned context.
  virtual std::expected<void, std::string> service_zone_contact_waits(
      Script::AreaScriptRuntime& script, std::size_t resident_slot) = 0;

 protected:
  ~ZoneContactHost() = default;
};

/// Owns the transient active-zone set and the zone-owned compact contexts: residency-derived zone
/// refresh, spatial qualification against the trigger proxy, the 16-slot registration capacity,
/// the heartbeat state machine, and retirement of drained contexts.
class ZoneContactService {
 public:
  /// Native spatial-contact capacity (the recovered registration table has 16 slots).
  static constexpr std::size_t k_max_registrations{16};

  ZoneContactService(ZoneContactHost& host, const TriggerProxyService& trigger_proxy);

  void reset();

  /// Rebuilds the enabled-zone set from the resident AREA/SCENE records and persistent ZONE bits.
  [[nodiscard]] std::expected<void, std::string> refresh_active_zones();

  [[nodiscard]] std::span<const ActiveZoneRef> active_zones() const {
    return m_active_zones;
  }
  [[nodiscard]] std::size_t contact_count() const {
    return m_contacts.size();
  }
  [[nodiscard]] const ZoneContactContext* contact(const std::size_t index) const {
    return index < m_contacts.size() ? m_contacts.at(index).get() : nullptr;
  }
  /// All live contexts, for compact-context lookup and interface/camera completion delivery.
  [[nodiscard]] std::span<const std::unique_ptr<ZoneContactContext>> contacts() const {
    return m_contacts;
  }
  [[nodiscard]] std::size_t registration_count() const;
  [[nodiscard]] bool lifecycle_pass_pending() const {
    return m_lifecycle_pass_pending;
  }
  void request_lifecycle_pass() {
    m_lifecycle_pass_pending = true;
  }

  /// True when the zone's record is resident and the current character qualifies for it.
  [[nodiscard]] bool reporting_enabled(const ActiveZoneRef& active_zone) const;

  /// Discards every contact owned by one resident slot (optionally only SCENE-sourced ones).
  /// Contacts borrow bytes from their slot's record and must not outlive it.
  void erase_contacts(std::size_t resident_slot, bool scene_source_only);

  /// Runs the queued heartbeat lifecycle pass, if one is pending.
  void run_pending_lifecycle_pass();

  /// Executes every live contact compact context (waits, VM).
  [[nodiscard]] std::expected<void, std::string> service_scripts(float delta_seconds);

  /// Qualifies every active zone against a fresh spatial sample and registers/refreshes contacts.
  [[nodiscard]] std::expected<OrdinarySpatialSampleResult, std::string>
  reconcile_from_fresh_spatial_sample();

  /// Non-spatial cleanup for residency, activation, and completed contexts.
  void housekeeping();

 private:
  [[nodiscard]] bool backing_resident(const ZoneContactContext& contact) const;
  [[nodiscard]] bool spatially_qualifies(
      std::size_t resident_slot, const Omikron::IamZoneRecord& zone) const;
  [[nodiscard]] ZoneContactContext* find_registered(const ActiveZoneRef& active_zone);
  [[nodiscard]] std::expected<ZoneContactContext*, std::string> register_or_refresh(
      const ActiveZoneRef& active_zone);
  void advance_heartbeat_lifecycle();
  [[nodiscard]] const CurrentCharacterTriggerProxy* proxy() const;
  void log_qualification_change(const ActiveZoneRef& active_zone, bool qualifies);

  ZoneContactHost& m_host;
  const TriggerProxyService& m_trigger_proxy;
  std::vector<ActiveZoneRef> m_active_zones;
  std::vector<std::unique_ptr<ZoneContactContext>> m_contacts;
  std::vector<ZoneQualificationDiagnostic> m_qualification_diagnostics;
  bool m_lifecycle_pass_pending{false};
};

}  // namespace App

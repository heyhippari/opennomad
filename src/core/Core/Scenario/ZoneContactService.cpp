#include "Core/Scenario/ZoneContactService.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/GameState.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Omikron/IamZone.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"

namespace App {

namespace {

/// Tests an authored IAM zone polygon against a live Runtime-space actor
/// position. IAM table-2 vertices remain serialized AREA integers, whereas
/// RuntimeCharacter::transform.translation is continuous world-space inches.
///
/// Convert the immutable polygon into Runtime space instead of quantizing the
/// moving actor back into AREA integers; root motion can be fractional.
[[nodiscard]] bool zone_contains_runtime_xz(
    const Omikron::IamZoneRecord& zone, const Runtime::Vec3& position) {
  bool inside{false};
  for (std::size_t current{0}, previous{zone.serialized_vertices.size() - 1U};
      current < zone.serialized_vertices.size();
      previous = current++) {
    const auto& vertex{zone.serialized_vertices.at(current)};
    const auto& prior{zone.serialized_vertices.at(previous)};
    const double vertex_x{static_cast<double>(Runtime::area_position_to_inches(vertex.at(0)))};
    const double vertex_z{static_cast<double>(Runtime::area_position_to_inches(vertex.at(2)))};
    const double prior_x{static_cast<double>(Runtime::area_position_to_inches(prior.at(0)))};
    const double prior_z{static_cast<double>(Runtime::area_position_to_inches(prior.at(2)))};
    const double actor_x{static_cast<double>(position.x)};
    const double actor_z{static_cast<double>(position.z)};
    if ((vertex_z > actor_z) == (prior_z > actor_z)) {
      continue;
    }
    const double left{(actor_x - vertex_x) * (prior_z - vertex_z)};
    const double right{(prior_x - vertex_x) * (actor_z - vertex_z)};
    if ((prior_z > vertex_z) ? (left < right) : (left > right)) {
      inside = !inside;
    }
  }
  return inside;
}

[[nodiscard]] bool zone_intersects_proxy_bounds(
    const Omikron::IamZoneRecord& zone, const Runtime::Vec3& position, const float radius) {
  return Runtime::area_zone_bounds_intersect_actor(
      Runtime::area_zone_spatial_bounds(zone.serialized_vertices), position, radius);
}

[[nodiscard]] std::optional<std::size_t> entry_or_missing(const std::uint32_t offset) {
  return offset == 0U ? std::nullopt : std::optional<std::size_t>{offset};
}

}  // namespace

ZoneSpatialQualification evaluate_zone_spatial_qualification(
    const CurrentCharacterTriggerProxy* const proxy, const Omikron::IamZoneRecord& zone) {
  if (proxy == nullptr || !proxy->registered || !proxy->contact_ready) {
    return {};
  }
  return {.broadphase = zone_intersects_proxy_bounds(zone, proxy->position, proxy->radius),
      .polygon = zone_contains_runtime_xz(zone, proxy->position),
      .heading = zone.accepts_heading_degrees(proxy->heading_degrees)};
}

ZoneContactService::ZoneContactService(
    ZoneContactHost& host, const TriggerProxyService& trigger_proxy)
    : m_host{host},
      m_trigger_proxy{trigger_proxy} {}

void ZoneContactService::reset() {
  m_active_zones.clear();
  m_contacts.clear();
  m_qualification_diagnostics.clear();
  m_lifecycle_pass_pending = false;
}

const CurrentCharacterTriggerProxy* ZoneContactService::proxy() const {
  return m_trigger_proxy.proxy().has_value() ? &m_trigger_proxy.proxy().value() : nullptr;
}

std::expected<void, std::string> ZoneContactService::refresh_active_zones() {
  const ScenarioManager* const manager{m_host.zone_contact_manager()};
  if (manager == nullptr || manager->game_state() == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, "persistent IAM game state is not initialized"};
  }

  const GameState& game_state{*manager->game_state()};
  std::vector<ActiveZoneRef> rebuilt;
  std::size_t area_count{0};
  std::size_t scene_count{0};

  const auto append_enabled = [&game_state, &rebuilt, &area_count, &scene_count](
                                  const std::vector<Omikron::IamZoneRecord>& zones,
                                  const std::size_t resident_slot,
                                  const ActiveZoneSource source,
                                  const std::int32_t area_id,
                                  const std::int32_t scene_id) -> std::expected<void, std::string> {
    for (const Omikron::IamZoneRecord& zone : zones) {
      const std::uint16_t raw_zone_id{static_cast<std::uint16_t>(zone.zone_id)};
      auto enabled{game_state.zone_flag(raw_zone_id)};
      if (!enabled) {
        return std::expected<void, std::string>{std::unexpect,
            fmt::format("cannot read persistent ZONE {}: {}", raw_zone_id, enabled.error())};
      }
      if (!enabled.value()) {
        continue;
      }
      rebuilt.push_back(ActiveZoneRef{.resident_slot = resident_slot,
          .source = source,
          .area_id = area_id,
          .scene_id = scene_id,
          .zone = zone});
      if (source == ActiveZoneSource::k_area) {
        ++area_count;
      } else {
        ++scene_count;
      }
    }
    return {};
  };

  const std::span<const RuntimeAreaSlot> slots{m_host.zone_contact_slots()};
  for (std::size_t index{0}; index < slots.size(); ++index) {
    const RuntimeAreaSlot& slot{slots[index]};
    if (slot.primary.has_value()) {
      const std::vector<Omikron::IamAreaZoneRecord> zones{slot.primary->zones()};
      if (auto appended{
              append_enabled(zones, index, ActiveZoneSource::k_area, slot.primary_area_id, -1)};
          !appended) {
        return appended;
      }
    }
    if (slot.scene.has_value()) {
      const std::vector<Omikron::IamSceneZoneRecord> zones{slot.scene->zones()};
      if (auto appended{append_enabled(
              zones, index, ActiveZoneSource::k_scene, slot.primary_area_id, slot.scene_id)};
          !appended) {
        return appended;
      }
    }
  }

  m_active_zones = std::move(rebuilt);
  App::Log::debug(LogCategory::Scenario,
      "active zones rebuilt — total={} area={} scene={}",
      m_active_zones.size(),
      area_count,
      scene_count);
  return {};
}

std::size_t ZoneContactService::registration_count() const {
  return static_cast<std::size_t>(std::ranges::count_if(
      m_contacts, [](const auto& contact) { return contact != nullptr && contact->spatially_registered(); }));
}

void ZoneContactService::erase_contacts(
    const std::size_t resident_slot, const bool scene_source_only) {
  std::erase_if(m_contacts,
      [resident_slot, scene_source_only](const std::unique_ptr<ZoneContactContext>& contact) {
        return contact != nullptr && contact->resident_slot == resident_slot &&
               (!scene_source_only || contact->source == ActiveZoneSource::k_scene);
      });
}

bool ZoneContactService::backing_resident(const ZoneContactContext& contact) const {
  const std::span<const RuntimeAreaSlot> slots{m_host.zone_contact_slots()};
  if (contact.resident_slot >= slots.size()) {
    return false;
  }
  const RuntimeAreaSlot& slot{slots[contact.resident_slot]};
  if (slot.primary_area_id != contact.area_id ||
      (contact.source == ActiveZoneSource::k_scene && slot.scene_id != contact.scene_id)) {
    return false;
  }
  std::vector<Omikron::IamZoneRecord> zones;
  if (contact.source == ActiveZoneSource::k_area) {
    if (slot.primary.has_value()) {
      zones = slot.primary->zones();
    }
  } else if (slot.scene.has_value()) {
    zones = slot.scene->zones();
  }
  return std::ranges::any_of(zones, [&contact](const Omikron::IamZoneRecord& candidate) {
    return candidate.zone_id == contact.zone.zone_id &&
           candidate.event_offsets == contact.zone.event_offsets;
  });
}

bool ZoneContactService::spatially_qualifies(
    const std::size_t resident_slot, const Omikron::IamZoneRecord& zone) const {
  const ScenarioManager* const manager{m_host.zone_contact_manager()};
  const std::span<const RuntimeAreaSlot> slots{m_host.zone_contact_slots()};
  const CurrentCharacterTriggerProxy* const current_proxy{proxy()};
  if (manager == nullptr || resident_slot >= slots.size() || current_proxy == nullptr) {
    return false;
  }
  const std::optional<ControlledCharacterRef> current{manager->controlled_character()};
  if (!current.has_value() || current->world_scene_id != slots[resident_slot].world_scene_id ||
      current_proxy->owner != current.value()) {
    return false;
  }
  return evaluate_zone_spatial_qualification(current_proxy, zone).qualifies();
}

bool ZoneContactService::reporting_enabled(const ActiveZoneRef& active_zone) const {
  return spatially_qualifies(active_zone.resident_slot, active_zone.zone);
}

ZoneContactContext* ZoneContactService::find_registered(const ActiveZoneRef& active_zone) {
  const ZoneIdentity identity{active_zone.identity()};
  const auto found{std::ranges::find_if(
      m_contacts, [&identity](const std::unique_ptr<ZoneContactContext>& existing) {
        return existing != nullptr && existing->spatially_registered() &&
               existing->identity() == identity;
      })};
  return found == m_contacts.end() ? nullptr : found->get();
}

std::expected<ZoneContactContext*, std::string> ZoneContactService::register_or_refresh(
    const ActiveZoneRef& active_zone) {
  const CurrentCharacterTriggerProxy* const current_proxy{proxy()};
  if (ZoneContactContext* const existing{find_registered(active_zone)}; existing != nullptr) {
    if (existing->heartbeat_state == ZoneContactHeartbeatState::k_awaiting_refresh) {
      existing->heartbeat_state = ZoneContactHeartbeatState::k_refreshed;
    }
    existing->last_heartbeat_generation =
        current_proxy != nullptr ? current_proxy->generation : existing->last_heartbeat_generation;
    return existing;
  }
  if (registration_count() >= k_max_registrations) {
    App::Log::warn(LogCategory::Scenario,
        "zone {} contact ignored — recovered spatial-contact capacity {} reached",
        active_zone.zone.zone_id,
        k_max_registrations);
    return nullptr;
  }
  const std::span<const RuntimeAreaSlot> slots{m_host.zone_contact_slots()};
  if (active_zone.resident_slot >= slots.size()) {
    return std::unexpected("zone resident slot is out of range");
  }
  const RuntimeAreaSlot& slot{slots[active_zone.resident_slot]};
  std::span<const std::byte> record_bytes;
  if (active_zone.source == ActiveZoneSource::k_area) {
    if (slot.primary.has_value()) {
      record_bytes = slot.primary->record_bytes();
    }
  } else if (slot.scene.has_value()) {
    record_bytes = slot.scene->record_bytes();
  }
  if (record_bytes.empty()) {
    return std::unexpected("zone owner record is no longer resident");
  }

  auto contact{std::make_unique<ZoneContactContext>()};
  contact->resident_slot = active_zone.resident_slot;
  contact->source = active_zone.source;
  contact->area_id = active_zone.area_id;
  contact->scene_id = active_zone.scene_id;
  contact->zone = active_zone.zone;
  contact->script = std::make_unique<Script::AreaScriptRuntime>(record_bytes);
  contact->heartbeat_state = ZoneContactHeartbeatState::k_entry_pending;
  contact->last_heartbeat_generation = current_proxy != nullptr ? current_proxy->generation : 0U;
  if (auto entries{contact->script->set_event_entries(Script::AreaScriptEventEntries{
          .event1 = entry_or_missing(active_zone.zone.event_offsets.at(0)),
          .event2 = entry_or_missing(active_zone.zone.event_offsets.at(1)),
          .event3 = entry_or_missing(active_zone.zone.event_offsets.at(2))})};
      !entries) {
    return std::unexpected(
        fmt::format("zone {} event entries: {}", active_zone.zone.zone_id, entries.error()));
  }
  m_host.bind_zone_contact_script(
      *contact->script, active_zone.resident_slot, active_zone.source == ActiveZoneSource::k_scene);
  ZoneContactContext* const registered{contact.get()};
  m_contacts.push_back(std::move(contact));
  App::Log::info(LogCategory::Scenario,
      "ZoneContactHeartbeat — zone={} state=entry-pending registered=true generation={}",
      active_zone.zone.zone_id,
      registered->last_heartbeat_generation);
  return registered;
}

void ZoneContactService::run_pending_lifecycle_pass() {
  if (m_lifecycle_pass_pending) {
    advance_heartbeat_lifecycle();
    m_lifecycle_pass_pending = false;
  }
}

void ZoneContactService::advance_heartbeat_lifecycle() {
  for (const std::unique_ptr<ZoneContactContext>& contact : m_contacts) {
    if (contact == nullptr || contact->script == nullptr || !backing_resident(*contact)) {
      continue;
    }
    Script::AreaScriptRuntime& script{*contact->script};
    switch (contact->heartbeat_state) {
      case ZoneContactHeartbeatState::k_entry_pending:
        if (script.event_entries().event1.has_value()) {
          script.queue_event(1);
          script.activate();
        }
        contact->heartbeat_state = ZoneContactHeartbeatState::k_awaiting_refresh;
        App::Log::info(LogCategory::Scenario,
            "ZoneContactHeartbeat — zone={} state=awaiting-refresh queuedEvent=1",
            contact->zone.zone_id);
        break;
      case ZoneContactHeartbeatState::k_refreshed:
        contact->heartbeat_state = ZoneContactHeartbeatState::k_awaiting_refresh;
        App::Log::debug(LogCategory::Scenario,
            "ZoneContactHeartbeat — zone={} state=awaiting-refresh generation={}",
            contact->zone.zone_id,
            contact->last_heartbeat_generation);
        break;
      case ZoneContactHeartbeatState::k_awaiting_refresh:
        // Runtime 0x00407DC0 gates event 1 on its entry but queues the departure event
        // unconditionally: a context blocked in 0x2F observes it even without event-3 bytecode.
        script.queue_event(3);
        script.activate();
        contact->heartbeat_state = ZoneContactHeartbeatState::k_departure_pending;
        App::Log::info(LogCategory::Scenario,
            "ZoneContactHeartbeat — zone={} state=departure-pending registered=false queuedEvent=3",
            contact->zone.zone_id);
        break;
      case ZoneContactHeartbeatState::k_departure_pending:
        break;
    }
  }
}

std::expected<void, std::string> ZoneContactService::service_scripts(const float delta_seconds) {
  for (const std::unique_ptr<ZoneContactContext>& contact : m_contacts) {
    if (contact == nullptr || contact->script == nullptr || !backing_resident(*contact)) {
      continue;
    }
    Script::AreaScriptRuntime& script{*contact->script};

    // Wait polling belongs to compact service, before structured ScriptLists. A failed wait poll
    // only stalls this zone's own context, so it is reported and the other contacts still run.
    if (auto serviced{m_host.service_zone_contact_waits(script, contact->resident_slot)};
        !serviced) {
      App::Log::warn(
          LogCategory::Script, "zone {} wait polling failed: {}", contact->zone.zone_id, serviced.error());
    }

    const std::optional<std::uint16_t> previous_event{script.active_event()};
    const Script::AreaScriptState state{script.run(delta_seconds)};
    if (previous_event.has_value() && !script.active_event().has_value()) {
      App::Log::info(LogCategory::Script,
          "CompactEventEnded — source=ZONE ownerSlot={} area={} scene={} zone={} event={} "
          "ip=+{:#x}",
          contact->resident_slot,
          contact->area_id,
          contact->scene_id,
          contact->zone.zone_id,
          previous_event.value(),
          script.instruction_pointer());
    }
    if (state == Script::AreaScriptState::k_paused_unsupported ||
        state == Script::AreaScriptState::k_failed) {
      App::Log::warn(LogCategory::Script,
          "zone {} compact VM {}: {}",
          contact->zone.zone_id,
          state == Script::AreaScriptState::k_failed ? "failed" : "paused",
          script.pause_info().reason_text);
    }
  }
  return {};
}

void ZoneContactService::log_qualification_change(
    const ActiveZoneRef& active_zone, const bool qualifies) {
  const ScenarioManager* const manager{m_host.zone_contact_manager()};
  const std::span<const RuntimeAreaSlot> slots{m_host.zone_contact_slots()};
  const RuntimeAreaSlot& slot{slots[active_zone.resident_slot]};
  const std::optional<ControlledCharacterRef> current{
      manager == nullptr ? std::nullopt : manager->controlled_character()};
  const std::int16_t character_id{current.has_value() ? current->character_id : std::int16_t{-1}};
  const ScenarioRuntime* const runtime{
      current.has_value() && current->world_scene_id == slot.world_scene_id
          ? manager->world_runtime(slot.world_scene_id)
          : nullptr};
  const Character::RuntimeCharacter* const character{
      runtime == nullptr || !current.has_value()
          ? nullptr
          : runtime->character_runtime().find_body(current->body_identity)};
  const CurrentCharacterTriggerProxy* const current_proxy{proxy()};
  const Runtime::AreaZoneSpatialBounds zone_bounds{
      Runtime::area_zone_spatial_bounds(active_zone.zone.serialized_vertices)};
  const ZoneSpatialQualification tests{
      evaluate_zone_spatial_qualification(current_proxy, active_zone.zone)};
  App::Log::debug(LogCategory::Scenario,
      "ZoneQualification — source={} ownerSlot={} area={} scene={} zone={} qualifies={} "
      "current={} active={} areaPresent={} controller={} actor=({:.3f},{:.3f},{:.3f}) "
      "proxyRegistered={} contactReady={} proxy=({:.3f},{:.3f},{:.3f}) "
      "actorObjectRadius={:.3f} zoneAabb=({:.3f},{:.3f},{:.3f})..({:.3f},{:.3f},{:.3f}) "
      "generation={} "
      "proxyFrozen={} reason='{}' broadphase={} polygon={} heading={}",
      active_zone.source == ActiveZoneSource::k_area ? "AREA" : "SCENE",
      active_zone.resident_slot,
      active_zone.area_id,
      active_zone.scene_id,
      active_zone.zone.zone_id,
      qualifies,
      character_id,
      character != nullptr && character->active,
      character != nullptr && character->area_present,
      character != nullptr && character->controller_enabled,
      character == nullptr ? 0.0F : character->transform.translation.x,
      character == nullptr ? 0.0F : character->transform.translation.y,
      character == nullptr ? 0.0F : character->transform.translation.z,
      current_proxy != nullptr && current_proxy->registered,
      current_proxy != nullptr && current_proxy->contact_ready,
      current_proxy == nullptr ? 0.0F : current_proxy->position.x,
      current_proxy == nullptr ? 0.0F : current_proxy->position.y,
      current_proxy == nullptr ? 0.0F : current_proxy->position.z,
      current_proxy == nullptr ? 0.0F : current_proxy->radius,
      zone_bounds.minimum.x,
      zone_bounds.minimum.y,
      zone_bounds.minimum.z,
      zone_bounds.maximum.x,
      zone_bounds.maximum.y,
      zone_bounds.maximum.z,
      current_proxy == nullptr ? 0U : current_proxy->generation,
      current_proxy != nullptr && current_proxy->synchronization_suspended,
      current_proxy == nullptr ? "not registered" : current_proxy->suspension_reason,
      tests.broadphase,
      tests.polygon,
      tests.heading);
}

std::expected<OrdinarySpatialSampleResult, std::string>
ZoneContactService::reconcile_from_fresh_spatial_sample() {
  OrdinarySpatialSampleResult result;
  for (const ActiveZoneRef& active_zone : m_active_zones) {
    const bool qualifies{reporting_enabled(active_zone)};
    if (qualifies) {
      ++result.qualifying_zone_count;
    }
    const ZoneIdentity identity{active_zone.identity()};
    const auto diagnostic{std::ranges::find_if(
        m_qualification_diagnostics, [&identity](const ZoneQualificationDiagnostic& candidate) {
          return candidate.identity == identity;
        })};
    if (diagnostic == m_qualification_diagnostics.end() || diagnostic->qualifies != qualifies) {
      if (diagnostic == m_qualification_diagnostics.end()) {
        m_qualification_diagnostics.push_back(
            ZoneQualificationDiagnostic{.identity = identity, .qualifies = qualifies});
      } else {
        diagnostic->qualifies = qualifies;
      }
      log_qualification_change(active_zone, qualifies);
    }
    if (qualifies) {
      if (auto heartbeat{register_or_refresh(active_zone)}; !heartbeat) {
        return std::unexpected(heartbeat.error());
      }
    }
  }
  return result;
}

void ZoneContactService::housekeeping() {
  std::erase_if(m_contacts, [this](const std::unique_ptr<ZoneContactContext>& contact) {
    if (contact == nullptr || contact->script == nullptr) {
      return true;
    }
    if (!backing_resident(*contact)) {
      App::Log::debug(LogCategory::Scenario,
          "zone {} contact removed — backing record is no longer resident",
          contact->zone.zone_id);
      return true;
    }
    const Script::AreaScriptRuntime& script{*contact->script};
    const bool pending_compact_work{script.active_event().has_value() ||
                                    !script.queued_events().empty() ||
                                    script.state() == Script::AreaScriptState::k_waiting};
    if (contact->heartbeat_state == ZoneContactHeartbeatState::k_departure_pending &&
        !pending_compact_work && script.state() == Script::AreaScriptState::k_ready) {
      App::Log::debug(LogCategory::Scenario, "zone {} event completed", contact->zone.zone_id);
      return true;
    }
    return false;
  });
}

}  // namespace App

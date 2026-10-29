#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Scenario/CharacterReferenceRuntime.hpp"
#include "Core/Scenario/RuntimeAreaSlot.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"

namespace App {

/// Native AREA-transition coordinator states (Runtime 0x00408530, global 0x006A0600). The numeric
/// values mirror the native states. A 0x2F whose pre-crossing script is not -1 runs the rich
/// sequence 1 -> 5 -> 6|7 -> 9; otherwise the seamless sequence 3 -> 8.
enum class AreaTransitionState : std::uint8_t {
  k_idle = 0,
  k_rich_loading = 1,
  k_rich_abandoned_loading = 2,
  k_seamless_loading = 3,
  // Native state 4 (seamless release pending) is not modelled: the destination always finishes
  // loading on the service after acceptance, so a 0x30 during loading is ignored instead.
  k_rich_pre_running = 5,
  k_rich_departed_first = 6,
  k_rich_pre_finished_first = 7,
  k_seamless_attached = 8,
  k_rich_post_running = 9,
};

/// What preparing a destination AREA's world produced.
struct PreparedAreaSummary {
  std::vector<Character::MaterializedPlacementResult> characters;
  std::string sky_name;
};

/// Services the controller provides to the transition service. Everything the coordinator needs
/// from character references, script installation, archives and trace recording is routed
/// through here so the service never sees the controller.
class AreaTransitionHost {
 public:
  AreaTransitionHost() = default;
  AreaTransitionHost(const AreaTransitionHost&) = delete;
  AreaTransitionHost(AreaTransitionHost&&) = delete;
  AreaTransitionHost& operator=(const AreaTransitionHost&) = delete;
  AreaTransitionHost& operator=(AreaTransitionHost&&) = delete;

  /// Null before a session is initialized.
  [[nodiscard]] virtual ScenarioManager* transition_manager() const = 0;
  [[nodiscard]] virtual std::span<RuntimeAreaSlot> transition_slots() const = 0;
  [[nodiscard]] virtual CharacterReferenceRuntime& transition_character_references() = 0;
  /// The logical current AREA slot.
  [[nodiscard]] virtual std::size_t transition_active_slot() const = 0;
  [[nodiscard]] virtual std::optional<std::size_t> transition_resident_slot(
      std::int32_t area_id) const = 0;

  /// True once IAM/AREA is loaded (a transition cannot be accepted before that).
  [[nodiscard]] virtual bool area_archive_available() const = 0;
  /// Parses an IAM/AREA record without touching any resident state.
  [[nodiscard]] virtual std::expected<Omikron::IamAreaRecord, std::string> load_area_record(
      std::int32_t area_id) = 0;
  /// Parses an IAM/SCENE record, loading the archive on first use.
  [[nodiscard]] virtual std::expected<Omikron::IamSceneRecord, std::string> load_scene_record(
      std::int32_t scene_id) = 0;

  /// Starts a rich-transition pre/post-crossing script in the slot's world SCX (Runtime
  /// 0x0041DC10). Unlike 0x39/0x3A it performs no camera handoff. Returns the instance ID.
  [[nodiscard]] virtual std::expected<std::size_t, std::string> launch_transition_script(
      std::size_t slot, std::uint16_t script_id) = 0;
  /// True once the tracked instance in the slot's world has completed; an error when it paused or
  /// no longer exists.
  [[nodiscard]] virtual std::expected<bool, std::string> transition_script_completed(
      std::size_t slot, std::size_t instance_id) = 0;

  /// Loads the AREA's decor/SCX dependencies and object/character placements into the slot's world
  /// context (resident, detached).
  [[nodiscard]] virtual std::expected<PreparedAreaSummary, std::string> prepare_area_world(
      std::size_t slot, std::int32_t area_id, const Omikron::IamAreaRecord& record) = 0;
  [[nodiscard]] virtual std::expected<void, std::string> bind_area_characters(std::size_t slot,
      const std::vector<Character::MaterializedPlacementResult>& materialized) = 0;
  /// Creates and activates the slot's primary compact context (event 1 queued).
  [[nodiscard]] virtual std::expected<void, std::string> install_area_script(std::size_t slot) = 0;
  /// Drops the slot's primary compact context and its registration order.
  virtual void discard_area_script(std::size_t slot) = 0;
  /// Wires a SCENE compact context to the session services.
  virtual void bind_scene_script(Script::AreaScriptRuntime& script, std::size_t slot) = 0;
  [[nodiscard]] virtual Script::AreaScriptRuntime* resolve_requester(
      Script::CompactContextHandle handle) = 0;

  /// Zone contacts borrow bytes from the slot's records and must not outlive them.
  virtual void erase_zone_contacts(std::size_t slot, bool scene_source_only) = 0;
  [[nodiscard]] virtual std::expected<void, std::string> refresh_zones() = 0;

  virtual void record_trace(std::string name, std::string detail) = 0;
  /// Publishes a failure as the session's last error.
  virtual void report_error(const std::string& error) = 0;

 protected:
  ~AreaTransitionHost() = default;
};

/// Owns the AREA-transition transaction and the resident-SCENE lifecycle: accepting a compact
/// 0x2F, preparing the alternate resident AREA (with full rollback on failure), sequencing the
/// rich form's pre/post-crossing scripts around the requester's departure, completing the
/// requesting VM's wait, and servicing 0x30 release and 0x47/0x48 SCENE attach/detach.
///
/// Rich form (pre-crossing script != -1; the post-crossing script is ignored otherwise):
///   1 loading       -> destination attached, pre script starts in the source world -> 5
///   5 pre running   -> requester departure (compact event 3) -> 6, pre finished -> 7
///   6 / 7           -> the other of the two -> post script starts in the source world -> 9
///   9 post running  -> post finished: detach the non-current resident decor, resume requester
/// Departure while loading (1 -> 2) prepares the destination without attaching it and returns to
/// idle with the requester still blocked, as Runtime does.
///
/// Runtime also bounds every coordinator phase with a 60-second watchdog
/// (0x00408220): on expiry the requester resumes, any pre/post script still running (or the post
/// script, started then) is awaited before the non-current decor is detached, and the coordinator
/// returns to idle immediately.
///
/// SCENE attach/detach requested while a SCENE or zone script is running is deferred (they destroy
/// the script and the record bytes it borrows); the controller drains them after those phases.
class AreaTransitionService {
 public:
  explicit AreaTransitionService(AreaTransitionHost& host);

  void reset();

  [[nodiscard]] bool pending() const {
    return m_transition.has_value();
  }
  [[nodiscard]] AreaTransitionState state() const {
    return m_state;
  }

  /// Compact 0x2F: accepts, continues immediately, or asks the requester to retry.
  [[nodiscard]] std::expected<Script::AreaTransitionResult, std::string> begin(
      std::size_t owner_slot,
      Script::CompactContextHandle requester,
      const Script::AreaTransitionRequest& request);

  /// Advances an accepted transition through target preparation, the rich crossing scripts and
  /// requester completion, and runs the phase watchdog on `delta_seconds` of scenario time. A
  /// failure latches: every later call reports the same error.
  [[nodiscard]] std::expected<void, std::string> service(float delta_seconds = 0.0F);

  /// Compact 0x30.
  [[nodiscard]] std::expected<void, std::string> release(const Script::AreaReleaseRequest& request);
  /// Compact 0x47 (deferred while `defer_scene_operations(true)`).
  [[nodiscard]] std::expected<void, std::string> attach_scene(
      const Script::AreaSceneAttachRequest& request);
  /// Compact 0x48 (deferred while `defer_scene_operations(true)`).
  [[nodiscard]] std::expected<void, std::string> detach_scene(
      const Script::AreaSceneDetachRequest& request);

  void defer_scene_operations(bool defer) {
    m_defer_scene_operations = defer;
  }
  /// Applies queued SCENE operations in request order.
  [[nodiscard]] std::expected<void, std::string> service_pending_scene_operations();

 private:
  using PendingSceneOperation =
      std::variant<Script::AreaSceneAttachRequest, Script::AreaSceneDetachRequest>;

  struct PendingAreaTransition {
    Script::AreaTransitionHandle handle;
    Script::AreaTransitionRequest request;
    std::size_t source_slot{0};
    std::int32_t source_area_id{-1};
    std::uint32_t source_world_scene_id{0};
    std::uint32_t source_world_generation{0};
    std::size_t destination_slot{0};
    Script::CompactContextHandle requester;
    /// Pre-crossing (states 5/6) or post-crossing (state 9) script instance in the source world.
    std::optional<std::size_t> crossing_script_instance;
    std::string error;
  };

  /// Work a watchdog expiry leaves behind after the coordinator went idle (native context
  /// state 11): await the crossing script, detach the non-current decor, resume the requester.
  struct TimedOutCrossing {
    Script::AreaTransitionHandle handle;
    Script::CompactContextHandle requester;
    std::size_t source_slot{0};
    std::size_t crossing_script_instance{0};
  };

  [[nodiscard]] ScenarioManager* manager() const {
    return m_host.transition_manager();
  }
  [[nodiscard]] RuntimeAreaSlot& slot_at(std::size_t index) const {
    return m_host.transition_slots().subspan(index, 1).front();
  }
  [[nodiscard]] std::size_t slot_count() const {
    return m_host.transition_slots().size();
  }
  /// Enters a coordinator state and restarts the watchdog phase clock.
  void enter_state(AreaTransitionState state);
  /// Latches `error` on the pending transaction and publishes it.
  [[nodiscard]] std::expected<void, std::string> fail_transition(
      PendingAreaTransition& transition, std::string error);
  /// Loads the alternate resident slot (and attaches its decor when `attach`).
  [[nodiscard]] std::expected<void, std::string> prepare_destination(
      PendingAreaTransition& transition, bool attach);
  [[nodiscard]] std::expected<void, std::string> service_crossing(
      PendingAreaTransition& transition, Script::AreaScriptRuntime& requester);
  [[nodiscard]] std::expected<void, std::string> start_post_crossing(
      PendingAreaTransition& transition);
  [[nodiscard]] std::expected<void, std::string> expire_watchdog();
  [[nodiscard]] std::expected<void, std::string> service_timed_out_crossings();
  /// Runtime 0x00419A90 on the resident AREA that is not logically current.
  [[nodiscard]] std::expected<void, std::string> detach_non_current_resident();
  void finish_accepted_transition();
  void reset_transition();
  [[nodiscard]] std::expected<void, std::string> attach_scene_now(
      const Script::AreaSceneAttachRequest& request);
  [[nodiscard]] std::expected<void, std::string> detach_scene_now(
      const Script::AreaSceneDetachRequest& request);
  [[nodiscard]] std::expected<void, std::string> detach_scene_from_slot(std::size_t slot_index);

  AreaTransitionHost& m_host;
  std::optional<PendingAreaTransition> m_transition;
  AreaTransitionState m_state{AreaTransitionState::k_idle};
  /// Watchdog owner: the requester of the current non-idle coordinator transaction, retained
  /// through seamless state 8 after its wait completed.
  std::optional<Script::CompactContextHandle> m_watchdog_requester;
  float m_phase_elapsed_seconds{0.0F};
  std::vector<TimedOutCrossing> m_timed_out_crossings;
  std::uint64_t m_next_generation{1};
  bool m_defer_scene_operations{false};
  std::vector<PendingSceneOperation> m_pending_scene_operations;
};

}  // namespace App

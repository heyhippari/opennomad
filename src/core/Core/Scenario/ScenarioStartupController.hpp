#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Omikron/IamArchive.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Omikron/IamStart.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"

namespace App::Startup {
class StartupTraceRecorder;
}

namespace App::Audio {
class AudioSystem;
}

namespace App {

class ScenarioManager;

/// One runtime area slot, mirroring Runtime's `RuntimeAreaSlot` layout with
/// safe ownership: the parsed AREA and optional attached SCENE are owned here;
/// IDs are ordinary values rather than serialized pointers.
struct RuntimeAreaSlot {
  std::optional<Omikron::IamAreaRecord> primary;
  std::optional<Omikron::IamSceneRecord> scene;
  std::int32_t primary_area_id{-1};
  /// START's linked-AREA relationship, not an attached SCENE ID.
  std::int32_t secondary_area_id{-1};
  std::int32_t scene_id{-1};
  /// Stable ScenarioManager world-scene identity owned by this resident slot.
  /// This is not an IAM AREA ID or an array index.
  std::uint32_t world_scene_id{0};
  /// Independent compact IAM context for the attached SCENE top-level script.
  std::optional<Script::AreaScriptRuntime> scene_script;
};

/// Staged startup that follows the recovered Runtime.exe path:
/// IAM/START -> IAM/AREA record -> area-selected decor/SCX dependencies ->
/// area script context -> interface 29 -> native main menu.
///
/// The controller owns all loaded byte buffers; parsers and the script VM
/// borrow from those buffers, never from raw relocated pointers. Loading a
/// script never executes it: event/state 1 is queued, the context is
/// explicitly activated, and tick() runs the first interpreter tick.
class ScenarioStartupController {
 public:
  /// Interface ID of the main menu (confirmed from IAM/AREA record 118).
  static constexpr std::uint16_t k_main_menu_interface{29};

  ScenarioStartupController() = default;
  ~ScenarioStartupController() = default;

  ScenarioStartupController(const ScenarioStartupController&) = delete;
  ScenarioStartupController(ScenarioStartupController&&) = delete;
  ScenarioStartupController& operator=(const ScenarioStartupController&) = delete;
  ScenarioStartupController& operator=(ScenarioStartupController&&) = delete;

  /// Wires the optional startup trace recorder. Fine-grained IAM/area-script
  /// events are recorded here; coarse mode-level events live in ScenarioEngine.
  void set_trace_recorder(Startup::StartupTraceRecorder* trace);

  /// Wires the application audio system so the AREA music opcode (0x67) can
  /// request numbered tracks. May be null (music is non-fatal).
  void set_audio_system(Audio::AudioSystem* audio);

  /// Selects aventure.SCX into the permanent gameplay-mode slot. Does not
  /// load any IAM data.
  [[nodiscard]] std::expected<void, std::string> select_permanent_mode_script(
      ScenarioManager& manager);

  /// Clears all transient session state (area IDs, mapping, record, script).
  void reset_session();

  /// Mode 2: new-session initialization. Loads IAM/START, selects and loads
  /// the initial area, loads its dependencies, creates the area context, and
  /// queues/activates event 1. Precondition: reset_session().
  [[nodiscard]] std::expected<void, std::string> initialize_new_session(ScenarioManager& manager);

  /// Compatibility entry point: select the permanent mode script, reset the
  /// scenario state, then initialize the new session.
  [[nodiscard]] std::expected<void, std::string> initialize(ScenarioManager& manager);

  /// Executes one area-script interpreter tick (mode 1).
  [[nodiscard]] std::expected<void, std::string> tick(float delta_seconds = 0.0F);

  /// True once the new session has been initialized (area script exists).
  [[nodiscard]] bool initialized() const {
    return m_initialized;
  }

  /// Delivers an interface completion to the waiting area script. Resumes the
  /// script when the completion matches the stored interface handle; logs and
  /// ignores stale/mismatched completions.
  [[nodiscard]] std::expected<void, std::string> complete_interface(
      const InterfaceCompletion& completion);

  /// The UI dispatch owning the native main-menu transition.
  [[nodiscard]] InterfaceDispatcher& dispatcher() {
    return m_dispatcher;
  }
  [[nodiscard]] const InterfaceDispatcher& dispatcher() const {
    return m_dispatcher;
  }

  /// Opens the preliminary interface 29 phase shown while the splash runs.
  /// This is not the final main menu; close_preliminary_29() must be called
  /// before the area script opens interface 29 as the real menu.
  void open_preliminary_29();

  /// Closes the preliminary interface 29 phase (idempotent).
  void close_preliminary_29();

  /// True while the preliminary splash interface 29 phase is open.
  [[nodiscard]] bool preliminary_29_active() const {
    return m_preliminary_29_active;
  }

  /// True once interface 29 has been requested, the main menu is active, and
  /// it has not yet completed.
  [[nodiscard]] bool main_menu_active() const {
    return m_main_menu_active;
  }

  /// The handle of the active main-menu instance, or nullopt when none.
  [[nodiscard]] std::optional<InterfaceHandle> active_handle() const {
    return m_active_handle;
  }

  [[nodiscard]] std::int16_t initial_area_id() const {
    return m_initial_area_id;
  }
  [[nodiscard]] std::int16_t linked_area_id() const {
    return m_linked_area_id;
  }
  /// Reproduced `areaMapping[areaId]` value, or nullopt when unset.
  [[nodiscard]] std::optional<std::int32_t> area_mapping(std::int32_t area_id) const;
  /// All recovered area-mapping entries (diagnostics).
  [[nodiscard]] const std::unordered_map<std::int32_t, std::int32_t>& area_mapping_entries() const;
  [[nodiscard]] const Omikron::IamAreaRecord* area_record() const;
  /// Index of the currently presented resident AREA slot.
  [[nodiscard]] std::size_t active_area_slot() const {
    return m_active_area_slot;
  }
  [[nodiscard]] std::int32_t active_area_id() const;
  [[nodiscard]] const RuntimeAreaSlot* runtime_area_slot(std::size_t index) const;
  [[nodiscard]] bool area_transition_pending() const {
    return m_area_transition.has_value();
  }
  [[nodiscard]] const Script::AreaScriptRuntime* area_script() const;
  /// Authored character ID selected by compact IAM for this session, if any.
  /// The durable owner is ScenarioManager, not this transient AREA context.
  [[nodiscard]] std::optional<std::int16_t> current_controlled_character() const;
  [[nodiscard]] bool ticked() const {
    return m_ticked;
  }
  /// True while an AREA-started dialog owns the global AREA scheduling gate.
  [[nodiscard]] bool dialog_takeover_active() const {
    return m_dialog_takeover_active;
  }
  /// Dialog ID that entered takeover, retained only for diagnostics.
  [[nodiscard]] std::optional<std::int16_t> dialog_takeover_id() const {
    return m_dialog_takeover_id;
  }
  /// Scenario dependency selected by the initial IAM/AREA record.
  [[nodiscard]] const std::string& initial_world_scenario_path() const {
    return m_initial_world_scenario_path;
  }
  /// Decor dependency selected by the initial IAM/AREA record.
  [[nodiscard]] const std::string& initial_world_decor_path() const {
    return m_initial_world_decor_path;
  }
  /// Honest initial decor dependency state (requested/loaded/...).
  [[nodiscard]] const std::string& initial_world_decor_state() const {
    return m_initial_world_decor_state;
  }
  [[nodiscard]] const std::string& last_error() const {
    return m_last_error;
  }

 private:
  /// Reads a whole file through the case-insensitive game-data resolver.
  [[nodiscard]] static std::expected<std::vector<std::byte>, std::string> read_file(
      const std::string& relative_path);

  /// Records one fine-grained startup trace event (no-op without a recorder).
  void record(std::string name, std::string detail = {});

  /// Advances one accepted native AREA transition through target preparation,
  /// transactional residency commit, and exact VM completion.
  [[nodiscard]] std::expected<void, std::string> service_area_transition();
  [[nodiscard]] std::expected<void, std::string> attach_area_scene(
      const Script::AreaSceneAttachRequest& request);
  [[nodiscard]] std::expected<void, std::string> release_area(
      const Script::AreaReleaseRequest& request);
  [[nodiscard]] std::expected<void, std::string> place_current_character_at_address(
      const Script::AreaAddressPlacementRequest& request);
  [[nodiscard]] std::expected<void, std::string> set_address_flag(
      const Script::AreaAddressFlagRequest& request);
  [[nodiscard]] std::expected<void, std::string> add_object_to_persistent_collection(
      const Script::AreaPersistentObjectCollectionRequest& request);
  /// Wires one compact context to playthrough-owned globals and character
  /// profiles. `prefer_scene_definition` preserves the context's resource
  /// ownership when an authored ID exists in both definition tables.
  void bind_compact_state_services(Script::AreaScriptRuntime& runtime,
      std::size_t owner_slot,
      bool prefer_scene_definition);
  [[nodiscard]] std::expected<std::int16_t, std::string> ensure_character_value_profile(
      std::size_t owner_slot,
      bool prefer_scene_definition,
      std::int16_t requested_character_id);
  [[nodiscard]] std::expected<std::int32_t, std::string> character_value(
      std::size_t owner_slot,
      bool prefer_scene_definition,
      const Script::AreaCharacterValueRequest& request);
  [[nodiscard]] std::expected<void, std::string> set_character_value(
      std::size_t owner_slot,
      bool prefer_scene_definition,
      const Script::AreaCharacterValueRequest& request,
      std::int32_t value);
  [[nodiscard]] std::expected<void, std::string> select_current_character(
      std::size_t owner_slot, const Script::AreaCharacterSelectionRequest& request);
  [[nodiscard]] std::expected<void, std::string> set_current_character_presentation(bool enabled);
  [[nodiscard]] std::expected<void, std::string> deactivate_owner_character(
      std::size_t owner_slot, const Script::AreaCharacterDeactivationRequest& request);
  /// Resolves an owner-world SCX script and starts it on either the authored
  /// target or the session-level current controlled character.
  [[nodiscard]] std::expected<std::size_t, std::string> launch_character_script(
      std::size_t owner_slot, const Script::AreaCharacterScriptRequest& request);
  /// Polls a character-bound child through the compact context's owner world.
  [[nodiscard]] std::expected<void, std::string> service_character_script_wait(
      Script::AreaScriptRuntime& area_script, std::size_t owner_slot);
  void bind_scene_compact_services(Script::AreaScriptRuntime& runtime, std::size_t owner_slot);
  void service_scene_scripts(float delta_seconds);
  [[nodiscard]] std::optional<std::size_t> resident_area_slot(std::int32_t area_id) const;
  [[nodiscard]] std::optional<Omikron::IamCameraRecord> active_resident_camera(
      std::int16_t camera_id) const;

  struct PendingAreaTransition {
    Script::AreaTransitionHandle handle;
    Script::AreaTransitionRequest request;
    std::size_t source_slot{0};
    std::size_t destination_slot{0};
    std::string error;
  };

  std::vector<std::byte> m_start_bytes;
  std::vector<std::byte> m_area_archive_bytes;
  std::vector<std::byte> m_scene_archive_bytes;

  std::optional<Omikron::IamStart> m_start;
  std::optional<Omikron::IamIndexedArchive> m_area_archive;
  std::optional<Omikron::IamIndexedArchive> m_scene_archive;
  /// Two resident Runtime AREA slots. Slot 0 holds the initial area; opcode
  /// 0x2F prepares the inactive alternate slot, and 0x47 later commits it.
  std::array<RuntimeAreaSlot, 2> m_area_slots{
      RuntimeAreaSlot{.primary = std::nullopt,
          .scene = std::nullopt,
          .primary_area_id = -1,
          .secondary_area_id = -1,
          .scene_id = -1,
          .world_scene_id = 0,
          .scene_script = std::nullopt},
      RuntimeAreaSlot{.primary = std::nullopt,
          .scene = std::nullopt,
          .primary_area_id = -1,
          .secondary_area_id = -1,
          .scene_id = -1,
          .world_scene_id = 1,
          .scene_script = std::nullopt}};
  std::size_t m_active_area_slot{0};
  /// Resident slot that owns the main AREA compact context, which may differ
  /// from the currently presented slot after a handoff.
  std::size_t m_area_script_owner_slot{0};
  std::optional<PendingAreaTransition> m_area_transition;
  std::uint64_t m_next_area_transition_generation{1};
  std::optional<Script::AreaScriptRuntime> m_area_script;

  /// Runtime-style AREA mapping: START initializes its linked-AREA value and
  /// 0x47 later replaces the entry with the attached SCENE ID.
  std::unordered_map<std::int32_t, std::int32_t> m_area_mapping;

  std::int16_t m_initial_area_id{0};
  std::int16_t m_linked_area_id{0};
  /// Dependencies selected by the initial IAM/AREA record. These describe
  /// startup history only; the current world is always queried from
  /// ScenarioManager because world contexts can later be replaced/recycled.
  std::string m_initial_world_scenario_path;
  std::string m_initial_world_decor_path;
  std::string m_initial_world_decor_state;
  std::string m_last_error;
  bool m_initialized{false};
  bool m_ticked{false};
  /// True once event 1 has been recorded as started (avoids per-frame spam).
  bool m_event_started{false};
  /// True once the transition into interface waiting has been recorded.
  bool m_waiting_recorded{false};
  /// Optional startup trace recorder (fine-grained events).
  Startup::StartupTraceRecorder* m_trace{nullptr};
  /// Application audio system (may be null; music is non-fatal).
  Audio::AudioSystem* m_audio{nullptr};
  /// Scenario owner used by the AREA -> SCX ScriptRuntime bridge.
  ScenarioManager* m_manager{nullptr};
  /// Global scheduling takeover entered only by a successful AREA 0x3D.
  bool m_dialog_takeover_active{false};
  std::optional<std::int16_t> m_dialog_takeover_id;
  /// UI dispatch; pure transport, no lifecycle policy.
  InterfaceDispatcher m_dispatcher;
  /// Preliminary splash interface 29 phase (startup-order fidelity only).
  bool m_preliminary_29_active{false};
  /// Final main-menu instance opened by AREA opcode 0x46.
  bool m_main_menu_active{false};
  std::optional<InterfaceHandle> m_active_handle;
};

}  // namespace App

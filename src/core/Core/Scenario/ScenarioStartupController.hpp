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

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Omikron/IamArchive.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamGlobal.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Omikron/IamStart.hpp"
#include "Core/Omikron/IamZone.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Scenario/CharacterReferenceRuntime.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"

namespace App::Startup {
class StartupTraceRecorder;
}

namespace App::Audio {
class AudioSystem;
}

namespace App::Character {
struct RuntimeCharacter;
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

/// Definition-table provenance for one compact camera resolved from Runtime's
/// complete two-resident IAM namespace.
enum class CompactCameraDefinitionSource : std::uint8_t {
  k_area,
  k_scene,
  k_global,
};

/// Immutable camera definition plus the namespace entry that supplied it.
struct ResolvedCompactCamera {
  Omikron::IamCameraRecord camera;
  CompactCameraDefinitionSource source{CompactCameraDefinitionSource::k_area};
  std::optional<std::size_t> resident_slot;
  std::int32_t area_id{-1};
  std::int32_t scene_id{-1};
};

/// Origin of one transient, residency-derived active spatial-zone record.
enum class ActiveZoneSource : std::uint8_t {
  k_area,
  k_scene,
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
};

/// One transient spatial-contact VM context. Its backing IAM record remains
/// immutable; only the compact context and lifecycle flags are mutable.
struct ZoneContactContext {
  std::size_t resident_slot{0};
  ActiveZoneSource source{ActiveZoneSource::k_area};
  std::int32_t area_id{-1};
  std::int32_t scene_id{-1};
  Omikron::IamZoneRecord zone;
  std::size_t program_record_origin{0};
  std::unique_ptr<Script::AreaScriptRuntime> script;
  bool overlapping{true};
  bool departure_queued{false};
};

struct ZoneQualificationDiagnostic {
  std::uint64_t identity{0};
  bool qualifies{false};
};

/// Session-owned spatial trigger proxy for the durable current character.
/// Runtime updates this independently from scripted presentation transforms.
struct CurrentCharacterStructuredOwner {
  Character::BodyIdentity body_identity{0};
  std::uint32_t script_world_scene_id{0};
  std::uint32_t script_world_generation{0};
  std::size_t script_instance_id{0};
};

struct CurrentCharacterTriggerProxy {
  ControlledCharacterRef owner;
  bool registered{false};
  bool contact_ready{false};
  Runtime::Vec3 position{};
  float radius{0.0F};
  float heading_degrees{0.0F};
  /// The actor spatial-service generation most recently consumed by this proxy.
  /// This is not a movement counter; it advances only when the current actor
  /// genuinely receives the ordinary service path that copies logical XYZ into
  /// the registered spatial proxy.
  std::uint64_t generation{0};
  std::uint64_t last_consumed_actor_spatial_generation{0};
  std::size_t overlapping_zone_count{0};
  bool synchronization_suspended{false};
  std::string suspension_reason;
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

  /// Compact phase: AREA, SCENE, zone contact VM execution. Call before Script_PlayScriptList.
  [[nodiscard]] std::expected<void, std::string> begin_tick(float delta_seconds);

  /// Actor phase: current-character actor, trigger proxy, zone contact qualification. After
  /// structured scripts.
  [[nodiscard]] std::expected<void, std::string> finish_tick(float delta_seconds);

  /// True once the new session has been initialized (area script exists).
  [[nodiscard]] bool initialized() const {
    return m_initialized;
  }
  [[nodiscard]] bool structured_character_owner_active() const {
    return m_current_character_structured_owner.has_value();
  }
  [[nodiscard]] bool actor_phase_pending() const {
    return m_actor_phase_enabled_for_tick;
  }
  [[nodiscard]] std::size_t character_reference_entry_count() const {
    return m_character_references.entries().size();
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
  /// Persistent `areaMap[areaId]` value, or nullopt for an invalid ID/state.
  [[nodiscard]] std::optional<std::int32_t> area_mapping(std::int32_t area_id) const;
  /// Canonical recovered area-map table (diagnostics).
  [[nodiscard]] std::span<const std::int16_t> area_mapping_entries() const;
  [[nodiscard]] const Omikron::IamAreaRecord* area_record() const;
  /// Index of the currently presented resident AREA slot.
  [[nodiscard]] std::size_t active_area_slot() const {
    return m_active_area_slot;
  }
  [[nodiscard]] std::int32_t active_area_id() const;
  [[nodiscard]] const RuntimeAreaSlot* runtime_area_slot(std::size_t index) const;
  /// Transient enabled zone records rebuilt from the two resident AREA slots.
  [[nodiscard]] std::span<const ActiveZoneRef> active_zones() const {
    return m_active_zones;
  }
  /// Number of live zone-owned compact contexts, exposed for focused lifecycle tests.
  [[nodiscard]] std::size_t zone_contact_count() const {
    return m_zone_contacts.size();
  }
  /// One live zone-owned compact context for debugger registry inspection.
  [[nodiscard]] const ZoneContactContext* zone_contact(std::size_t index) const {
    return index < m_zone_contacts.size() ? m_zone_contacts.at(index).get() : nullptr;
  }
  [[nodiscard]] const std::optional<CurrentCharacterTriggerProxy>& current_character_trigger_proxy()
      const {
    return m_current_character_trigger_proxy;
  }
  [[nodiscard]] bool area_transition_pending() const {
    return m_area_transition.has_value();
  }
  /// Primary compact context for the currently presented resident AREA.
  [[nodiscard]] const Script::AreaScriptRuntime* area_script() const;
  /// Primary compact context owned by one resident AREA slot. This diagnostic
  /// accessor exposes the Phase-1 two-resident architecture without transferring
  /// ownership or manufacturing a Runtime.exe registry slot.
  [[nodiscard]] const Script::AreaScriptRuntime* area_script(std::size_t resident_slot) const;
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

  /// Accepts one 0x2F transition on behalf of the exact resident AREA context
  /// that executed it. The source owner must remain stable while the alternate
  /// resident slot is prepared.
  [[nodiscard]] std::expected<Script::AreaTransitionHandle, std::string> begin_area_transition(
      std::size_t owner_slot, const Script::AreaTransitionRequest& request);
  /// Creates and activates the primary compact context owned by a resident AREA.
  /// Runtime registers event 1 when the AREA is loaded, before any later 0x47
  /// presentation handoff makes that resident slot active.
  [[nodiscard]] std::expected<void, std::string> install_primary_area_script(
      std::size_t owner_slot);
  /// Advances one accepted native AREA transition through target preparation,
  /// resident primary-context creation, and exact requesting-VM completion.
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
  /// Rebuilds transient zone residency after a ZONE flag or resident-record change.
  [[nodiscard]] std::expected<void, std::string> refresh_active_zones();
  /// Applies persistent ZONE state, then rebuilds resident active zones.
  [[nodiscard]] std::expected<void, std::string> set_zone_activation(
      const Script::AreaZoneActivationRequest& request);
  /// Applies 0x4C/0x4D to the owner AREA first, then its attached SCENE.
  [[nodiscard]] std::expected<void, std::string> set_object_placement_state(
      std::size_t owner_slot, const Script::AreaObjectPlacementStateRequest& request);
  /// Wires one compact context to playthrough-owned globals and character
  /// profiles. `prefer_scene_definition` preserves the context's resource
  /// ownership when an authored ID exists in both definition tables.
  void bind_compact_state_services(
      Script::AreaScriptRuntime& runtime, std::size_t owner_slot, bool prefer_scene_definition);
  [[nodiscard]] std::expected<std::int16_t, std::string> ensure_character_value_profile(
      std::size_t owner_slot, bool prefer_scene_definition, std::int16_t requested_character_id);
  [[nodiscard]] std::expected<std::int32_t, std::string> character_value(std::size_t owner_slot,
      bool prefer_scene_definition,
      const Script::AreaCharacterValueRequest& request);
  [[nodiscard]] std::expected<void, std::string> set_character_value(std::size_t owner_slot,
      bool prefer_scene_definition,
      const Script::AreaCharacterValueRequest& request,
      std::int32_t value);
  [[nodiscard]] std::expected<void, std::string> select_current_character(
      std::size_t owner_slot, const Script::AreaCharacterSelectionRequest& request);
  [[nodiscard]] std::expected<void, std::string> set_current_character_presentation(bool enabled);
  [[nodiscard]] std::expected<void, std::string> select_current_character_move(
      const Script::AreaCurrentCharacterMoveRequest& request);
  [[nodiscard]] std::expected<void, std::string> set_current_character_controller(
      const Script::AreaCurrentCharacterControllerRequest& request);
  /// Services the controlled character's enabled adventure CTL controller
  /// once per scenario tick, before zone-contact production, using the
  /// session's current CTL profile input mask.
  void service_ctl_controller(float delta_seconds);
  void service_current_character_actor(float delta_seconds);
  /// Starts a compact-owned dialog and enters the session-global scheduling
  /// takeover shared by AREA, SCENE, and contact contexts.
  [[nodiscard]] std::expected<void, std::string> start_compact_dialog(
      const Script::AreaDialogRequest& request);
  /// Resolves the recovered IAM/OBJECT voice-over variant and submits its
  /// independent audio/subtitle presentation to the owner world.
  [[nodiscard]] std::expected<void, std::string> present_compact_object(
      const Script::AreaObjectActivationRequest& request);
  [[nodiscard]] std::expected<void, std::string> deactivate_owner_character(
      std::size_t owner_slot, const Script::AreaCharacterDeactivationRequest& request);
  /// Resolves and starts a generic 0x39/0x3A SCX child in the compact
  /// context's owner world. Runtime follows a successful launch by switching
  /// the camera controller to mode 13 using operand B as its duration.
  [[nodiscard]] std::expected<std::size_t, std::string> launch_scx_script(
      std::size_t owner_slot, const Script::AreaScxScriptRequest& request);
  /// Primary AREA character activation differs slightly from attached-SCENE
  /// activation: an attached SCENE definition may own the requested body.
  [[nodiscard]] std::expected<void, std::string> activate_primary_character(
      std::size_t owner_slot, const Script::AreaCharacterActivationRequest& request);
  /// Resolves an owner-world SCX script and starts it on either the authored
  /// target or the session-level current controlled character.
  [[nodiscard]] std::expected<std::size_t, std::string> launch_character_script(
      std::size_t owner_slot, const Script::AreaCharacterScriptRequest& request);
  /// Polls a generic tracked 0x3A child through the compact context's owner
  /// world rather than whichever resident world is currently presented.
  [[nodiscard]] std::expected<void, std::string> service_scx_script_wait(
      Script::AreaScriptRuntime& area_script, std::size_t owner_slot);
  /// Polls a character-bound child through the compact context's owner world.
  [[nodiscard]] std::expected<void, std::string> service_character_script_wait(
      Script::AreaScriptRuntime& area_script, std::size_t owner_slot);
  /// Services every resident primary AREA context in creation/registration
  /// order. The older source context therefore resumes its 0x47 handoff before
  /// the newly loaded destination executes event 1.
  [[nodiscard]] std::expected<void, std::string> service_area_scripts(float delta_seconds);
  /// Delivers presentation-owned camera completions to the exact compact
  /// context suspended on their operation generation.
  [[nodiscard]] std::expected<void, std::string> service_camera_completions();
  void bind_scene_compact_services(Script::AreaScriptRuntime& runtime,
      std::size_t owner_slot,
      bool prefer_scene_definition = true);
  /// Finds the first camera in Runtime's slot0 AREA/SCENE, slot1 AREA/SCENE,
  /// then IAM/GLOBAL namespace. Definition lookup is independent of caller.
  [[nodiscard]] std::optional<ResolvedCompactCamera> resolve_compact_camera(
      std::int16_t camera_id) const;
  /// Submits globally resolved immutable fields through the calling compact
  /// context's owner world, where live attachment resolution occurs. Missing
  /// cameras resolve as a no-op and return no tracked operation handle.
  [[nodiscard]] std::expected<std::optional<Script::AreaCameraOperationHandle>, std::string>
  enqueue_compact_camera(std::size_t owner_slot, const Script::AreaCameraRequest& request);
  void service_scene_scripts(float delta_seconds);
  /// Compact phase: Zone contact VM execution (waits, script.run, events).
  [[nodiscard]] std::expected<void, std::string> service_zone_contact_scripts(float delta_seconds);
  /// Post-actor phase: Zone contact qualification, creation, spatial matching, lifecycle.
  [[nodiscard]] std::expected<void, std::string> reconcile_zone_contacts();
  [[nodiscard]] bool zone_contact_backing_resident(const ZoneContactContext& contact) const;
  [[nodiscard]] bool zone_contact_spatially_matches(const ZoneContactContext& contact) const;
  [[nodiscard]] bool zone_contact_reporting_enabled(const ZoneContactContext& contact) const;
  [[nodiscard]] bool zone_contact_reporting_enabled(const ActiveZoneRef& active_zone) const;
  void register_current_character_trigger_proxy(
      const ControlledCharacterRef& owner, const Character::RuntimeCharacter& character);
  void service_current_character_trigger_proxy();
  [[nodiscard]] std::expected<void, std::string> create_zone_contact(
      const ActiveZoneRef& active_zone);
  [[nodiscard]] std::optional<std::size_t> resident_area_slot(std::int32_t area_id) const;

  struct PendingAreaTransition {
    Script::AreaTransitionHandle handle;
    Script::AreaTransitionRequest request;
    std::size_t source_slot{0};
    std::size_t destination_slot{0};
    std::string error;
  };

  std::vector<std::byte> m_start_bytes;
  std::vector<std::byte> m_global_bytes;
  std::vector<std::byte> m_area_archive_bytes;
  std::vector<std::byte> m_scene_archive_bytes;
  std::vector<std::byte> m_object_archive_bytes;

  std::optional<Omikron::IamStart> m_start;
  std::optional<Omikron::IamGlobal> m_global;
  std::optional<Omikron::IamIndexedArchive> m_area_archive;
  std::optional<Omikron::IamIndexedArchive> m_scene_archive;
  std::optional<Omikron::IamFixedStrideArchive> m_object_archive;
  /// Two resident Runtime AREA slots. Slot 0 holds the initial area; opcode
  /// 0x2F prepares the inactive alternate slot, and 0x47 later commits it.
  std::array<RuntimeAreaSlot, 2> m_area_slots{RuntimeAreaSlot{.primary = std::nullopt,
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
  /// Runtime registers one independent primary compact context per resident
  /// AREA. These are intentionally separate from attached-SCENE contexts.
  std::array<std::optional<Script::AreaScriptRuntime>, 2> m_area_scripts{};
  /// Monotonic registration order. Slot indices are reusable, so fixed slot
  /// order is not equivalent to Runtime's context-list order after a recycle.
  std::array<std::uint64_t, 2> m_area_script_sequences{};
  std::array<bool, 2> m_area_event_started_recorded{};
  std::array<bool, 2> m_area_waiting_recorded{};
  std::optional<PendingAreaTransition> m_area_transition;
  std::uint64_t m_next_area_script_sequence{1};
  std::uint64_t m_next_area_transition_generation{1};
  std::uint64_t m_next_camera_operation_generation{1};
  std::vector<ActiveZoneRef> m_active_zones;
  std::vector<std::unique_ptr<ZoneContactContext>> m_zone_contacts;
  std::vector<ZoneQualificationDiagnostic> m_zone_qualification_diagnostics;
  std::optional<CurrentCharacterStructuredOwner> m_current_character_structured_owner;
  std::optional<CurrentCharacterTriggerProxy> m_current_character_trigger_proxy;
  CharacterReferenceRuntime m_character_references;
  std::uint64_t m_next_trigger_proxy_generation{1};

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
  /// Gating flag for phase separation: set true at end of begin_tick(), cleared at start of
  /// finish_tick().
  bool m_actor_phase_enabled_for_tick{false};
  /// Optional startup trace recorder (fine-grained events).
  Startup::StartupTraceRecorder* m_trace{nullptr};
  /// Application audio system (may be null; music is non-fatal).
  Audio::AudioSystem* m_audio{nullptr};
  /// Scenario owner used by the AREA -> SCX ScriptRuntime bridge.
  ScenarioManager* m_manager{nullptr};
  /// Global scheduling takeover entered by any successful compact 0x3D.
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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Dialog/DialogPerformanceRuntime.hpp"
#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/GameState.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Omikron/SFX.hpp"
#include "Core/WorldPresentation.hpp"

namespace App::Script {
class ScriptRuntime;
}

namespace App::Audio {
class AudioSystem;
}

namespace App {
class ScenarioRuntime;
}

namespace App {

/// Gameplay mode selection. Each mode has a dedicated scenario slot.
enum class GameplayMode : std::uint8_t {
  /// Third-person adventure/tank-control gameplay (aventure.scx).
  Adventure,
  /// First-person shooting gameplay (shoot2.scx) — declarative; not loaded in
  /// this milestone.
  FirstPersonShooting,
  /// Hand-to-hand fighting gameplay (fight.scx) — declarative; not loaded in
  /// this milestone.
  HandToHandCombat,
};

/// Scenario role: either the gameplay-mode slot or a world-scene context.
enum class ScenarioRole : std::uint8_t {
  /// Dedicated gameplay-mode scenario slot.
  GameplayMode,
  /// Ordinary world-scene residency context.
  WorldScene,
};

/// World-scene residency state. A context may be free, loaded but inactive
/// (detached and recyclable), or loaded and active (attached and eviction-
/// resistant).
enum class WorldSceneResidencyState : std::uint8_t {
  /// No loaded scenario or world model; available for allocation.
  Free,
  /// Loaded scenario and optional world model, but detached/inactive. The
  /// context is recyclable if a newer load requires space.
  LoadedInactive,
  /// Loaded scenario and optional world model, attached and active. The
  /// context is non-evictable; new loads must find a free or recyclable
  /// entry or fail cleanly.
  LoadedActive,
};

/// Durable session-level identity of the body currently controlled by compact
/// IAM. The character ID is authored data; the world scene ID identifies the
/// owning runtime instance rather than an AREA-slot array position.
struct ControlledCharacterRef {
  /// Canonical/current profile character identity.
  std::int16_t character_id{-1};
  /// Stable physical/live actor identity.
  Character::BodyIdentity body_identity{0};
  std::uint32_t world_scene_id{0};

  bool operator==(const ControlledCharacterRef&) const = default;
};

struct LocatedCharacterBody {
  std::uint32_t world_scene_id{0};
  ScenarioRuntime* runtime{nullptr};
  Character::RuntimeCharacter* character{nullptr};
};

struct LocatedConstCharacterBody {
  std::uint32_t world_scene_id{0};
  const ScenarioRuntime* runtime{nullptr};
  const Character::RuntimeCharacter* character{nullptr};
};

/// Declarative definition of a gameplay mode: the logical mode and its
/// scenario path. Only `Adventure` is loaded during default boot; the other
/// mappings exist so a future milestone can replace the mode slot without
/// touching either world context.
struct GameplayModeDefinition {
  GameplayMode mode;
  std::string_view scenario_path;
};

/// The three gameplay-mode scenario mappings recovered from Runtime.exe's
/// fixed string references. Loaded declaratively; never opened automatically.
inline constexpr GameplayModeDefinition k_gameplay_mode_scenarios[]{
    {GameplayMode::Adventure, "SCPTDATA/aventure.scx"},
    {GameplayMode::FirstPersonShooting, "SCPTDATA/shoot2.scx"},
    {GameplayMode::HandToHandCombat, "SCPTDATA/fight.scx"},
};

/// Snake-case name of a gameplay mode (diagnostics).
[[nodiscard]] constexpr std::string_view gameplay_mode_name(const GameplayMode mode) {
  switch (mode) {
    case GameplayMode::Adventure:
      return "adventure";
    case GameplayMode::FirstPersonShooting:
      return "first_person_shooting";
    case GameplayMode::HandToHandCombat:
      return "hand_to_hand_combat";
  }
  return "unknown";
}

/// Scenario path of a gameplay mode, or an empty view for an unknown mode.
[[nodiscard]] inline std::string_view gameplay_mode_scenario_path(const GameplayMode mode) {
  for (const GameplayModeDefinition& definition : k_gameplay_mode_scenarios) {
    if (definition.mode == mode) {
      return definition.scenario_path;
    }
  }
  return {};
}

/// Stable identity of a scenario slot or context, independent of array
/// index. Includes a generation counter to detect use-after-free.
struct ScenarioIdentity {
  ScenarioRole role;
  std::uint32_t slot;  ///< Mode slot index (0) or world context index (0 or 1).
  std::uint32_t generation;
};

/// World-scene residency context: owns a loaded scenario and an optional
/// level/decor model, tracked through a lifecycle state.
struct WorldSceneContext {
  static constexpr std::size_t k_capacity = 2;

  /// Stable scene/context ID independent of array index.
  std::uint32_t scene_id{0};
  /// Current lifecycle state.
  WorldSceneResidencyState residency{WorldSceneResidencyState::Free};
  /// Generation counter for handle validity checking.
  std::uint32_t generation{0};
  /// Requested decor/level model path, or nullopt if not associated yet.
  std::optional<std::string> decor_path;
  /// Resolved decor/level model path (empty until a decor association is
  /// recovered and loaded).
  std::string resolved_decor_path;
  /// Requested scenario path (e.g., "SCPTDATA/Hall27.SCX").
  std::string scenario_path;
  /// Resolved on-disk scenario path (diagnostics).
  std::string resolved_scenario_path;
  /// Parsed SCX data (owned here for this milestone).
  std::optional<Omikron::ScxData> scx_data;
  /// Optional immutable retail SFX companion data and backing bytes.
  std::optional<Omikron::SfxData> sfx_data;
  std::vector<std::byte> sfx_file_buffer;
  std::string resolved_sfx_path;
  /// Scenario-local byte buffer for the loaded file.
  std::vector<std::byte> scx_file_buffer;
  /// Parsed file size in bytes.
  std::size_t file_size_bytes{0};
  /// Last load/parse error (kept for the inspector).
  std::string last_error;

  /// CPU-side decor (level) model owned by this context. Loaded from the
  /// resolved decor path; nullopt when the context has no decor or its load
  /// failed. GL resources are never built here — presentation (WorldScene /
  /// a future WorldRenderer) reads this CPU data.
  std::optional<Omikron::Model3DOData> decor_model;

  /// Per-context mutable scenario runtime (script runtime, sprite pool, sound
  /// resources). Owned here so each SCX slot keeps its own state; built
  /// transactionally before the context is installed.
  std::unique_ptr<ScenarioRuntime> runtime;
};

/// Represents one scenario slot (gameplay-mode or world-context) for
/// inspection and state reporting.
struct LoadedScenarioView {
  ScenarioIdentity identity;
  WorldSceneResidencyState residency{WorldSceneResidencyState::Free};
  std::uint32_t scene_id{0};
  std::string scenario_path;
  std::string resolved_path;
  std::string decor_path;
  std::string resolved_decor_path;
  std::size_t file_size{0};
  std::uint32_t file_version{0};
  std::size_t script_count{0};
  std::size_t active_script_instances{0};
  std::size_t sound_count{0};
  std::size_t sprite_count{0};
  std::size_t model_count{0};
  std::size_t shared_value_count{0};
  std::size_t active_voices{0};
  std::size_t render_instances{0};
  bool sfx_loaded{false};
  std::size_t sfx_definition_count{0};
  std::size_t sfx_node_count{0};
  std::size_t sfx_track_count{0};
  std::size_t active_sfx_nodes{0};
  std::size_t queued_sfx_requests{0};
  std::size_t active_sfx_particles{0};
  std::size_t sfx_attached_sprites{0};
  bool loaded{false};
  std::string last_error;
};

/// Manages the three-slot scenario architecture: one dedicated gameplay-mode
/// slot and two ordinary world-scene contexts. Owns all SCX parsing, runtime
/// resource preparation, and lifecycle state.
class ScenarioManager {
 public:
  ScenarioManager();
  ~ScenarioManager();
  ScenarioManager(const ScenarioManager&) = delete;
  ScenarioManager(ScenarioManager&&) = delete;
  ScenarioManager& operator=(const ScenarioManager&) = delete;
  ScenarioManager& operator=(ScenarioManager&&) = delete;

  // --- Gameplay-mode slot operations ----------------------------------------

  /// Current gameplay mode (default: Adventure).
  [[nodiscard]] GameplayMode current_gameplay_mode() const;

  /// Stable identity of the gameplay-mode slot. Its generation advances
  /// whenever the slot's runtime is replaced.
  [[nodiscard]] ScenarioIdentity gameplay_identity() const;

  /// Requested and case-resolved paths of the gameplay-mode scenario.
  [[nodiscard]] std::string_view gameplay_scenario_path() const;
  [[nodiscard]] std::string_view gameplay_resolved_scenario_path() const;

  /// Sets the gameplay-mode scenario to the specified mode. Replaces the
  /// current mode slot with the new one, transactionally. Fails cleanly if
  /// load/parse fails. Does not affect either world context.
  [[nodiscard]] std::expected<void, std::string> set_gameplay_mode(GameplayMode mode);

  // --- World-context operations ---------------------------------------------

  /// Loads a scenario and optional decor into a world context. If a free
  /// entry exists, uses it; otherwise attempts to recycle the lowest-index
  /// LoadedInactive entry. Fails cleanly and leaves all contexts intact if:
  /// - no free or recyclable entry is available (both are LoadedActive);
  /// - the scenario fails to parse.
  /// Returns the context, which is left in LoadedInactive state until
  /// explicitly activated.
  [[nodiscard]] std::expected<WorldSceneContext*, std::string> load_world_context(
      std::uint32_t scene_id,
      std::optional<std::string> decor_path,
      const std::string& scenario_path);

  /// Transitions a world context from LoadedInactive to LoadedActive,
  /// attaching its world model and preparing it for script activation.
  /// No-op if already active. Fails if the context is Free.
  [[nodiscard]] std::expected<void, std::string> activate_world_context(std::uint32_t scene_id);

  /// Transitions a world context from LoadedActive to LoadedInactive,
  /// detaching its world model. No-op if already inactive. Fails if the
  /// context is Free. Does not destroy the loaded scenario.
  [[nodiscard]] std::expected<void, std::string> deactivate_world_context(std::uint32_t scene_id);

  /// Atomically switches presentation residency from one prepared world to
  /// another. Both contexts remain loaded; source becomes inactive and target
  /// becomes the sole active world.
  [[nodiscard]] std::expected<void, std::string> switch_active_world_context(
      std::uint32_t source_scene_id, std::uint32_t target_scene_id);

  /// Completely unloads a world context, freeing its scenario, world model,
  /// audio voices and sprite instances. Transitions to Free. Fails if the
  /// context is LoadedActive (use deactivate first).
  [[nodiscard]] std::expected<void, std::string> unload_world_context(std::uint32_t scene_id);

  /// Current controlled body for this game session, if compact IAM has
  /// selected one. It deliberately survives AREA slot changes.
  [[nodiscard]] std::optional<ControlledCharacterRef> controlled_character() const;
  void set_controlled_character(ControlledCharacterRef character);
  void clear_controlled_character();
  [[nodiscard]] std::optional<LocatedCharacterBody> find_character_body(
      Character::BodyIdentity body_identity);
  [[nodiscard]] std::optional<LocatedConstCharacterBody> find_character_body(
      Character::BodyIdentity body_identity) const;

  /// The frame's raw CTL profile-0 slot bits supplied by the application from
  /// InputManager. The CTL controller applies the 14-slot semantics and the
  /// 0x40000000 no-input sentinel; scenario code never reads raw devices.
  void set_ctl_input_mask(std::uint32_t mask) {
    m_ctl_input_mask = mask;
  }
  [[nodiscard]] std::uint32_t ctl_input_mask() const {
    return m_ctl_input_mask;
  }

  /// Moves the selected body from source to target when the selection belongs
  /// to source. No model/resource reload occurs; all live character state is
  /// transferred with the single runtime owner.
  [[nodiscard]] std::expected<void, std::string> transfer_controlled_character(
      std::uint32_t source_scene_id, std::uint32_t target_scene_id);

  /// Tears down both world contexts for a new scenario session (mode 2).
  /// The gameplay-mode slot is left intact; only world-scene residency is
  /// cleared.
  [[nodiscard]] std::expected<void, std::string> reset_for_new_session();

  /// Installs a fresh mutable session state copied from immutable IAM/START.
  [[nodiscard]] std::expected<void, std::string> initialize_game_state(
      const Omikron::IamStart& start);

  /// Session-owned persistent state, if IAM/START has initialized it.
  [[nodiscard]] GameState* game_state();
  [[nodiscard]] const GameState* game_state() const;

  /// Finds a world context by scene ID.
  [[nodiscard]] WorldSceneContext* find_world_context(std::uint32_t scene_id);
  [[nodiscard]] const WorldSceneContext* find_world_context(std::uint32_t scene_id) const;

  /// The currently attached (LoadedActive) world context, or nullptr when no
  /// context is active. This is the presentation target of WorldScene.
  [[nodiscard]] WorldSceneContext* active_world_context();
  [[nodiscard]] const WorldSceneContext* active_world_context() const;

  /// Snapshot of all world context entries (for inspection).
  [[nodiscard]] std::span<const WorldSceneContext, 2> world_contexts() const;

  // --- Scenario ownership and inspection ------------------------------------

  /// Parsed SCX data of the current gameplay-mode scenario, or nullptr if not
  /// yet loaded.
  [[nodiscard]] const Omikron::ScxData* gameplay_mode_scx() const;

  /// Raw file bytes backing the gameplay-mode scenario's SCX data (offsets in
  /// the parsed data refer into this buffer). Empty when not loaded.
  [[nodiscard]] std::span<const std::byte> gameplay_mode_scx_bytes() const;

  /// Parsed SCX data of a world context, or nullptr if not loaded.
  [[nodiscard]] const Omikron::ScxData* world_context_scx(std::uint32_t scene_id) const;

  /// Consolidated view for inspection: gameplay-mode slot plus all world
  /// contexts (even if Free).
  [[nodiscard]] std::vector<LoadedScenarioView> scenario_inventory() const;

  /// Currently loaded scenario count (0 to 3).
  [[nodiscard]] std::size_t loaded_scenario_count() const;

  /// Count of active script instances across all loaded scenarios.
  [[nodiscard]] std::size_t active_script_instances_total() const;

  /// Mutable gameplay-mode scenario runtime (sprite pool, script runtime,
  /// sound resources) built from the current gameplay-mode scenario. Null
  /// until a gameplay-mode scenario is installed. ModelViewerScene uses this
  /// slot directly; debug tools can select either this runtime or any world
  /// runtime explicitly.
  [[nodiscard]] ScenarioRuntime* gameplay_runtime() const;

  /// Mutable scenario runtime of a world context, or nullptr when the context
  /// is not loaded. World-context scripts belong to this runtime, not the
  /// gameplay-mode runtime.
  [[nodiscard]] ScenarioRuntime* world_runtime(std::uint32_t scene_id) const;

  /// Mutable scenario runtimes of every LoadedActive world context, for the
  /// per-frame scheduler. LoadedInactive and Free contexts contribute none.
  [[nodiscard]] std::vector<ScenarioRuntime*> active_world_runtimes() const;

  // --- Audio subsystem integration (future) ---------------------------------

  /// Injects the audio system for scenario-owned voice lifecycle management.
  void set_audio_system(Audio::AudioSystem* audio_system);

  [[nodiscard]] WorldPresentationState& world_presentation() {
    return m_world_presentation;
  }
  [[nodiscard]] const WorldPresentationState& world_presentation() const {
    return m_world_presentation;
  }

  // --- Session-level dialog subsystem --------------------------------------

  /// Loads IAM/DIALOG once on first use, selects `dialog_id`, and starts its
  /// CPU runtime at node 0. AREA opcode 0x3D reaches this through the startup
  /// controller's typed bridge.
  [[nodiscard]] std::expected<void, std::string> start_dialog(std::uint16_t dialog_id);

  [[nodiscard]] Dialog::DialogRuntime& dialog_runtime() {
    return m_dialog_runtime;
  }
  [[nodiscard]] const Dialog::DialogRuntime& dialog_runtime() const {
    return m_dialog_runtime;
  }

  /// Services the synchronized 3DM layer after ordinary world runtimes tick.
  void service_dialog_performance(float real_delta_seconds);

  [[nodiscard]] const Dialog::DialogPerformanceRuntime& dialog_performance() const {
    return m_dialog_performance;
  }

 private:
  /// Applies the authored IAM/DIALOG camera pair exactly once for each
  /// DialogRuntime presentation generation.
  ///
  /// Runtime treats a pair as:
  ///   camera[0] = immediate camera
  ///   camera[1] = 160-unit camera transition
  ///
  /// Main/NPC presentation uses line_cameras; player-response presentation
  /// uses response_cameras.
  void service_dialog_camera();

  /// A fully loaded scenario package: the parsed data plus the backing byte
  /// buffer whose offsets the parsed data indexes into.
  struct LoadedScenario {
    std::string resolved_path;
    std::vector<std::byte> file_buffer;
    Omikron::ScxData scx_data;
    std::string resolved_sfx_path;
    std::vector<std::byte> sfx_file_buffer;
    std::optional<Omikron::SfxData> sfx_data;
  };

  /// Gameplay-mode scenario slot state.
  struct GameplayModeSlot {
    GameplayMode current_mode{GameplayMode::Adventure};
    std::uint32_t generation{0};
    std::string scenario_path;
    std::string resolved_path;
    Omikron::ScxData scx_data;
    std::vector<std::byte> file_buffer;
    std::string resolved_sfx_path;
    std::vector<std::byte> sfx_file_buffer;
    std::optional<Omikron::SfxData> sfx_data;
    std::size_t file_size_bytes{0};
    std::string last_error;
    /// Mutable scenario runtime owned by this slot (built transactionally).
    std::unique_ptr<ScenarioRuntime> runtime;
  };

  GameplayModeSlot m_gameplay_mode_slot;
  std::array<WorldSceneContext, WorldSceneContext::k_capacity> m_world_contexts;
  std::optional<ControlledCharacterRef> m_controlled_character;
  /// Frame-scoped CTL profile slot bits (0 until the application feeds them).
  std::uint32_t m_ctl_input_mask{0};
  /// Durable IAM/START-derived flags and object collections. This outlives
  /// AREA/SCENE slot swaps and world-runtime residency changes.
  std::optional<GameState> m_game_state;

  /// CPU-only command mailbox between AREA/scenario execution and the stable
  /// WorldScene presentation layer.
  WorldPresentationState m_world_presentation;

  /// Session-level immutable IAM/DIALOG archive cache and active progression.
  std::vector<std::byte> m_dialog_archive;
  Dialog::DialogRuntime m_dialog_runtime;
  Dialog::DialogPerformanceRuntime m_dialog_performance;

  /// Last DialogRuntime generation whose authored camera pair was consumed.
  /// DialogRuntime increments generation on every presentation-state change,
  /// so this prevents a 160-unit camera transition from restarting each frame.
  std::optional<std::uint64_t> m_dialog_camera_generation;

  Audio::AudioSystem* m_audio_system{nullptr};  ///< Non-owning.

  /// Normalizes legacy Windows separators to the portable form.
  [[nodiscard]] static std::string normalize_asset_path(std::string path);

  /// Loads the SCX from disk, returning a resolved-path/bytes/parsed-data
  /// package. Errors include missing file, parse failure, and truncation.
  [[nodiscard]] std::expected<LoadedScenario, std::string> load_scenario(
      const std::string& scenario_path);

  /// Builds a scenario runtime (script runtime, sprite pool, sound resources)
  /// from a loaded package, with no SCX runtime instances active. Returns an
  /// error without mutating anything when construction fails.
  [[nodiscard]] std::expected<std::unique_ptr<ScenarioRuntime>, std::string> prepare_runtime(
      const std::string& scenario_name, const LoadedScenario& loaded);

  /// Atomically installs a loaded mode scenario and its runtime into the
  /// gameplay-mode slot.
  void install_gameplay_mode(
      GameplayMode mode, LoadedScenario loaded, std::unique_ptr<ScenarioRuntime> runtime);

  /// Tears down the gameplay-mode slot completely (runtime first, then data).
  void teardown_gameplay_mode_slot();

  /// Atomically installs a loaded scenario, optional decor and runtime into a
  /// world context entry.
  void install_world_context(WorldSceneContext& context,
      std::uint32_t scene_id,
      std::optional<std::string> decor_path,
      std::string resolved_decor_path,
      std::optional<Omikron::Model3DOData> decor_model,
      const std::string& scenario_path,
      LoadedScenario loaded,
      std::unique_ptr<ScenarioRuntime> runtime,
      WorldSceneResidencyState residency);

  /// Tears down a world context completely (runtime/decor first, then data).
  void teardown_world_context(WorldSceneContext& context);

  /// Returns the best target context for allocation:
  /// 1. The first Free entry (by index).
  /// 2. If none, the first LoadedInactive entry that does not own the
  ///    controlled body (by index).
  /// 3. If none (both are LoadedActive), nullptr.
  [[nodiscard]] WorldSceneContext* allocate_world_context_slot();
};

}  // namespace App

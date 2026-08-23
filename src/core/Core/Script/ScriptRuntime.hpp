#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Core/Audio/AudioTypes.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Script/ScriptOpcode.hpp"
#include "Core/Sprite/SpriteInstance.hpp"

namespace App::Script {

/// Nominal script-frame rate: value-pool durations are expressed in 30 Hz
/// script-frame units, not seconds.
inline constexpr float k_script_frames_per_second{30.0F};
/// Maximum normal script delta: three script frames.
inline constexpr float k_max_script_delta_frames{3.0F};

/// Converts a real-time delta in seconds to 30 Hz script-frame units,
/// clamped to [0, 3] frames. This is the single conversion point; handlers
/// and the scheduler receive script-frame units only.
[[nodiscard]] inline float convert_real_delta_to_script_frames(const float real_delta_seconds) {
  const float unclamped_script_delta_frames{real_delta_seconds * k_script_frames_per_second};
  if (unclamped_script_delta_frames < 0.0F) {
    return 0.0F;
  }
  if (unclamped_script_delta_frames > k_max_script_delta_frames) {
    return k_max_script_delta_frames;
  }
  return unclamped_script_delta_frames;
}

/// Result of one native command dispatch. Independent of the undefined x86
/// `AL` return values of the original (effectively void) handlers.
enum class ScriptCommandStatus : std::uint8_t {
  k_running,    ///< Active; keep servicing on subsequent ticks.
  k_completed,  ///< Reached its execution limit this tick (or via precheck).
  k_paused,     ///< Stopped on an unhandled opcode or structured error.
  k_error,      ///< Structured error; see the pause info.
};

/// Coarse scenario/script execution state for the debugger.
enum class ScriptRunState : std::uint8_t {
  k_running,
  k_user_paused,
  k_paused_on_unhandled,
  k_paused_on_error,
  k_completed,
};

/// Structured pause reason; a first-class runtime state, not a log line.
enum class ScriptPauseReason : std::uint8_t {
  k_none,
  k_unhandled_opcode,
  k_invalid_argument_count,
  k_out_of_range_index,
  k_missing_resource,
  k_invalid_linked_command,
  k_invalid_group,
  k_command_budget_exhausted,
  k_unsupported_subsystem,
  k_invalid_duration,
};

/// Human-readable name of a pause reason (debugger / logs).
[[nodiscard]] const char* pause_reason_name(ScriptPauseReason reason);

/// One argument captured in a pause event, shown in every interpretation.
struct ScriptArgumentView {
  std::uint32_t raw{0};
  std::int32_t as_signed{0};
  std::uint32_t as_unsigned{0};
  float as_float{0.0F};
};

/// Persistent capture of why execution paused. Retains the responsible
/// script/group/command and every argument in raw/int/float form.
struct ScriptPauseInfo {
  std::string scenario_name;
  std::size_t script_index{0};
  std::string script_name;
  std::size_t instance_id{0};
  /// Explicit character owner captured from the instance launch context.
  std::optional<std::int16_t> character_id;
  /// Preserved AREA operand 3 for a character launch (semantics unresolved).
  std::int16_t launch_parameter{0};
  std::size_t current_group_index{0};
  std::size_t chain_position{0};
  bool is_root_command{true};
  std::size_t command_index{0};
  std::size_t file_offset{0};
  std::uint32_t opcode{0};
  std::string opcode_name;
  std::uint32_t value_count{0};
  std::vector<ScriptArgumentView> arguments;
  std::uint32_t execution_limit{0};
  std::uint32_t execution_count{0};
  std::int32_t next_command_index{-1};
  std::uint64_t tick{0};
  ScriptPauseReason reason{ScriptPauseReason::k_none};
  std::string reason_text;
};

/// One command in OpenNomad's mutable execution representation. It never
/// aliases the parsed SCX definition; execution counters are owned here.
struct RuntimeScriptCommand {
  std::uint32_t opcode{0};
  std::uint32_t value_count{0};
  std::uint32_t first_value_index{0};
  std::optional<std::uint32_t> next_linked_command_index;
  std::uint32_t execution_limit{0};
  std::uint32_t initial_execution_count{0};
  std::uint32_t execution_count{0};
  std::size_t source_file_offset{0};
};

/// Typed metadata describing how a mutable script instance was launched.
/// World scripts have no explicit character owner; AREA 0x3B/0x3C launches
/// retain their character ID and otherwise-uninterpreted third operand.
struct ScriptLaunchContext {
  std::optional<std::int16_t> character_id;
  std::int16_t parameter{0};
};

/// One OpenNomad runtime instance produced from a parsed SCX definition, with
/// mutable argument storage, counters and an instance-local sprite remap.
/// Retail Runtime instead executes mutable primary loaded records directly and
/// separately owns clones created by Script_MakeInstance.
struct ScriptInstance {
  std::size_t instance_id{0};
  std::size_t source_script_index{0};
  std::string script_name;
  ScriptLaunchContext launch_context;
  std::vector<Omikron::ScriptValue> value_pool;
  std::vector<RuntimeScriptCommand> root_commands;
  std::vector<RuntimeScriptCommand> linked_commands;
  std::size_t current_group_index{0};
  std::int32_t repeat_limit{0};
  std::uint32_t repeat_index{0};
  std::uint32_t initial_repeat_index{0};
  std::unordered_map<std::uint32_t, Sprite::SpriteHandle> sprite_remap;
  /// Accumulated Runtime script time in native 30 Hz frame units.
  /// PlaySyncSound compares its authored schedule directly against this clock.
  float elapsed_script_frames{0.0F};
  bool completed{false};
  bool paused{false};
  ScriptPauseInfo pause_info;
  /// Single-command step cursor: dispatches the root when true, otherwise
  /// the linked command at step_linked_index (chain position tracked).
  bool step_at_root{true};
  std::uint32_t step_linked_index{0};
  std::size_t step_chain_position{0};
};

/// One bounded trace entry, consumed by the ImGui trace window.
struct CommandTraceEntry {
  std::uint64_t tick{0};
  std::size_t instance_id{0};
  std::size_t group_index{0};
  std::size_t chain_position{0};
  std::uint32_t opcode{0};
  std::string opcode_name;
  std::string status_before;
  std::string status_after;
  std::uint32_t execution_count_before{0};
  std::uint32_t execution_count_after{0};
  std::string mutated_arguments;
};

/// Internal result of one handler: the status plus an optional structured
/// pause reason. Handlers never log or mutate scenario state themselves.
struct HandlerResult {
  ScriptCommandStatus status{ScriptCommandStatus::k_completed};
  ScriptPauseReason pause_reason{ScriptPauseReason::k_none};
  std::string reason_text;
};

/// Typed request for Runtime's Script_SelectRelativeBodyAnimation operation.
/// All coordinates and progress values remain in Runtime-native units.
struct RelativeBodyAnimationRequest {
  std::int16_t character_id{0};
  std::string_view object_binding;
  std::uint32_t animation_index{0};
  float previous_progress{0.0F};
  float current_progress{1.0F};
  std::array<float, 3> body_animation_vector{};
  std::uint32_t path_index{0};
  std::uint32_t subpath_index{0};
  std::array<float, 3> authored_offset{};
  bool first_tick{true};
  std::uint32_t execution_count{0};
  std::uint32_t execution_limit{0};
};

struct RelativeBodyAnimationResult {
  std::uint32_t max_frame_index{0};
};

/// Abstract world service connecting script handlers to the existing sprite
/// renderer/runtime. Implemented by ModelViewerScene; faked in tests.
class ScriptWorld {
 public:
  virtual ~ScriptWorld() = default;

  /// Resolves a scenario sprite-table index to a live sprite instance,
  /// creating and attaching it on first use (instance-local remap source).
  [[nodiscard]] virtual std::expected<Sprite::SpriteHandle, std::string> ensure_sprite(
      std::uint32_t source_sprite_index) = 0;

  /// Runtime SetSpriteFrame: frame_index < the object's frame count.
  [[nodiscard]] virtual std::expected<void, std::string> set_sprite_frame(
      Sprite::SpriteHandle handle, std::uint16_t frame_index) = 0;

  virtual void set_sprite_position(Sprite::SpriteHandle handle, std::array<float, 3> position) = 0;
  virtual void set_sprite_type(Sprite::SpriteHandle handle, std::uint16_t type) = 0;
  virtual void set_sprite_scale_x(Sprite::SpriteHandle handle, float scale_x) = 0;
  virtual void set_sprite_scale_y(Sprite::SpriteHandle handle, float scale_y) = 0;
  virtual void set_sprite_rotation(Sprite::SpriteHandle handle, float rotation) = 0;

  /// Resolves an XYZ-pointer index to a world position. An active entry uses
  /// its stored floats; a valid-but-inactive entry (and, in the POC, an
  /// as-yet-unparsed pool) falls back to the current model/3DO transform
  /// translation. Out-of-range indices are a structured error.
  [[nodiscard]] virtual std::expected<std::array<float, 3>, std::string> resolve_position(
      std::uint32_t xyz_index) = 0;

  /// Resolves a scenario sound-table index to its runtime sound descriptor
  /// (resource handle, name, hID and load status).
  [[nodiscard]] virtual std::expected<Audio::SoundDescriptor, std::string> resolve_sound(
      std::uint32_t sound_table_index) = 0;

  /// Resolves a scenario object index to a stable audio owner token, or the
  /// null owner for -1.
  [[nodiscard]] virtual std::expected<Audio::AudioOwnerToken, std::string> resolve_audio_owner(
      std::int32_t object_index) = 0;

  /// Current world position of an object owner (emitter updates).
  [[nodiscard]] virtual std::expected<Audio::Vec3, std::string> resolve_owner_position(
      const Audio::AudioOwnerToken& owner) = 0;

  /// Queues a typed play request through the audio subsystem.
  [[nodiscard]] virtual std::expected<Audio::VoiceHandle, std::string> play_sound(
      const Audio::SoundPlayRequest& request) = 0;

  /// Stops the first active voice matching (soundId, owner).
  virtual void stop_sound(Audio::SoundResourceId sound, const Audio::AudioOwnerToken& owner) = 0;

  /// Compact audio context for the script debugger's audio diagnostics.
  [[nodiscard]] virtual Audio::AudioContextInfo audio_context() const = 0;

  /// Resolves cached SCX resources and applies one relative body-animation
  /// interval to the explicitly character-bound model instance.
  [[nodiscard]] virtual std::expected<RelativeBodyAnimationResult, std::string>
  select_relative_body_animation(const RelativeBodyAnimationRequest& request) {
    (void)request;
    return std::expected<RelativeBodyAnimationResult, std::string>{
        std::unexpect, "relative body animation is unavailable in this world"};
  }

  /// Clears instance-local body-animation playback state during script reset.
  virtual void reset_body_animation(std::int16_t character_id) {
    (void)character_id;
  }

  [[nodiscard]] virtual std::string_view scenario_name() const = 0;
};

/// Safe modern SCX execution model: parsed definitions produce mutable
/// ScriptInstance objects serviced by a bounded per-frame scheduler. Retail
/// Runtime ownership is different: mutable primary loaded records execute
/// directly, alongside a separate Script_MakeInstance clone pool. See
/// docs/reverse-engineering/script-runtime.md.
class ScriptRuntime {
 public:
  ScriptRuntime() = default;
  ~ScriptRuntime() = default;
  ScriptRuntime(const ScriptRuntime&) = delete;
  ScriptRuntime(ScriptRuntime&&) = delete;
  ScriptRuntime& operator=(const ScriptRuntime&) = delete;
  ScriptRuntime& operator=(ScriptRuntime&&) = delete;

  /// Creates a runtime over parsed SCX data. The runtime does not own `scx`
  /// or `world`; both must outlive it.
  static std::expected<std::unique_ptr<ScriptRuntime>, std::string> create(
      const Omikron::ScxData& scx, ScriptWorld& world, std::string scenario_name);

  /// Creates OpenNomad's mutable execution representation from one parsed SCX
  /// definition and reinitialises mutable command arguments. This is not a
  /// literal implementation of retail Script_MakeInstance. Returns the ID.
  [[nodiscard]] std::expected<std::size_t, std::string> create_instance(
      std::size_t source_script_index);

  /// Creates a runtime instance with explicit launch metadata. The context is
  /// retained for ownership/debugging and does not alter the SCX value pool.
  [[nodiscard]] std::expected<std::size_t, std::string> create_instance(
      std::size_t source_script_index, ScriptLaunchContext launch_context);

  /// Advances every non-completed instance by one scheduler tick with the
  /// real application delta in seconds (converted to script frames here).
  /// Does nothing while paused or completed.
  void tick(float real_delta_seconds);

  // --- Controls (debugger) ---
  void set_user_paused(bool paused);
  [[nodiscard]] bool user_paused() const;
  /// One deterministic script tick with a fixed delta in 30 Hz script-frame
  /// units, bypassing the pause gate. Restores the prior state unless a new
  /// pause occurred.
  void step_tick(float script_delta_frames);
  /// Dispatches exactly one command of the current instance, bypassing the
  /// pause gate.
  void step_command();
  /// Resets one instance to its freshly-created state.
  [[nodiscard]] std::expected<void, std::string> reset_instance(std::size_t instance_id);
  /// Resets every instance and clears the pause state.
  void reset_all();

  // --- Inspection (debugger) ---
  [[nodiscard]] ScriptRunState run_state() const;
  [[nodiscard]] const std::vector<ScriptInstance>& instances() const;
  [[nodiscard]] std::vector<ScriptInstance>& instances();
  [[nodiscard]] const ScriptInstance* instance(std::size_t instance_id) const;
  [[nodiscard]] const ScriptPauseInfo& pause_info() const;
  [[nodiscard]] const std::deque<CommandTraceEntry>& trace() const;
  [[nodiscard]] bool trace_enabled() const;
  void set_trace_enabled(bool enabled);
  [[nodiscard]] std::uint64_t tick_count() const;
  [[nodiscard]] const Omikron::ScxData& scx() const;
  /// The most recent real delta in seconds (0 before the first tick).
  [[nodiscard]] float last_real_delta_seconds() const;
  /// The most recent effective script delta in 30 Hz frames.
  [[nodiscard]] float last_script_delta_frames() const;
  /// True when the most recent script delta was clamped to the three-frame
  /// maximum.
  [[nodiscard]] bool last_script_delta_clamped() const;

 private:
  /// Per-tick scheduler advance, fed with 30 Hz script-frame units.
  void advance(float script_delta_frames);

  /// Services one command group of one instance; returns false when the
  /// instance (and therefore the scenario) paused.
  bool advance_instance(ScriptInstance& instance, float script_delta_frames, std::size_t& budget);

  /// Dispatches one command through the opcode registry and its handler.
  ScriptCommandStatus dispatch_command(ScriptInstance& instance,
      RuntimeScriptCommand& command,
      float script_delta_frames,
      std::size_t group_index,
      std::size_t chain_position,
      std::size_t linked_index,
      bool is_root);

  /// Pauses the scenario with full command context.
  void pause(ScriptInstance& instance,
      const RuntimeScriptCommand& command,
      std::size_t group_index,
      std::size_t chain_position,
      std::size_t linked_index,
      bool is_root,
      ScriptPauseReason reason,
      std::string reason_text);

  /// OpenNomad execution-limit eligibility helper. Retail performs this check
  /// inside Script_PlayScript around 0x0044C9D2; 0x004A0260 is
  /// Script_MakeInstance.
  [[nodiscard]] static bool precheck_completed(const RuntimeScriptCommand& command);

  /// Explicit exhaustion predicate: an unlimited (0xFFFFFFFF) limit is never
  /// exhausted; a finite limit is exhausted once the count reaches it.
  [[nodiscard]] static bool is_command_exhausted(const RuntimeScriptCommand& command);

  /// Finishes one whole pass, either completing the instance or restarting
  /// it at group zero with command-local mutable state reinitialised.
  static void finish_script_pass(ScriptInstance& instance);

  /// Restores debugger-visible runtime state to the serialized initial state.
  void reset_instance_to_initial_state(ScriptInstance& instance);

  /// Handler implementations.
  HandlerResult handle_set_sprite_type(ScriptInstance& instance, RuntimeScriptCommand& command);
  HandlerResult handle_set_sprite_frame(ScriptInstance& instance, RuntimeScriptCommand& command);
  HandlerResult handle_interpolated(
      ScriptInstance& instance, RuntimeScriptCommand& command, float script_delta_frames);
  HandlerResult handle_display_3d_sprite(
      ScriptInstance& instance, RuntimeScriptCommand& command, float script_delta_frames);
  static HandlerResult handle_wait(
      ScriptInstance& instance, RuntimeScriptCommand& command, float script_delta_frames);
  HandlerResult handle_play_sound(ScriptInstance& instance, RuntimeScriptCommand& command);
  HandlerResult handle_play_sync_sound(ScriptInstance& instance, RuntimeScriptCommand& command);
  HandlerResult handle_stop_sound(ScriptInstance& instance, RuntimeScriptCommand& command);
  HandlerResult handle_select_relative_body_animation(
      ScriptInstance& instance, RuntimeScriptCommand& command, float script_delta_frames);

  /// Stops the matching (soundId, owner) voice for every started audio
  /// command of an instance (replay/reset hook, called before the pool reset).
  void reset_audio_commands(ScriptInstance& instance);

  /// Resolves (and caches) the runtime sprite of a command's argument 0.
  [[nodiscard]] std::expected<Sprite::SpriteHandle, std::string> resolve_sprite(
      ScriptInstance& instance, const RuntimeScriptCommand& command);

  /// Applies one interpolated value to a sprite (0 = scale X, 1 = scale Y,
  /// 2 = roll). `is_completion` selects the Runtime-faithful roll call
  /// boundary (raw target without the degrees-to-radians conversion).
  void apply_interpolated_value(
      Sprite::SpriteHandle handle, float value, std::uint16_t kind, bool is_completion);

  /// Appends one trace entry, bounded.
  void append_trace(CommandTraceEntry entry);

  const Omikron::ScxData* m_scx{nullptr};
  ScriptWorld* m_world{nullptr};
  std::string m_scenario_name;
  std::vector<ScriptInstance> m_instances;
  std::size_t m_next_instance_id{1};
  std::uint64_t m_tick{0};
  ScriptRunState m_run_state{ScriptRunState::k_running};
  bool m_user_paused{false};
  bool m_trace_enabled{false};
  std::deque<CommandTraceEntry> m_trace;
  /// Most recent real and effective script deltas (debugger inspection).
  float m_last_real_delta_seconds{0.0F};
  float m_last_script_delta_frames{0.0F};
  bool m_last_script_delta_clamped{false};
  /// Bound on the trace ring buffer.
  static constexpr std::size_t k_trace_capacity{256};
  /// Bound on command dispatches per script tick (malformed-link safety).
  static constexpr std::size_t k_command_budget_per_tick{4096};
};

}  // namespace App::Script

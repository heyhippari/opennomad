#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Audio/AudioTypes.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Script/AreaScriptOpcode.hpp"

namespace App::Script {

/// Coarse execution state of one area-script context. Mirrors the recovered
/// scenario-context lifecycle: a context is created, an event/state is
/// queued, activation is explicit, and the first tick executes the queued
/// prefix.
enum class AreaScriptState : std::uint8_t {
  k_ready,               ///< Created; waiting for an event to be queued and run.
  k_running,             ///< Executing instructions.
  k_waiting,             ///< Waiting on a native callback (e.g. an open interface).
  k_paused_unsupported,  ///< Stopped on an opcode outside the compatibility set.
  k_completed,           ///< Ran past the end of the bytecode.
  k_failed,              ///< Structured decode/execution error.
};

/// AREA opcodes 0x3B and 0x3C use the same explicit-character script request,
/// differing only in whether the parent AREA context tracks completion.
enum class AreaCharacterScriptLaunchMode : std::uint8_t {
  k_fire_and_forget,  ///< 0x3B: request the script and continue.
  k_tracked,          ///< 0x3C: request the script and block in Runtime state 4.
};

/// Explicit-character SCX-script launch requested by AREA opcodes 0x3B/0x3C.
///
/// This is intentionally distinct from AreaScxScriptRequest (opcode 0x39):
/// the first operand is a CHARACTERS/table-0 ID, not an SCX script ID.
struct AreaCharacterScriptRequest {
  std::int16_t character_id{0};
  std::uint16_t script_id{0};
  std::int16_t parameter{0};
  AreaCharacterScriptLaunchMode mode{AreaCharacterScriptLaunchMode::k_fire_and_forget};
};

/// Typed reason an area context is waiting. The recovered legacy wait-state
/// value is preserved separately for diagnostics.
enum class AreaWaitKind : std::uint8_t {
  k_none,
  k_interface,
  k_scx_script,
  k_character_script,
  k_camera,
};

/// Typed wait state for the currently suspended AREA context.
struct AreaWaitState {
  AreaWaitKind kind{AreaWaitKind::k_none};
  /// Recovered Runtime context state while blocked.
  std::uint16_t runtime_state{0};
  std::optional<App::InterfaceHandle> interface;
  /// Destination START/global variable for an interface result (opcode 0x46).
  std::optional<std::uint16_t> interface_result_variable;
  /// SCX ScriptRuntime instance spawned by opcode 0x39.
  std::optional<std::size_t> scx_script_instance;
  /// Explicit-character request blocked by opcode 0x3C.
  ///
  /// Phase 1 tracks the logical request. Phase 3 will additionally associate
  /// it with the concrete ScriptRuntime instance created for that character.
  std::optional<AreaCharacterScriptRequest> character_script;
  /// Remaining 30 Hz scenario units for the timed camera wait used by 0x60.
  float remaining_scenario_frames{0.0F};
};

/// AREA opcode 0x39 request. Runtime resolves operand 0 against the active
/// SCX template's +0x1A script ID; the other operands are preserved until
/// their semantics are fully recovered.
struct AreaScxScriptRequest {
  std::uint16_t script_id{0};
  std::int16_t operand_b{0};
  std::int16_t operand_c{0};
};

/// Recovered AREA opcode 0x4E character activation request.
///
/// Runtime resolves character_id through the active AREA's table 0. For a
/// resident runtime character it reactivates the character and marks its AREA
/// presence bit. When apply_area_transform is true it also applies the
/// position/orientation serialized in that table-0 record.
///
/// character_id == -1 is Runtime's special current-character path; it clears
/// model flag bit 0x2 instead of looking up a table-0 record.
///
/// OpenNomad does not yet own a runtime character subsystem, so the AREA VM
/// preserves this operation as a typed request rather than fabricating a
/// character implementation here.
struct AreaCharacterActivationRequest {
  std::int16_t character_id{0};
  bool apply_area_transform{false};
};

/// Recovered camera operation emitted by opcodes 0x5F/0x60.
struct AreaCameraRequest {
  std::uint16_t camera_id{0};
  std::int16_t duration_units{0};
  std::int16_t flags{0};
  bool wait_for_completion{false};
};

/// Shared presentation request used by 0x76 (mode 1) and 0x77 (mode 2).
struct AreaPresentationRequest {
  std::uint8_t mode{0};
  std::uint32_t color{0};
  std::int16_t operand_b{0};
  std::int16_t operand_c{0};
};

/// One decoded instruction boundary and its operands (sign-extended), kept
/// for the trace window.
struct AreaInstructionTrace {
  std::size_t offset{0};
  std::uint32_t opcode{0};
  std::string opcode_name;
  std::vector<std::int32_t> operands;
  std::string effect;
};

/// Persistent capture of why an area context paused or failed.
struct AreaPauseInfo {
  std::size_t offset{0};
  std::uint32_t opcode{0};
  std::string opcode_name;
  std::string reason_text;
  /// Up to eight bytes at and after the instruction, as two-digit hex.
  std::string nearby_bytes;
};

/// Runtime-style AREA bytecode interpreter over immutable bytes. Loading or
/// constructing a context never executes it: an event/state must be queued,
/// the context explicitly activated, and run() called on a tick.
class AreaScriptRuntime {
 public:
  /// Sink for the interface-open opcode (0x46): the full open request. The
  /// sink opens the interface and returns the opened instance handle, or an
  /// error. Wired by the startup controller to the UI dispatch.
  using InterfaceSink = std::function<std::expected<App::InterfaceHandle, std::string>(
      const App::InterfaceOpenRequest&)>;

  /// Sink for the music opcode (0x67): the typed track request. Wired by the
  /// startup controller to the audio system. Fire-and-forget (void); failures
  /// are logged by the sink and never stop the script.
  using MusicSink = std::function<void(const Audio::MusicTrackRequest& request)>;

  /// Bridge from compact AREA bytecode to the active world's SCX ScriptRuntime.
  /// Returns the newly-created ScriptRuntime instance ID.
  using ScxScriptSink =
      std::function<std::expected<std::size_t, std::string>(const AreaScxScriptRequest&)>;

  /// Bridge for explicit-character script requests from AREA 0x3B/0x3C.
  ///
  /// Phase 1 requires the bridge to resolve/validate the character but does
  /// not yet require it to construct the concrete SCX ScriptRuntime instance.
  using CharacterScriptSink =
      std::function<std::expected<void, std::string>(const AreaCharacterScriptRequest&)>;

  /// Presentation bridge for 0x5F/0x60. The VM still owns AREA wait/yield
  /// semantics; the sink receives each command exactly once for rendering.
  using CameraSink = std::function<void(const AreaCameraRequest&)>;

  /// Presentation bridge for 0x76/0x77. The VM owns opcode/yield semantics;
  /// the sink receives each presentation request exactly once.
  using PresentationSink = std::function<void(const AreaPresentationRequest&)>;

  /// Sink invoked before each instruction executes, with the decoded opcode
  /// and operands. Used to emit ordered per-instruction startup trace events.
  using InstructionSink =
      std::function<void(std::uint32_t opcode, const std::vector<std::int32_t>& operands)>;

  explicit AreaScriptRuntime(std::span<const std::byte> script_bytes);

  AreaScriptRuntime(const AreaScriptRuntime&) = delete;
  AreaScriptRuntime(AreaScriptRuntime&&) = delete;
  AreaScriptRuntime& operator=(const AreaScriptRuntime&) = delete;
  AreaScriptRuntime& operator=(AreaScriptRuntime&&) = delete;
  ~AreaScriptRuntime() = default;

  /// Queues one event/state (the recovered "queued event"). Does not execute.
  void queue_event(std::uint16_t event);

  /// Marks the context as the active scenario context. Idempotent.
  void activate();

  /// Executes queued work until the context waits, pauses, completes, fails,
  /// or the per-call instruction budget is exhausted. Returns the new state.
  [[nodiscard]] AreaScriptState run(float real_delta_seconds = 0.0F);

  /// Wires the interface-open sink (opcode 0x46).
  void set_interface_sink(InterfaceSink sink);

  /// Wires the music sink (opcode 0x67).
  void set_music_sink(MusicSink sink);

  /// Wires AREA opcode 0x39 to the active world's SCX ScriptRuntime.
  void set_scx_script_sink(ScxScriptSink sink);

  /// Wires AREA opcodes 0x3B/0x3C to explicit-character script handling.
  void set_character_script_sink(CharacterScriptSink sink);

  /// Wires AREA camera opcodes to the world presentation mailbox.
  void set_camera_sink(CameraSink sink);

  /// Wires AREA presentation opcodes to the world presentation mailbox.
  void set_presentation_sink(PresentationSink sink);

  /// Wires the pre-execution instruction sink (per-instruction diagnostics).
  void set_instruction_sink(InstructionSink sink);

  /// Completes the interface wait this context is suspended on. Returns an
  /// error when the script is not waiting, is waiting on a non-interface
  /// condition, or the completion handle does not match the stored handle.
  /// On success the script resumes at the instruction after opcode 0x46.
  [[nodiscard]] std::expected<void, std::string> complete_interface_wait(
      const App::InterfaceCompletion& completion);

  /// Completes the SCX-script wait created by opcode 0x39. The instance ID
  /// must match the one returned by the bridge sink.
  [[nodiscard]] std::expected<void, std::string> complete_scx_script_wait(std::size_t instance_id);

  /// Resumes a tracked explicit-character script wait.
  ///
  /// Phase 1 matches the logical request because concrete ScriptRuntime
  /// instance ownership is deliberately deferred to Phase 3. Production
  /// startup does not call this yet.
  [[nodiscard]] std::expected<void, std::string> complete_character_script_wait(
      std::int16_t character_id,
      std::uint16_t script_id,
      std::int16_t parameter);

  [[nodiscard]] AreaScriptState state() const {
    return m_state;
  }

  [[nodiscard]] bool active() const {
    return m_active;
  }

  /// The recovered legacy wait state (6 for interfaces); meaningful only
  /// while Waiting. Opcode 0x3C uses recovered state 4.
  [[nodiscard]] std::uint16_t wait_state() const {
    return m_wait_state;
  }
  
  /// Runtime-facing numeric scenario-context state.
  ///
  /// This deliberately exposes only states whose meaning is recovered:
  /// Running maps to Runtime state 1; a typed wait exposes its recovered
  /// state. Other OpenNomad lifecycle states have no asserted Runtime numeric
  /// equivalent and report 0.
  [[nodiscard]] std::uint16_t runtime_state() const {
    if (m_state == AreaScriptState::k_running) {
      return 1;
    }
    if (m_state == AreaScriptState::k_waiting) {
      return m_wait.runtime_state;
    }
    return 0;
  }

  /// The typed wait state.
  [[nodiscard]] const AreaWaitState& wait_info() const {
    return m_wait;
  }
  /// The completion result delivered by the last matching completion, or
  /// nullopt before one is delivered.
  [[nodiscard]] std::optional<std::int16_t> completion_result() const {
    return m_completion_result;
  }
  [[nodiscard]] std::size_t instruction_pointer() const {
    return m_ip;
  }

  /// Value of a START/global variable set by opcodes 0x0D/0x0E, or nullopt.
  [[nodiscard]] std::optional<std::int32_t> variable(std::uint16_t id) const;
  /// All START/global variables set by opcodes 0x0D/0x0E (diagnostics).
  [[nodiscard]] const std::unordered_map<std::uint16_t, std::int32_t>& variables() const {
    return m_variables;
  }

  [[nodiscard]] std::size_t evaluation_stack_depth() const {
    return m_evaluation_stack.size();
  }
  [[nodiscard]] const std::optional<AreaCharacterActivationRequest>&
  last_character_activation_request() const {
    return m_last_character_activation_request;
  }
  [[nodiscard]] const std::optional<AreaCharacterScriptRequest>&
  last_character_script_request() const {
    return m_last_character_script_request;
  }
  [[nodiscard]] const std::optional<AreaCameraRequest>& last_camera_request() const {
    return m_last_camera_request;
  }
  [[nodiscard]] const std::optional<AreaPresentationRequest>& last_presentation_request() const {
    return m_last_presentation_request;
  }
  /// True after 0x84 and false after 0x85. Rendering the recovered 60-unit
  /// transition remains a presentation-layer responsibility.
  [[nodiscard]] bool cinematic_letterbox_requested() const {
    return m_cinematic_letterbox_requested;
  }

  [[nodiscard]] const AreaPauseInfo& pause_info() const {
    return m_pause_info;
  }
  [[nodiscard]] const std::deque<AreaInstructionTrace>& trace() const {
    return m_trace;
  }

  /// Number of instructions executed since the context began running.
  [[nodiscard]] std::size_t executed_instruction_count() const {
    return m_executed_instruction_count;
  }

 private:
  /// Decodes and executes one instruction, mutating state and the
  /// instruction pointer. Leaves the context Running unless it waits, pauses,
  /// fails, or the caller observes completion at the loop boundary.
  void execute_instruction();

  /// Appends one bounded trace entry.
  void append_trace(AreaInstructionTrace entry);

  /// Formats up to eight bytes at `offset` as two-digit hex.
  [[nodiscard]] std::string nearby_bytes_hex(std::size_t offset) const;

  /// Pops one value from Runtime's compact-VM evaluation stack.
  [[nodiscard]] std::expected<std::int32_t, std::string> pop_evaluation_value();

  /// Resolves a signed relative jump from the byte immediately following the
  /// displacement operand.
  [[nodiscard]] std::expected<std::size_t, std::string> relative_target(
      std::size_t base, std::int32_t displacement) const;

  std::span<const std::byte> m_script;
  std::deque<std::uint16_t> m_queued_events;
  bool m_active{false};
  AreaScriptState m_state{AreaScriptState::k_ready};
  std::size_t m_ip{0};
  std::uint16_t m_wait_state{0};
  AreaWaitState m_wait{};
  std::optional<std::int16_t> m_completion_result;
  std::unordered_map<std::uint16_t, std::int32_t> m_variables;
  std::vector<std::int32_t> m_evaluation_stack;
  InterfaceSink m_interface_sink;
  MusicSink m_music_sink;
  ScxScriptSink m_scx_script_sink;
  CharacterScriptSink m_character_script_sink;
  CameraSink m_camera_sink;
  PresentationSink m_presentation_sink;
  InstructionSink m_instruction_sink;
  std::optional<AreaCharacterActivationRequest> m_last_character_activation_request;
  std::optional<AreaCharacterScriptRequest> m_last_character_script_request;
  std::optional<AreaCameraRequest> m_last_camera_request;
  std::optional<AreaPresentationRequest> m_last_presentation_request;
  bool m_cinematic_letterbox_requested{false};
  /// Runtime side-effect handlers set context flag 0x10; the central AREA
  /// dispatcher observes it and yields until the next scenario tick.
  bool m_yield_requested{false};
  AreaPauseInfo m_pause_info;
  std::deque<AreaInstructionTrace> m_trace;
  std::size_t m_executed_instruction_count{0};

  /// Bound on the trace ring buffer.
  static constexpr std::size_t k_trace_capacity{256};
  /// Bound on instructions per run() call (defensive, malformed-bytecode
  /// safety). Budget exhaustion leaves the context Running for the next call.
  static constexpr std::size_t k_instruction_budget{4096};
};

}  // namespace App::Script

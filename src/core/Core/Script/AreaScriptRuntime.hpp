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

/// Coarse execution state of one compact IAM scenario context. The historical
/// class name is retained for compatibility, but both AREA and attached SCENE
/// records use this lifecycle: a context is created, event/state is queued,
/// activation is explicit, and the first tick executes the queued prefix.
enum class AreaScriptState : std::uint8_t {
  k_ready,               ///< Created; waiting for an event to be queued and run.
  k_running,             ///< Executing instructions.
  k_waiting,             ///< Waiting on a native callback (e.g. an open interface).
  k_paused_unsupported,  ///< Stopped on an opcode outside the compatibility set.
  k_completed,           ///< Ran past the end of the bytecode.
  k_failed,              ///< Structured decode/execution error.
};

/// The four compact character-script opcodes differ only in target selection
/// and whether the parent AREA context tracks completion.
enum class AreaCharacterScriptLaunchMode : std::uint8_t {
  k_fire_and_forget,  ///< 0x3B/0x5A: request the script and continue.
  k_tracked,          ///< 0x3C/0x2E: request it and block in Runtime state 4.
};

/// Selects whether a character-script operation has an authored target or
/// reads Runtime's session-level controlled-character slot.
enum class AreaCharacterScriptTarget : std::uint8_t {
  k_explicit,
  k_current,
};

/// SCX-script launch requested by an AREA compact opcode.
///
/// 0x3B/0x3C use an explicit CHARACTERS/table-0 ID. 0x2E/0x5A have no
/// authored character operand and use the session current controlled body.
/// script_id is always a raw serialized u16. camera_duration_units preserves
/// the trailing Scalar16 for the still-unmodeled presentation-controller path;
/// it is not a ScriptLaunchContext parameter.
struct AreaCharacterScriptRequest {
  AreaCharacterScriptTarget target{AreaCharacterScriptTarget::k_explicit};
  std::optional<std::int16_t> character_id;
  std::uint16_t script_id{0};
  std::int16_t camera_duration_units{0};
  AreaCharacterScriptLaunchMode mode{AreaCharacterScriptLaunchMode::k_fire_and_forget};
};

/// Native two-slot AREA transition requested by compact opcode 0x2F.
/// operand_b/operand_c are preserved neutrally until their variants are
/// recovered; the confirmed startup path uses -1 for both.
struct AreaTransitionRequest {
  std::int16_t target_area_id{0};
  std::int16_t operand_b{0};
  std::int16_t operand_c{0};
};

/// Nonblocking release request emitted by opcode 0x30.
struct AreaReleaseRequest {
  std::int16_t area_id{0};
};

/// Nonblocking attached-SCENE replacement request emitted by opcode 0x47.
struct AreaSceneAttachRequest {
  std::int16_t area_id{0};
  std::int16_t scene_id{0};
};

/// Nonblocking named-address placement request emitted by opcode 0x49.
struct AreaAddressPlacementRequest {
  std::int16_t address_id{0};
};

/// Persistent ADDRESS bit mutation requested by compact IAM opcodes 0x57/0x58.
/// The session state, rather than a transient compact context, owns the bits.
struct AreaAddressFlagRequest {
  std::int16_t address_id{0};
  bool enabled{false};
};

/// Persistent object-collection insertion requested by compact IAM opcode
/// 0x32. Both operands retain their authored numeric meaning.
struct AreaPersistentObjectCollectionRequest {
  std::int16_t collection_kind{0};
  std::int16_t object_id{0};
};

/// Current-character selection requested by compact IAM opcode 0x38. The
/// scalar operand is an authored character ID; durable current-body ownership
/// is established by the session sink, not by this compact VM context.
struct AreaCharacterSelectionRequest {
  std::int16_t character_id{0};
};

/// Character removal/presentation request emitted by compact IAM opcode 0x4F.
/// The session sink interprets -1 as the current body's presentation bit.
struct AreaCharacterDeactivationRequest {
  std::int16_t character_id{0};
};

/// Stable identity of one accepted session-level AREA transition.
struct AreaTransitionHandle {
  std::uint64_t generation{0};

  bool operator==(const AreaTransitionHandle&) const = default;
};

/// Typed reason an area context is waiting. The recovered legacy wait-state
/// value is preserved separately for diagnostics.
enum class AreaWaitKind : std::uint8_t {
  k_none,
  k_interface,
  k_scx_script,
  k_character_script,
  k_camera,
  k_area_transition,
};

/// Typed wait state for the currently suspended AREA context.
struct AreaWaitState {
  AreaWaitKind kind{AreaWaitKind::k_none};
  /// Recovered Runtime context state while blocked.
  std::uint16_t runtime_state{0};
  std::optional<App::InterfaceHandle> interface;
  /// Destination START/global variable for an interface result (opcode 0x46).
  std::optional<std::uint16_t> interface_result_variable;
  /// SCX ScriptRuntime instance tracked by opcode 0x3A.
  std::optional<std::size_t> scx_script_instance;
  /// Character-script request blocked by opcode 0x2E or 0x3C (debug metadata).
  std::optional<AreaCharacterScriptRequest> character_script;
  /// Exact ScriptRuntime instance spawned for a tracked 0x2E or 0x3C request.
  std::optional<std::size_t> character_script_instance;
  /// Request and exact coordinator generation blocked by opcode 0x2F.
  std::optional<AreaTransitionRequest> area_transition;
  std::optional<AreaTransitionHandle> area_transition_handle;
  /// Remaining 30 Hz scenario units for the timed camera wait used by 0x60.
  float remaining_scenario_frames{0.0F};
};

/// AREA opcode 0x39/0x3A request. Runtime resolves operand 0 against the active
/// parsed SCX source script's +0x1A ID; the other operands are preserved until
/// their semantics are fully recovered.
struct AreaScxScriptRequest {
  std::uint16_t script_id{0};
  std::int16_t operand_b{0};
  std::int16_t operand_c{0};
};

/// Dialog start requested by AREA opcode 0x3D after resolving its Scalar16
/// operand. Dialog takeover is scheduler state, not an AREA typed wait.
struct AreaDialogRequest {
  std::int16_t dialog_id{0};
};

/// Recovered AREA opcode 0x4E character activation request.
///
/// Runtime resolves character_id through the active AREA's table 0. For a
/// resident runtime character it reactivates the character and marks its AREA
/// presence bit. When apply_area_transform is true it also applies the
/// position/orientation serialized in that table-0 record.
///
/// character_id == -1 is the current-character path; it enables presentation
/// for the selected body instead of looking up a table-0 record.
///
/// The AREA VM preserves this operation as a typed request; the active world
/// owns materialization, model resources and presentation state.
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

/// Presentation intent emitted by operand-less AREA opcodes 0x84/0x85.
struct AreaCinematicLetterboxRequest {
  bool enabled{false};
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

/// Runtime-style compact IAM interpreter over immutable AREA or SCENE bytes.
/// Loading or constructing a context never executes it: an event/state must
/// be queued, the context explicitly activated, and run() called on a tick.
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

  /// Bridge for character-script requests from AREA 0x2E/0x3B/0x3C/0x5A.
  ///
  using CharacterScriptSink =
      std::function<std::expected<std::size_t, std::string>(const AreaCharacterScriptRequest&)>;

  /// Bridge from opcode 0x3D to the session-owned IAM/DIALOG runtime.
  using DialogSink = std::function<std::expected<void, std::string>(const AreaDialogRequest&)>;

  /// Bridge from opcode 0x2F to the session-level two-slot AREA coordinator.
  using AreaTransitionSink = std::function<std::expected<AreaTransitionHandle, std::string>(
      const AreaTransitionRequest&)>;

  /// Session lifecycle bridges for the nonblocking AREA handoff opcodes.
  using AreaReleaseSink = std::function<std::expected<void, std::string>(const AreaReleaseRequest&)>;
  using AreaSceneAttachSink =
      std::function<std::expected<void, std::string>(const AreaSceneAttachRequest&)>;
  using AreaAddressPlacementSink =
      std::function<std::expected<void, std::string>(const AreaAddressPlacementRequest&)>;
  using AddressFlagSink =
      std::function<std::expected<void, std::string>(const AreaAddressFlagRequest&)>;
  using PersistentObjectCollectionSink = std::function<std::expected<void, std::string>(
      const AreaPersistentObjectCollectionRequest&)>;

  /// Bridge from opcode 0x4E to the active world's character runtime.
  using CharacterActivationSink =
      std::function<std::expected<void, std::string>(const AreaCharacterActivationRequest&)>;

  /// Bridge from 0x38 to session-owned current-character selection.
  using CharacterSelectionSink =
      std::function<std::expected<void, std::string>(const AreaCharacterSelectionRequest&)>;

  /// Bridge from 0x4F to the owner world's character lifecycle.
  using CharacterDeactivationSink =
      std::function<std::expected<void, std::string>(const AreaCharacterDeactivationRequest&)>;

  /// Presentation bridge for 0x5F/0x60. The VM still owns AREA wait/yield
  /// semantics; the sink receives each command exactly once for rendering.
  using CameraSink = std::function<void(const AreaCameraRequest&)>;

  /// Presentation bridge for 0x76/0x77. The VM owns opcode/yield semantics;
  /// the sink receives each presentation request exactly once.
  using PresentationSink = std::function<void(const AreaPresentationRequest&)>;

  /// Presentation bridge for the cinematic top/bottom mask (0x84/0x85).
  using CinematicLetterboxSink = std::function<void(const AreaCinematicLetterboxRequest&)>;

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

  /// Wires AREA opcodes 0x39/0x3A to the active world's SCX ScriptRuntime.
  void set_scx_script_sink(ScxScriptSink sink);

  /// Wires AREA character-script opcodes to session/world character handling.
  void set_character_script_sink(CharacterScriptSink sink);

  /// Wires AREA opcode 0x3D to the session dialog runtime.
  void set_dialog_sink(DialogSink sink);

  /// Wires AREA opcode 0x2F to native AREA-transition coordination.
  void set_area_transition_sink(AreaTransitionSink sink);

  /// Wires 0x30, 0x47 and 0x49 to session-owned residency operations.
  void set_area_release_sink(AreaReleaseSink sink);
  void set_area_scene_attach_sink(AreaSceneAttachSink sink);
  void set_area_address_placement_sink(AreaAddressPlacementSink sink);
  /// Wires persistent ADDRESS mutations from opcodes 0x57/0x58.
  void set_address_flag_sink(AddressFlagSink sink);
  /// Wires persistent object-collection insertion from opcode 0x32.
  void set_persistent_object_collection_sink(PersistentObjectCollectionSink sink);

  /// Wires AREA opcode 0x4E to runtime-character activation.
  void set_character_activation_sink(CharacterActivationSink sink);

  /// Wires AREA opcode 0x38 to session-owned current-character selection.
  void set_character_selection_sink(CharacterSelectionSink sink);

  /// Wires AREA opcode 0x4F to current/non-current character lifecycle.
  void set_character_deactivation_sink(CharacterDeactivationSink sink);

  /// Wires AREA camera opcodes to the world presentation mailbox.
  void set_camera_sink(CameraSink sink);

  /// Wires AREA presentation opcodes to the world presentation mailbox.
  void set_presentation_sink(PresentationSink sink);

  /// Wires AREA cinematic-mask opcodes to the world presentation mailbox.
  void set_cinematic_letterbox_sink(CinematicLetterboxSink sink);

  /// Wires the pre-execution instruction sink (per-instruction diagnostics).
  void set_instruction_sink(InstructionSink sink);

  /// Completes the interface wait this context is suspended on. Returns an
  /// error when the script is not waiting, is waiting on a non-interface
  /// condition, or the completion handle does not match the stored handle.
  /// On success the script resumes at the instruction after opcode 0x46.
  [[nodiscard]] std::expected<void, std::string> complete_interface_wait(
      const App::InterfaceCompletion& completion);

  /// Completes the SCX-script wait created by opcode 0x3A. The instance ID
  /// must match the one returned by the bridge sink.
  [[nodiscard]] std::expected<void, std::string> complete_scx_script_wait(std::size_t instance_id);

  /// Resumes a tracked character-script wait. The instance ID must
  /// exactly match the concrete child returned by the launch bridge.
  [[nodiscard]] std::expected<void, std::string> complete_character_script_wait(
      std::size_t instance_id);

  /// Completes the state-10 wait created by opcode 0x2F. Only the exact
  /// transition generation returned by the sink can resume the context.
  [[nodiscard]] std::expected<void, std::string> complete_area_transition(
      AreaTransitionHandle handle);

  [[nodiscard]] AreaScriptState state() const {
    return m_state;
  }

  [[nodiscard]] bool active() const {
    return m_active;
  }

  /// The recovered legacy wait state (6 for interfaces); meaningful only
  /// while Waiting. Opcodes 0x2E/0x3A/0x3C use recovered state 4.
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

  /// Immutable execution span currently used by OpenNomad. Its base is the
  /// startup context's selected event entry, not a process pointer.
  [[nodiscard]] std::span<const std::byte> bytecode() const {
    return m_script;
  }

  /// OpenNomad event currently executing, waiting, or paused. This is
  /// observability state corresponding conceptually (but not layout-wise) to
  /// RuntimeScenarioContext::activeEvent.
  [[nodiscard]] std::optional<std::uint16_t> active_event() const {
    return m_active_event;
  }

  /// Pending OpenNomad events in FIFO order. OpenNomad's queue is not claimed
  /// to reproduce the recovered four-u8 retail storage.
  [[nodiscard]] const std::deque<std::uint16_t>& queued_events() const {
    return m_queued_events;
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
  /// Actual signed dword values in the OpenNomad evaluation stack.
  [[nodiscard]] const std::vector<std::int32_t>& evaluation_stack() const {
    return m_evaluation_stack;
  }
  /// True when the most recent run stopped on the explicit dispatcher-yield
  /// flag. Typed waits remain separately observable through wait_info().
  [[nodiscard]] bool last_run_yielded() const {
    return m_last_run_yielded;
  }
  [[nodiscard]] const std::optional<AreaCharacterActivationRequest>&
  last_character_activation_request() const {
    return m_last_character_activation_request;
  }
  [[nodiscard]] const std::optional<AreaCharacterSelectionRequest>&
  last_character_selection_request() const {
    return m_last_character_selection_request;
  }
  [[nodiscard]] const std::optional<AreaCharacterDeactivationRequest>&
  last_character_deactivation_request() const {
    return m_last_character_deactivation_request;
  }
  [[nodiscard]] const std::optional<AreaCharacterScriptRequest>& last_character_script_request()
      const {
    return m_last_character_script_request;
  }
  [[nodiscard]] const std::optional<AreaDialogRequest>& last_dialog_request() const {
    return m_last_dialog_request;
  }
  [[nodiscard]] const std::optional<AreaTransitionRequest>& last_area_transition_request() const {
    return m_last_area_transition_request;
  }
  [[nodiscard]] const std::optional<AreaReleaseRequest>& last_area_release_request() const {
    return m_last_area_release_request;
  }
  [[nodiscard]] const std::optional<AreaSceneAttachRequest>& last_area_scene_attach_request()
      const {
    return m_last_area_scene_attach_request;
  }
  [[nodiscard]] const std::optional<AreaAddressPlacementRequest>&
  last_area_address_placement_request() const {
    return m_last_area_address_placement_request;
  }
  [[nodiscard]] const std::optional<AreaAddressFlagRequest>& last_address_flag_request() const {
    return m_last_address_flag_request;
  }
  [[nodiscard]] const std::optional<AreaPersistentObjectCollectionRequest>&
  last_persistent_object_collection_request() const {
    return m_last_persistent_object_collection_request;
  }
  [[nodiscard]] const std::optional<AreaCameraRequest>& last_camera_request() const {
    return m_last_camera_request;
  }
  [[nodiscard]] const std::optional<AreaPresentationRequest>& last_presentation_request() const {
    return m_last_presentation_request;
  }
  /// True after 0x84 and false after 0x85. The presentation layer independently
  /// animates the recovered 60-unit transition after receiving the typed intent.
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

  /// Immutable bytecode ownership keeps a context valid while its owning AREA
  /// or attached SCENE is released by one of its own nonblocking opcodes.
  std::vector<std::byte> m_script_storage;
  std::span<const std::byte> m_script;
  std::deque<std::uint16_t> m_queued_events;
  std::optional<std::uint16_t> m_active_event;
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
  DialogSink m_dialog_sink;
  AreaTransitionSink m_area_transition_sink;
  AreaReleaseSink m_area_release_sink;
  AreaSceneAttachSink m_area_scene_attach_sink;
  AreaAddressPlacementSink m_area_address_placement_sink;
  AddressFlagSink m_address_flag_sink;
  PersistentObjectCollectionSink m_persistent_object_collection_sink;
  CharacterActivationSink m_character_activation_sink;
  CharacterSelectionSink m_character_selection_sink;
  CharacterDeactivationSink m_character_deactivation_sink;
  CameraSink m_camera_sink;
  PresentationSink m_presentation_sink;
  CinematicLetterboxSink m_cinematic_letterbox_sink;
  InstructionSink m_instruction_sink;
  std::optional<AreaCharacterActivationRequest> m_last_character_activation_request;
  std::optional<AreaCharacterSelectionRequest> m_last_character_selection_request;
  std::optional<AreaCharacterDeactivationRequest> m_last_character_deactivation_request;
  std::optional<AreaCharacterScriptRequest> m_last_character_script_request;
  std::optional<AreaDialogRequest> m_last_dialog_request;
  std::optional<AreaTransitionRequest> m_last_area_transition_request;
  std::optional<AreaReleaseRequest> m_last_area_release_request;
  std::optional<AreaSceneAttachRequest> m_last_area_scene_attach_request;
  std::optional<AreaAddressPlacementRequest> m_last_area_address_placement_request;
  std::optional<AreaAddressFlagRequest> m_last_address_flag_request;
  std::optional<AreaPersistentObjectCollectionRequest> m_last_persistent_object_collection_request;
  std::optional<AreaCameraRequest> m_last_camera_request;
  std::optional<AreaPresentationRequest> m_last_presentation_request;
  bool m_cinematic_letterbox_requested{false};
  /// Runtime side-effect handlers set context flag 0x10; the central AREA
  /// dispatcher observes it and yields until the next scenario tick.
  bool m_yield_requested{false};
  bool m_last_run_yielded{false};
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

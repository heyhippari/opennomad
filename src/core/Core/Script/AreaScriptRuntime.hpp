#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

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
  /// Sink for the interface-open opcode (0x46): interface id and its two
  /// preserved operands. Wired by the startup controller to the UI dispatch.
  using InterfaceSink = std::function<void(
      std::uint16_t interface_id, std::int16_t operand_b, std::int16_t operand_c)>;

  /// Sink invoked before each instruction executes, with the decoded opcode
  /// and operands. Used to emit ordered per-instruction startup trace events.
  using InstructionSink = std::function<void(
      std::uint32_t opcode, const std::vector<std::int32_t>& operands)>;

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
  [[nodiscard]] AreaScriptState run();

  /// Wires the interface-open sink (opcode 0x46).
  void set_interface_sink(InterfaceSink sink);

  /// Wires the pre-execution instruction sink (per-instruction diagnostics).
  void set_instruction_sink(InstructionSink sink);

  [[nodiscard]] AreaScriptState state() const {
    return m_state;
  }
  [[nodiscard]] bool active() const {
    return m_active;
  }
  /// The wait state set by opcode 0x46 (6); meaningful only while Waiting.
  [[nodiscard]] std::uint16_t wait_state() const {
    return m_wait_state;
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
  /// Observed global/state value (opcode 0x67).
  [[nodiscard]] std::int32_t state_value() const {
    return m_state_value;
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

  std::span<const std::byte> m_script;
  std::deque<std::uint16_t> m_queued_events;
  bool m_active{false};
  AreaScriptState m_state{AreaScriptState::k_ready};
  std::size_t m_ip{0};
  std::uint16_t m_wait_state{0};
  std::int32_t m_state_value{0};
  std::unordered_map<std::uint16_t, std::int32_t> m_variables;
  InterfaceSink m_interface_sink;
  InstructionSink m_instruction_sink;
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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Script/AreaScriptRuntime.hpp"

namespace App {
class ScenarioEngine;
}

namespace App::Debug {

/// Confirmed capacities of the retail Runtime scenario-context architecture.
inline constexpr std::size_t k_retail_area_vm_registry_capacity{32};
inline constexpr std::size_t k_retail_area_vm_queue_capacity{4};
inline constexpr std::size_t k_retail_area_vm_stack_capacity{16};

/// Source/ownership information supplied independently of the VM's mutable
/// execution state.
struct AreaVmContextSourceDebugState {
  std::uint64_t identity{0};
  std::size_t open_nomad_context_index{0};
  std::optional<std::uint8_t> owner_area_slot;
  std::optional<std::uint8_t> retail_registry_slot;
  std::int32_t area_id{0};
  std::optional<std::uint32_t> source_primary_event_offset;
  std::array<std::optional<std::uint32_t>, 3> source_event_entry_offsets{};
  std::size_t open_nomad_execution_base_offset{0};
};

/// Sorted global-variable row retained as typed/raw values for the inspector.
struct AreaVmVariableDebugState {
  std::uint16_t id{0};
  std::int32_t value{0};
};

/// Bounds-safe current-instruction projection.
struct AreaVmInstructionDebugState {
  std::uint32_t opcode{0};
  std::string opcode_name;
  std::vector<std::uint8_t> nearby_bytes;
};

/// UI-independent snapshot of one actual OpenNomad AREA VM context.
struct AreaVmContextDebugState {
  AreaVmContextSourceDebugState source;
  bool active{false};
  Script::AreaScriptState lifecycle_state{Script::AreaScriptState::k_ready};
  std::optional<std::uint16_t> recovered_runtime_state;
  std::optional<std::uint16_t> active_event;
  std::size_t instruction_pointer{0};
  std::size_t bytecode_size{0};
  std::size_t executed_instruction_count{0};
  std::optional<AreaVmInstructionDebugState> current_instruction;
  bool last_run_yielded{false};
  std::vector<std::uint16_t> queued_events;
  std::vector<std::int32_t> evaluation_stack;
  Script::AreaWaitState wait;
  std::optional<Script::AreaCameraRequest> last_camera_request;
  std::vector<AreaVmVariableDebugState> variables;
  Script::AreaPauseInfo pause;
  std::vector<Script::AreaInstructionTrace> trace;
};

/// Collection model consumed by the AREA VM inspector. The retail capacity is
/// architectural evidence; contexts contains only real OpenNomad runtimes.
struct AreaVmRegistryDebugState {
  std::size_t retail_capacity{k_retail_area_vm_registry_capacity};
  std::vector<AreaVmContextDebugState> contexts;
};

/// Maps only recovered numeric Runtime states. Unknown/provisional OpenNomad
/// lifecycle states deliberately return nullopt.
[[nodiscard]] std::optional<std::uint16_t> recovered_area_runtime_state(
    Script::AreaScriptState state, const Script::AreaWaitState& wait);

/// Builds one context snapshot from authoritative runtime and source state.
[[nodiscard]] AreaVmContextDebugState build_area_vm_context_debug_state(
    const Script::AreaScriptRuntime& runtime, AreaVmContextSourceDebugState source);

/// Builds the current registry view. Today this returns zero or one actual
/// startup AREA context; future context providers can append without changing
/// the inspector's list/detail model.
[[nodiscard]] AreaVmRegistryDebugState build_area_vm_registry_debug_state(
    const ScenarioEngine& engine);

}  // namespace App::Debug

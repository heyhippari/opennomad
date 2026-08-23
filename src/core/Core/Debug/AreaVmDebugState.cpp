#include "Core/Debug/AreaVmDebugState.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Omikron/IamArea.hpp"
#include "Core/Scenario/ScenarioEngine.hpp"
#include "Core/Script/AreaScriptOpcode.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"

namespace App::Debug {

std::optional<std::uint16_t> recovered_area_runtime_state(
    const Script::AreaScriptState state, const Script::AreaWaitState& wait) {
  switch (state) {
    case Script::AreaScriptState::k_ready:
    case Script::AreaScriptState::k_completed:
      return 0;
    case Script::AreaScriptState::k_running:
      return 1;
    case Script::AreaScriptState::k_waiting:
      if (wait.runtime_state == 4U || wait.runtime_state == 6U || wait.runtime_state == 7U) {
        return wait.runtime_state;
      }
      return std::nullopt;
    case Script::AreaScriptState::k_paused_unsupported:
    case Script::AreaScriptState::k_failed:
      return std::nullopt;
  }
  return std::nullopt;
}

AreaVmContextDebugState build_area_vm_context_debug_state(
    const Script::AreaScriptRuntime& runtime, AreaVmContextSourceDebugState source) {
  AreaVmContextDebugState result{.source = source,
      .active = runtime.active(),
      .lifecycle_state = runtime.state(),
      .recovered_runtime_state = recovered_area_runtime_state(runtime.state(), runtime.wait_info()),
      .active_event = runtime.active_event(),
      .instruction_pointer = runtime.instruction_pointer(),
      .bytecode_size = runtime.bytecode().size(),
      .executed_instruction_count = runtime.executed_instruction_count(),
      .current_instruction = std::nullopt,
      .last_run_yielded = runtime.last_run_yielded(),
      .queued_events = {runtime.queued_events().begin(), runtime.queued_events().end()},
      .evaluation_stack = runtime.evaluation_stack(),
      .wait = runtime.wait_info(),
      .last_camera_request = runtime.last_camera_request(),
      .variables = {},
      .pause = runtime.pause_info(),
      .trace = {runtime.trace().begin(), runtime.trace().end()}};

  const std::span<const std::byte> bytecode{runtime.bytecode()};
  if (result.instruction_pointer < bytecode.size()) {
    // std::span has no bounds-checked at(); the IP was checked above.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const std::uint32_t opcode{std::to_integer<std::uint8_t>(bytecode[result.instruction_pointer])};
    const Script::AreaOpcodeInfo* const info{Script::area_opcode_info(opcode)};
    AreaVmInstructionDebugState instruction{.opcode = opcode,
        .opcode_name = info == nullptr ? "Unknown" : std::string{info->name},
        .nearby_bytes = {}};
    const std::size_t byte_count{
        std::min<std::size_t>(8U, bytecode.size() - result.instruction_pointer)};
    instruction.nearby_bytes.reserve(byte_count);
    for (std::size_t index{0}; index < byte_count; ++index) {
      // std::span has no bounds-checked at(); byte_count is clamped to the
      // remaining span above.
      instruction.nearby_bytes.push_back(
          // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
          std::to_integer<std::uint8_t>(bytecode[result.instruction_pointer + index]));
    }
    result.current_instruction = std::move(instruction);
  }

  result.variables.reserve(runtime.variables().size());
  for (const auto& [id, value] : runtime.variables()) {
    result.variables.push_back(AreaVmVariableDebugState{.id = id, .value = value});
  }
  std::ranges::sort(result.variables, {}, &AreaVmVariableDebugState::id);
  return result;
}

AreaVmRegistryDebugState build_area_vm_registry_debug_state(const ScenarioEngine& engine) {
  AreaVmRegistryDebugState result;
  const Script::AreaScriptRuntime* const runtime{engine.area_script()};
  const Omikron::IamAreaRecord* const record{engine.area_record()};
  if (runtime == nullptr || record == nullptr) {
    return result;
  }

  const std::int32_t area_id{engine.initial_area_id()};
  const std::uint64_t identity{
      static_cast<std::uint64_t>(static_cast<std::uint32_t>(area_id)) << 32U};
  const std::uint32_t primary_event_offset{record->script_offset()};
  std::array<std::optional<std::uint32_t>, 3> event_entries{};
  // The current startup controller queues Event 1 and constructs its runtime
  // span at the AREA header's primary/default event offset. Event 2/3 source
  // mappings are not modeled and remain absent.
  event_entries.at(0) = primary_event_offset;
  result.contexts.push_back(build_area_vm_context_debug_state(*runtime,
      AreaVmContextSourceDebugState{.identity = identity,
          .open_nomad_context_index = 0,
          .owner_area_slot = std::uint8_t{0},
          .retail_registry_slot = std::nullopt,
          .area_id = area_id,
          .source_primary_event_offset = primary_event_offset,
          .source_event_entry_offsets = event_entries,
          .open_nomad_execution_base_offset = primary_event_offset}));
  return result;
}

}  // namespace App::Debug

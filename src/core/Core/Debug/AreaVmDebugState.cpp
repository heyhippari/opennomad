#include "Core/Debug/AreaVmDebugState.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "Core/Omikron/IamArea.hpp"
#include "Core/Scenario/ScenarioEngine.hpp"
#include "Core/Scenario/ScenarioStartupController.hpp"
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
      if (wait.runtime_state == 4U || wait.runtime_state == 6U || wait.runtime_state == 7U ||
          wait.runtime_state == 10U) {
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

  const std::span<const std::int32_t> variables{runtime.global_variables()};
  const std::size_t inspectable_count{std::min(variables.size(),
      static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1U)};
  result.variables.reserve(inspectable_count);
  for (std::size_t index{0}; index < inspectable_count; ++index) {
    // std::span has no at(); inspectable_count is clamped to its size above.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const std::int32_t value{variables[index]};
    result.variables.push_back(
        AreaVmVariableDebugState{.id = static_cast<std::uint16_t>(index), .value = value});
  }
  return result;
}

AreaVmRegistryDebugState build_area_vm_registry_debug_state(const ScenarioEngine& engine) {
  AreaVmRegistryDebugState result;
  std::size_t context_index{0};

  // Phase 1 replaced the singleton primary AREA VM with one primary context
  // per resident AREA slot. Build diagnostics from those exact owners instead
  // of relabelling whichever context is currently presented as initial AREA 118.
  for (std::size_t owner_slot{0}; owner_slot < 2U; ++owner_slot) {
    const RuntimeAreaSlot* const slot{engine.runtime_area_slot(owner_slot)};
    const Script::AreaScriptRuntime* const runtime{engine.area_script(owner_slot)};
    if (slot == nullptr || runtime == nullptr || !slot->primary.has_value()) {
      continue;
    }

    const Omikron::IamAreaRecord& record{slot->primary.value()};
    const std::int32_t area_id{slot->primary_area_id};
    const std::uint32_t primary_event_offset{record.script_offset()};
    std::array<std::optional<std::uint32_t>, 3> event_entries{};
    // Primary resident contexts currently model the AREA header's default/event
    // 1 entry only. Event 2/3 source mappings remain deliberately absent.
    event_entries.at(0) = primary_event_offset;

    // Stable across active-world switches and distinct for the two resident
    // owners even if malformed/test data were ever to reuse an AREA ID.
    const std::uint64_t identity{
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(area_id)) << 32U) |
        static_cast<std::uint64_t>(owner_slot)};

    result.contexts.push_back(build_area_vm_context_debug_state(*runtime,
        AreaVmContextSourceDebugState{.identity = identity,
            .open_nomad_context_index = context_index++,
            .owner_area_slot = static_cast<std::uint8_t>(owner_slot),
            .retail_registry_slot = std::nullopt,
            .area_id = area_id,
            .source_primary_event_offset = primary_event_offset,
            .source_event_entry_offsets = event_entries,
            .open_nomad_execution_base_offset = primary_event_offset}));
  }
  return result;
}

}  // namespace App::Debug

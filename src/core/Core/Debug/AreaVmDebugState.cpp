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
#include "Core/Omikron/SCX.hpp"
#include "Core/Scenario/ScenarioEngine.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Scenario/ScenarioStartupController.hpp"
#include "Core/Script/AreaScriptOpcode.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "Core/Script/ScriptOpcode.hpp"
#include "Core/Script/ScriptRuntime.hpp"

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
      .tracked_script = std::nullopt,
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

namespace {

std::string command_status(const Script::ScriptInstance& instance,
    const Script::RuntimeScriptCommand& command) {
  if (instance.paused) {
    return "paused";
  }
  if (command.execution_limit != 0xFFFFFFFFU &&
      command.execution_count >= command.execution_limit) {
    return "completed";
  }
  return instance.completed ? "completed" : "running";
}

AreaVmTrackedCommandDebugState snapshot_command(const Script::ScriptInstance& instance,
    const Script::RuntimeScriptCommand& command,
    const std::size_t command_index,
    const bool root) {
  const Script::OpcodeInfo* const info{Script::opcode_info(command.opcode)};
  AreaVmTrackedCommandDebugState result{.root = root,
      .command_index = command_index,
      .opcode = command.opcode,
      .opcode_name = info == nullptr ? "Unknown" : std::string{info->name},
      .execution_count = command.execution_count,
      .execution_limit = command.execution_limit,
      .status = command_status(instance, command),
      .arguments = {}};
  for (std::uint32_t offset{0}; offset < command.value_count; ++offset) {
    const std::size_t value_index{command.first_value_index + offset};
    if (value_index >= instance.value_pool.size()) {
      break;
    }
    const Omikron::ScriptValue& value{instance.value_pool.at(value_index)};
    result.arguments.push_back(AreaVmTrackedCommandDebugState::Argument{.raw = value.raw,
        .as_signed = value.as_signed(),
        .as_unsigned = value.as_unsigned(),
        .as_float = value.as_float()});
  }
  return result;
}

void attach_tracked_script(AreaVmContextDebugState& context,
    const ScenarioEngine& engine,
    const std::size_t owner_slot) {
  std::optional<std::size_t> instance_id;
  if (context.wait.kind == Script::AreaWaitKind::k_character_script) {
    instance_id = context.wait.character_script_instance;
  } else if (context.wait.kind == Script::AreaWaitKind::k_scx_script) {
    instance_id = context.wait.scx_script_instance;
  }
  const RuntimeAreaSlot* const owner{engine.runtime_area_slot(owner_slot)};
  if (!instance_id.has_value() || owner == nullptr) {
    return;
  }
  const ScenarioRuntime* const scenario{engine.manager().world_runtime(owner->world_scene_id)};
  const Script::ScriptRuntime* const runtime{
      scenario == nullptr ? nullptr : scenario->script_runtime()};
  const Script::ScriptInstance* const instance{
      runtime == nullptr ? nullptr : runtime->instance(instance_id.value())};
  if (runtime == nullptr || instance == nullptr) {
    return;
  }
  const Omikron::ScxScript& source{runtime->scx().scripts.at(instance->source_script_index)};
  AreaVmTrackedScriptDebugState tracked{.instance_id = instance->instance_id,
      .source_script_index = instance->source_script_index,
      .source_script_id = source.script_id,
      .source_script_name = source.name,
      .bound_character_id = instance->launch_context.character_id,
      .paused = instance->paused,
      .completed = instance->completed,
      .current_group_index = instance->current_group_index,
      .group_count = instance->root_commands.size(),
      .repeat_index = instance->repeat_index,
      .repeat_limit = instance->repeat_limit,
      .elapsed_script_frames = instance->elapsed_script_frames,
      .root_command_count = instance->root_commands.size(),
      .linked_command_count = instance->linked_commands.size(),
      .active_group_commands = {}};
  if (!instance->root_commands.empty()) {
    const std::size_t group_index{std::min(
        instance->current_group_index, instance->root_commands.size() - 1U)};
    const Script::RuntimeScriptCommand& root{instance->root_commands.at(group_index)};
    tracked.active_group_commands.push_back(snapshot_command(*instance, root, group_index, true));
    std::optional<std::uint32_t> linked{root.next_linked_command_index};
    while (linked.has_value() && linked.value() < instance->linked_commands.size()) {
      const Script::RuntimeScriptCommand& command{instance->linked_commands.at(linked.value())};
      tracked.active_group_commands.push_back(
          snapshot_command(*instance, command, linked.value(), false));
      linked = command.next_linked_command_index;
    }
  }
  context.tracked_script = std::move(tracked);
}

std::uint64_t context_identity(const AreaVmContextSourceType type,
    const std::size_t owner_slot,
    const std::int32_t id) {
  return (static_cast<std::uint64_t>(type) << 60U) |
         (static_cast<std::uint64_t>(owner_slot) << 56U) |
         static_cast<std::uint32_t>(id);
}

}  // namespace

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
    AreaVmContextDebugState context{build_area_vm_context_debug_state(*runtime,
        AreaVmContextSourceDebugState{.identity = 0,
            .open_nomad_context_index = context_index++,
        .source_type = AreaVmContextSourceType::k_area,
            .owner_area_slot = static_cast<std::uint8_t>(owner_slot),
            .retail_registry_slot = std::nullopt,
            .area_id = area_id,
        .scene_id = std::nullopt,
        .zone_id = std::nullopt,
            .source_primary_event_offset = primary_event_offset,
            .source_event_entry_offsets = event_entries,
        .open_nomad_execution_base_offset = primary_event_offset})};
    context.source.identity = context_identity(AreaVmContextSourceType::k_area, owner_slot, area_id);
    attach_tracked_script(context, engine, owner_slot);
    result.contexts.push_back(std::move(context));

    if (slot->scene.has_value() && slot->scene_script.has_value()) {
      const std::uint32_t script_offset{slot->scene->script_offset()};
      AreaVmContextDebugState scene{build_area_vm_context_debug_state(*slot->scene_script,
        AreaVmContextSourceDebugState{
          .identity = context_identity(
            AreaVmContextSourceType::k_scene, owner_slot, slot->scene_id),
          .open_nomad_context_index = context_index++,
          .source_type = AreaVmContextSourceType::k_scene,
          .owner_area_slot = static_cast<std::uint8_t>(owner_slot),
          .retail_registry_slot = std::nullopt,
          .area_id = area_id,
          .scene_id = slot->scene_id,
          .zone_id = std::nullopt,
          .source_primary_event_offset = script_offset,
          .source_event_entry_offsets = {script_offset, std::nullopt, std::nullopt},
          .open_nomad_execution_base_offset = script_offset})};
      attach_tracked_script(scene, engine, owner_slot);
      result.contexts.push_back(std::move(scene));
    }
    }

    for (std::size_t index{0}; index < engine.zone_contact_count(); ++index) {
    const ZoneContactContext* const contact{engine.zone_contact(index)};
    if (contact == nullptr || contact->script == nullptr) {
      continue;
    }
    const std::int32_t zone_identity_id{
      (contact->source == ActiveZoneSource::k_scene ? 1 << 16 : 0) |
      static_cast<std::uint16_t>(contact->zone.zone_id)};
    AreaVmContextDebugState zone{build_area_vm_context_debug_state(*contact->script,
      AreaVmContextSourceDebugState{
        .identity = context_identity(
          AreaVmContextSourceType::k_zone, contact->resident_slot, zone_identity_id),
        .open_nomad_context_index = context_index++,
        .source_type = AreaVmContextSourceType::k_zone,
        .owner_area_slot = static_cast<std::uint8_t>(contact->resident_slot),
        .retail_registry_slot = std::nullopt,
        .area_id = contact->area_id,
        .scene_id = contact->source == ActiveZoneSource::k_scene
                ? std::optional<std::int32_t>{contact->scene_id}
                : std::nullopt,
        .zone_id = contact->zone.zone_id,
        .source_primary_event_offset = contact->zone.event_offsets.at(0),
        .source_event_entry_offsets = {contact->zone.event_offsets.at(0),
          contact->zone.event_offsets.at(1),
          contact->zone.event_offsets.at(2)},
        .open_nomad_execution_base_offset = 0U})};
    attach_tracked_script(zone, engine, contact->resident_slot);
    result.contexts.push_back(std::move(zone));
  }
  return result;
}

}  // namespace App::Debug

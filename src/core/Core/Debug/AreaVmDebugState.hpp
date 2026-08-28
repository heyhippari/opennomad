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

enum class AreaVmContextSourceType : std::uint8_t {
  k_area,
  k_scene,
  k_zone,
};

/// Confirmed capacities of the retail Runtime scenario-context architecture.
inline constexpr std::size_t k_retail_area_vm_registry_capacity{32};
inline constexpr std::size_t k_retail_area_vm_queue_capacity{4};
inline constexpr std::size_t k_retail_area_vm_stack_capacity{16};

/// Source/ownership information supplied independently of the VM's mutable
/// execution state.
struct AreaVmContextSourceDebugState {
  std::uint64_t identity{0};
  std::size_t open_nomad_context_index{0};
  AreaVmContextSourceType source_type{AreaVmContextSourceType::k_area};
  std::optional<std::uint8_t> owner_area_slot;
  std::optional<std::uint8_t> retail_registry_slot;
  std::int32_t area_id{0};
  std::optional<std::int32_t> scene_id;
  std::optional<std::int16_t> zone_id;
  std::optional<std::uint32_t> source_primary_event_offset;
  std::array<std::optional<std::uint32_t>, 3> source_event_entry_offsets{};
  std::size_t open_nomad_execution_base_offset{0};
};

struct AreaVmTrackedCommandDebugState {
  struct Argument {
    std::uint32_t raw{0};
    std::int32_t as_signed{0};
    std::uint32_t as_unsigned{0};
    float as_float{0.0F};
  };

  bool root{false};
  std::size_t command_index{0};
  std::uint32_t opcode{0};
  std::string opcode_name;
  std::uint32_t execution_count{0};
  std::uint32_t execution_limit{0};
  std::string status;
  std::vector<Argument> arguments;
};

struct AreaVmTrackedScriptDebugState {
  std::size_t instance_id{0};
  std::size_t source_script_index{0};
  std::uint16_t source_script_id{0};
  std::string source_script_name;
  std::optional<std::int16_t> bound_character_id;
  bool paused{false};
  bool completed{false};
  std::size_t current_group_index{0};
  std::size_t group_count{0};
  std::uint32_t repeat_index{0};
  std::int32_t repeat_limit{0};
  float elapsed_script_frames{0.0F};
  std::size_t root_command_count{0};
  std::size_t linked_command_count{0};
  std::vector<AreaVmTrackedCommandDebugState> active_group_commands;
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
  std::optional<AreaVmTrackedScriptDebugState> tracked_script;
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

/// Builds the current registry view from every real resident primary AREA
/// context. Resident slot ownership and AREA identity come from the same
/// ScenarioStartupController state that owns each VM; no retail registry slots
/// or Runtime.exe pointers are fabricated.
[[nodiscard]] AreaVmRegistryDebugState build_area_vm_registry_debug_state(
    const ScenarioEngine& engine);

}  // namespace App::Debug

#include "Core/Script/ScriptRuntime.hpp"

#include <fmt/format.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Core/Audio/AudioTypes.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Script/ScriptOpcode.hpp"
#include "Core/Sprite/SpriteInstance.hpp"

namespace App::Script {

namespace {

// Native opcodes confirmed from the reference runtime.
constexpr std::uint32_t K_SET_SPRITE_TYPE{0x0400000CU};
constexpr std::uint32_t K_DISPLAY_3D_SPRITE_ON_PATH{0x0400000DU};
constexpr std::uint32_t K_SCALE_SPRITE_ON_X{0x0400001BU};
constexpr std::uint32_t K_SCALE_SPRITE_ON_Y{0x0400001CU};
constexpr std::uint32_t K_SET_SPRITE_ROLLING{0x0400001DU};
constexpr std::uint32_t K_UNKNOWN_20{0x04000020U};
constexpr std::uint32_t K_DISPLAY_3D_SPRITE{0x04000028U};
constexpr std::uint32_t K_SET_SPRITE_FRAME{0x04000029U};
constexpr std::uint32_t K_SELECT_RELATIVE_BODY_ANIMATION{0x0200002AU};

// Audio opcodes (recovered from Runtime.exe).
constexpr std::uint32_t K_PLAY_SOUND{0x05000014U};
constexpr std::uint32_t K_PLAY_SYNC_SOUND{0x05000015U};
constexpr std::uint32_t K_STOP_SOUND{0x05000016U};
// Control/timing opcode used by GRID script ID 20 ("Wait5sec").
constexpr std::uint32_t K_WAIT{0x06000017U};
/// Mask identifying the audio/music opcode family for the debugger's extra
/// audio diagnostics.
constexpr std::uint32_t K_AUDIO_OPCODE_FAMILY{0x05000000U};
constexpr std::uint32_t K_AUDIO_OPCODE_MASK{0xFF000000U};

// Recovered Runtime-native distances in inches. ScenarioRuntime converts them
// to metres only at audio submission.
constexpr float K_PLAY_SOUND_MIN_DISTANCE{78.0F};
constexpr float K_PLAY_SOUND_MAX_DISTANCE{1170.0F};
constexpr float K_PLAY_SYNC_MIN_DISTANCE{0.0F};
constexpr float K_PLAY_SYNC_MAX_DISTANCE{30.0F};

/// 0xFFFFFFFF marks an unlimited/infinite execution limit.
constexpr std::uint32_t K_INFINITE_EXECUTION_LIMIT{0xFFFFFFFFU};
/// π/180, exactly as used by Runtime's SetSpriteRolling interpolation.
constexpr float K_DEGREES_TO_RADIANS{0.017453292519943295F};
/// Interpolation kinds shared by the scale/roll handlers.
constexpr std::uint16_t K_KIND_SCALE_X{0};
constexpr std::uint16_t K_KIND_SCALE_Y{1};
constexpr std::uint16_t K_KIND_ROLL{2};

constexpr std::array<OpcodeSemanticParam, 6> K_PATH_PARAMS{
    OpcodeSemanticParam{.semantic_type = k_semantic_sprite, .argument_index = 0},
    OpcodeSemanticParam{.semantic_type = k_semantic_unknown_7, .argument_index = 1},
    OpcodeSemanticParam{.semantic_type = k_semantic_unknown_8, .argument_index = 2},
    OpcodeSemanticParam{.semantic_type = k_semantic_none, .argument_index = 4},
    OpcodeSemanticParam{.semantic_type = k_semantic_duration, .argument_index = 5},
    OpcodeSemanticParam{.semantic_type = k_semantic_progress_elapsed, .argument_index = 6},
};
constexpr std::array<OpcodeSemanticParam, 5> K_SCALE_PARAMS{
    OpcodeSemanticParam{.semantic_type = k_semantic_sprite, .argument_index = 0},
    OpcodeSemanticParam{.semantic_type = k_semantic_initial_scale, .argument_index = 1},
    OpcodeSemanticParam{.semantic_type = k_semantic_target_scale, .argument_index = 2},
    OpcodeSemanticParam{.semantic_type = k_semantic_duration, .argument_index = 4},
    OpcodeSemanticParam{.semantic_type = k_semantic_progress_elapsed, .argument_index = 5},
};
constexpr std::array<OpcodeSemanticParam, 5> K_ROLL_PARAMS{
    OpcodeSemanticParam{.semantic_type = k_semantic_sprite, .argument_index = 0},
    OpcodeSemanticParam{.semantic_type = k_semantic_initial_roll, .argument_index = 1},
    OpcodeSemanticParam{.semantic_type = k_semantic_target_roll, .argument_index = 2},
    OpcodeSemanticParam{.semantic_type = k_semantic_duration, .argument_index = 4},
    OpcodeSemanticParam{.semantic_type = k_semantic_progress_elapsed, .argument_index = 5},
};
constexpr std::array<OpcodeSemanticParam, 4> K_DISPLAY_3D_PARAMS{
    OpcodeSemanticParam{.semantic_type = k_semantic_sprite, .argument_index = 0},
    OpcodeSemanticParam{.semantic_type = k_semantic_xyz_pointer, .argument_index = 1},
    OpcodeSemanticParam{.semantic_type = k_semantic_duration, .argument_index = 2},
    OpcodeSemanticParam{.semantic_type = k_semantic_progress_elapsed, .argument_index = 3},
};
constexpr std::array<OpcodeSemanticParam, 2> K_SET_FRAME_PARAMS{
    OpcodeSemanticParam{.semantic_type = k_semantic_sprite, .argument_index = 0},
    OpcodeSemanticParam{.semantic_type = k_semantic_frame, .argument_index = 1},
};
constexpr std::array<OpcodeSemanticParam, 1> K_SET_SPRITE_TYPE_PARAMS{
    OpcodeSemanticParam{.semantic_type = k_semantic_sprite, .argument_index = 0},
};
constexpr std::array<OpcodeSemanticParam, 2> K_WAIT_PARAMS{
    OpcodeSemanticParam{.semantic_type = k_semantic_duration, .argument_index = 0},
    OpcodeSemanticParam{.semantic_type = k_semantic_progress_elapsed, .argument_index = 1},
};

constexpr std::array<OpcodeInfo, 13> K_OPCODE_TABLE{
    OpcodeInfo{.opcode = K_SELECT_RELATIVE_BODY_ANIMATION,
        .name = "Script_SelectRelativeBodyAnimation",
        .expected_argument_count = 12,
        .semantic_params = nullptr,
        .semantic_param_count = 0,
        .owns_sprite = false,
        .support = OpcodeSupport::k_supported,
        .notes = "Anchors a hierarchical 3DA body animation to an SCX 3DP subpath."},
    OpcodeInfo{.opcode = K_SET_SPRITE_TYPE,
        .name = "SetSpriteType",
        .expected_argument_count = 2,
        .semantic_params = K_SET_SPRITE_TYPE_PARAMS.data(),
        .semantic_param_count = K_SET_SPRITE_TYPE_PARAMS.size(),
        .owns_sprite = false,
        .support = OpcodeSupport::k_supported,
        .notes = "Writes the low 16 bits of argument 1 to the runtime sprite's type field."},
    OpcodeInfo{.opcode = K_DISPLAY_3D_SPRITE_ON_PATH,
        .name = "Display3DSpriteOnPath",
        .expected_argument_count = 7,
        .semantic_params = K_PATH_PARAMS.data(),
        .semantic_param_count = K_PATH_PARAMS.size(),
        .owns_sprite = true,
        .support = OpcodeSupport::k_unsupported,
        .notes = "Path animation not implemented; pauses unless the startup path requires it."},
    OpcodeInfo{.opcode = K_SCALE_SPRITE_ON_X,
        .name = "ScaleSpriteOnX",
        .expected_argument_count = 6,
        .semantic_params = K_SCALE_PARAMS.data(),
        .semantic_param_count = K_SCALE_PARAMS.size(),
        .owns_sprite = false,
        .support = OpcodeSupport::k_supported,
        .notes = {}},
    OpcodeInfo{.opcode = K_SCALE_SPRITE_ON_Y,
        .name = "ScaleSpriteOnY",
        .expected_argument_count = 6,
        .semantic_params = K_SCALE_PARAMS.data(),
        .semantic_param_count = K_SCALE_PARAMS.size(),
        .owns_sprite = false,
        .support = OpcodeSupport::k_supported,
        .notes = {}},
    OpcodeInfo{.opcode = K_SET_SPRITE_ROLLING,
        .name = "SetSpriteRolling",
        .expected_argument_count = 6,
        .semantic_params = K_ROLL_PARAMS.data(),
        .semantic_param_count = K_ROLL_PARAMS.size(),
        .owns_sprite = false,
        .support = OpcodeSupport::k_supported,
        .notes = {}},
    OpcodeInfo{.opcode = K_UNKNOWN_20,
        .name = "UnknownOpcode0x04000020",
        .expected_argument_count = 0,
        .semantic_params = nullptr,
        .semantic_param_count = 0,
        .owns_sprite = true,
        .support = OpcodeSupport::k_unsupported,
        .notes = "Known sprite-owning opcode; semantics not yet established."},
    OpcodeInfo{.opcode = K_DISPLAY_3D_SPRITE,
        .name = "Display3DSprite",
        .expected_argument_count = 4,
        .semantic_params = K_DISPLAY_3D_PARAMS.data(),
        .semantic_param_count = K_DISPLAY_3D_PARAMS.size(),
        .owns_sprite = true,
        .support = OpcodeSupport::k_supported,
        .notes = {}},
    OpcodeInfo{.opcode = K_SET_SPRITE_FRAME,
        .name = "SetSpriteFrame",
        .expected_argument_count = 2,
        .semantic_params = K_SET_FRAME_PARAMS.data(),
        .semantic_param_count = K_SET_FRAME_PARAMS.size(),
        .owns_sprite = false,
        .support = OpcodeSupport::k_supported,
        .notes = {}},
    OpcodeInfo{.opcode = K_PLAY_SOUND,
        .name = "PlaySound",
        .expected_argument_count = 4,
        .semantic_params = nullptr,
        .semantic_param_count = 0,
        .owns_sprite = false,
        .support = OpcodeSupport::k_supported,
        .notes = "args: sound index, flags(bit0=loop), started latch, object index/-1."},
    OpcodeInfo{.opcode = K_PLAY_SYNC_SOUND,
        .name = "PlaySyncSound",
        .expected_argument_count = 5,
        .semantic_params = nullptr,
        .semantic_param_count = 0,
        .owns_sprite = false,
        .support = OpcodeSupport::k_supported,
        .notes = "args: sound index, scheduled time, flags(bit0=loop), voice+1 latch, "
                 "object index/-1."},
    OpcodeInfo{.opcode = K_STOP_SOUND,
        .name = "StopSound",
        .expected_argument_count = 2,
        .semantic_params = nullptr,
        .semantic_param_count = 0,
        .owns_sprite = false,
        .support = OpcodeSupport::k_supported,
        .notes = "args: sound index, object index/-1 (null owner)."},
    OpcodeInfo{.opcode = K_WAIT,
        .name = "Wait",
        .expected_argument_count = 2,
        .semantic_params = K_WAIT_PARAMS.data(),
        .semantic_param_count = K_WAIT_PARAMS.size(),
        .owns_sprite = false,
        .support = OpcodeSupport::k_supported,
        .notes = "args: duration in 30 Hz script frames, mutable elapsed progress."},
};

/// Human-readable status label for the trace window.
const char* status_name(const ScriptCommandStatus status) {
  switch (status) {
    case ScriptCommandStatus::k_running:
      return "running";
    case ScriptCommandStatus::k_completed:
      return "completed";
    case ScriptCommandStatus::k_paused:
      return "paused";
    case ScriptCommandStatus::k_error:
      return "error";
    default:
      return "unknown";
  }
}

/// Interpolation kind of a scale/roll opcode (avoids a nested ternary).
std::uint16_t interpolation_kind(const std::uint32_t opcode) {
  if (opcode == K_SCALE_SPRITE_ON_X) {
    return K_KIND_SCALE_X;
  }
  if (opcode == K_SCALE_SPRITE_ON_Y) {
    return K_KIND_SCALE_Y;
  }
  return K_KIND_ROLL;
}

/// Reinitialises the mutable arguments of one command (current = initial,
/// elapsed = 0) as the Runtime reinitializers do.
void reinitialize_command(ScriptInstance& instance, RuntimeScriptCommand& command) {
  const std::uint32_t base{command.first_value_index};
  const std::size_t pool_size{instance.value_pool.size()};
  switch (command.opcode) {
    case K_SELECT_RELATIVE_BODY_ANIMATION:
      if ((base + 3U) < pool_size && command.value_count >= 12U) {
        instance.value_pool.at(base + 2U).set_float(0.0F);
        instance.value_pool.at(base + 3U).set_float(1.0F);
      }
      break;
    case K_SCALE_SPRITE_ON_X:
    case K_SCALE_SPRITE_ON_Y:
    case K_SET_SPRITE_ROLLING:
      if ((base + 5U) < pool_size && command.value_count >= 6U) {
        instance.value_pool.at(base + 3U) = instance.value_pool.at(base + 1U);
        instance.value_pool.at(base + 5U).set_float(0.0F);
      }
      break;
    case K_DISPLAY_3D_SPRITE:
      if ((base + 3U) < pool_size && command.value_count >= 4U) {
        instance.value_pool.at(base + 3U).set_float(0.0F);
      }
      break;
    case K_PLAY_SOUND:
      if ((base + 2U) < pool_size && command.value_count >= 4U) {
        instance.value_pool.at(base + 2U).raw = 0;  // started latch.
      }
      break;
    case K_PLAY_SYNC_SOUND:
      if ((base + 3U) < pool_size && command.value_count >= 5U) {
        instance.value_pool.at(base + 3U).raw = 0;  // voiceIndex+1 latch.
      }
      break;
    case K_WAIT:
      if ((base + 1U) < pool_size && command.value_count >= 2U) {
        instance.value_pool.at(base + 1U).set_float(0.0F);
      }
      break;
    default:
      break;
  }
}

}  // namespace

const char* pause_reason_name(const ScriptPauseReason reason) {
  switch (reason) {
    case ScriptPauseReason::k_none:
      return "none";
    case ScriptPauseReason::k_unhandled_opcode:
      return "unhandled opcode";
    case ScriptPauseReason::k_invalid_argument_count:
      return "invalid argument count";
    case ScriptPauseReason::k_out_of_range_index:
      return "out-of-range index";
    case ScriptPauseReason::k_missing_resource:
      return "missing resource";
    case ScriptPauseReason::k_invalid_linked_command:
      return "invalid linked command";
    case ScriptPauseReason::k_invalid_group:
      return "invalid group";
    case ScriptPauseReason::k_command_budget_exhausted:
      return "command budget exhausted";
    case ScriptPauseReason::k_unsupported_subsystem:
      return "unsupported subsystem";
    case ScriptPauseReason::k_invalid_duration:
      return "invalid duration";
    default:
      return "unknown";
  }
}

const OpcodeInfo* opcode_info(const std::uint32_t opcode) {
  for (const OpcodeInfo& info : K_OPCODE_TABLE) {
    if (info.opcode == opcode) {
      return &info;
    }
  }
  return nullptr;
}

const char* opcode_name(const std::uint32_t opcode) {
  const OpcodeInfo* info{opcode_info(opcode)};
  return info == nullptr ? nullptr : info->name.data();
}

bool opcode_owns_sprite(const std::uint32_t opcode) {
  const OpcodeInfo* info{opcode_info(opcode)};
  return info != nullptr && info->owns_sprite;
}

std::expected<std::unique_ptr<ScriptRuntime>, std::string> ScriptRuntime::create(
    const Omikron::ScxData& scx, ScriptWorld& world, std::string scenario_name) {
  APP_PROFILE_FUNCTION();

  auto runtime{std::make_unique<ScriptRuntime>()};
  runtime->m_scx = &scx;
  runtime->m_world = &world;
  runtime->m_scenario_name = std::move(scenario_name);
  return runtime;
}

std::expected<std::size_t, std::string> ScriptRuntime::create_instance(
    const std::size_t source_script_index) {
  return create_instance(source_script_index, ScriptLaunchContext{});
}

std::expected<std::size_t, std::string> ScriptRuntime::create_instance(
    const std::size_t source_script_index, ScriptLaunchContext launch_context) {
  APP_PROFILE_FUNCTION();

  if (m_scx == nullptr) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "script runtime is not initialised"};
  }
  if (source_script_index >= m_scx->scripts.size()) {
    return std::expected<std::size_t, std::string>{std::unexpect,
        fmt::format("source script index {} out of range ({} scripts)",
            source_script_index,
            m_scx->scripts.size())};
  }

  const Omikron::ScxScript& source{m_scx->scripts.at(source_script_index)};

  ScriptInstance instance;
  instance.instance_id = m_next_instance_id++;
  instance.source_script_index = source_script_index;
  instance.script_name = source.name;
  instance.launch_context = launch_context;
  instance.value_pool = m_scx->shared_values;  // Deep copy: runtime owns mutable args.
  instance.repeat_limit = source.repeat_limit;
  instance.repeat_index = source.initial_repeat_index;
  instance.initial_repeat_index = source.initial_repeat_index;

  instance.root_commands.reserve(source.root_commands.size());
  for (const Omikron::ScxScriptCommand& command : source.root_commands) {
    instance.root_commands.push_back(RuntimeScriptCommand{.opcode = command.opcode,
        .value_count = command.value_count,
        .first_value_index = command.first_value_index,
        .next_linked_command_index = command.next_linked_command_index,
        .execution_limit = command.execution_limit,
        .initial_execution_count = command.initial_execution_count,
        .execution_count = command.initial_execution_count,
        .source_file_offset = command.file_offset});
  }
  instance.linked_commands.reserve(source.linked_commands.size());
  for (const Omikron::ScxScriptCommand& command : source.linked_commands) {
    instance.linked_commands.push_back(RuntimeScriptCommand{.opcode = command.opcode,
        .value_count = command.value_count,
        .first_value_index = command.first_value_index,
        .next_linked_command_index = command.next_linked_command_index,
        .execution_limit = command.execution_limit,
        .initial_execution_count = command.initial_execution_count,
        .execution_count = command.initial_execution_count,
        .source_file_offset = command.file_offset});
  }

  for (RuntimeScriptCommand& command : instance.root_commands) {
    reinitialize_command(instance, command);
  }
  for (RuntimeScriptCommand& command : instance.linked_commands) {
    reinitialize_command(instance, command);
  }

  const std::size_t instance_id{instance.instance_id};
  App::Log::debug(LogCategory::Script,
      "created instance {} from script {} '{}' ({} roots, {} linked)",
      instance_id,
      source_script_index,
      instance.script_name,
      instance.root_commands.size(),
      instance.linked_commands.size());
  // A completed runtime may receive another instance later through AREA's
  // generic SCX launch opcodes; creating it makes the scenario schedulable again.
  if (m_run_state == ScriptRunState::k_completed) {
    m_run_state = ScriptRunState::k_running;
  }
  m_instances.push_back(std::move(instance));
  return instance_id;
}

void ScriptRuntime::tick(const float real_delta_seconds) {
  APP_PROFILE_FUNCTION();

  if (m_run_state != ScriptRunState::k_running) {
    return;  // Paused or completed: no time advance, no command mutation.
  }

  // Centralized real-seconds -> 30 Hz script-frame conversion (clamped to
  // three frames). Handlers and the scheduler only ever see script frames.
  const float unclamped_script_delta_frames{real_delta_seconds * k_script_frames_per_second};
  const float script_delta_frames{convert_real_delta_to_script_frames(real_delta_seconds)};
  m_last_real_delta_seconds = real_delta_seconds;
  m_last_script_delta_frames = script_delta_frames;
  m_last_script_delta_clamped = script_delta_frames != unclamped_script_delta_frames &&
                                unclamped_script_delta_frames > k_max_script_delta_frames;
  advance(script_delta_frames);
}

void ScriptRuntime::advance(const float script_delta_frames) {
  ++m_tick;
  std::size_t budget{k_command_budget_per_tick};

  for (ScriptInstance& instance : m_instances) {
    if (instance.completed || instance.paused) {
      continue;
    }
    // Runtime keeps the script clock in native 30 Hz frame units.
    // PlaySyncSound's authored schedule is expressed in the same units.
    instance.elapsed_script_frames += script_delta_frames;
    if (!advance_instance(instance, script_delta_frames, budget)) {
      return;  // Scenario execution paused partway through the tick.
    }
  }

  bool all_completed{!m_instances.empty()};
  for (const ScriptInstance& instance : m_instances) {
    if (!instance.completed) {
      all_completed = false;
      break;
    }
  }
  if (all_completed) {
    App::Log::info(LogCategory::Script, "scenario execution completed after {} ticks", m_tick);
    m_run_state = ScriptRunState::k_completed;
  }
}

bool ScriptRuntime::advance_instance(
    ScriptInstance& instance, const float script_delta_frames, std::size_t& budget) {
  if (instance.current_group_index >= instance.root_commands.size()) {
    finish_script_pass(instance);
    return true;
  }

  const std::size_t group_index{instance.current_group_index};
  RuntimeScriptCommand& root{instance.root_commands.at(group_index)};
  std::vector<bool> visited(instance.linked_commands.size(), false);
  std::vector<std::uint32_t> chain_indices;

  // Service the root command first.
  if (budget == 0U) {
    pause(instance,
        root,
        group_index,
        0,
        0,
        true,
        ScriptPauseReason::k_command_budget_exhausted,
        "command dispatch budget exhausted before the group root");
    return false;
  }
  --budget;
  const ScriptCommandStatus root_status{
      dispatch_command(instance, root, script_delta_frames, group_index, 0, 0, true)};
  if (root_status == ScriptCommandStatus::k_paused || root_status == ScriptCommandStatus::k_error) {
    return false;
  }

  // Follow the next-command chain through the linked-command array. Every
  // reachable command is serviced once per tick; exhausted commands are
  // skipped by dispatch_command's precheck and do not stop traversal.
  std::optional<std::uint32_t> next{root.next_linked_command_index};
  std::size_t chain_position{1};
  while (next.has_value()) {
    const std::uint32_t index{*next};
    if (index >= instance.linked_commands.size()) {
      pause(instance,
          root,
          group_index,
          chain_position,
          index,
          false,
          ScriptPauseReason::k_invalid_linked_command,
          fmt::format("linked command index {} out of range for {} commands",
              index,
              instance.linked_commands.size()));
      return false;
    }
    if (visited.at(index)) {
      pause(instance,
          root,
          group_index,
          chain_position,
          index,
          false,
          ScriptPauseReason::k_invalid_linked_command,
          fmt::format("cycle detected: linked command {} already visited in this group", index));
      return false;
    }
    visited.at(index) = true;
    chain_indices.push_back(index);

    if (budget == 0U) {
      pause(instance,
          root,
          group_index,
          chain_position,
          index,
          false,
          ScriptPauseReason::k_command_budget_exhausted,
          "command dispatch budget exhausted mid-chain");
      return false;
    }
    --budget;

    RuntimeScriptCommand& command{instance.linked_commands.at(index)};
    const ScriptCommandStatus status{dispatch_command(
        instance, command, script_delta_frames, group_index, chain_position, index, false)};
    if (status == ScriptCommandStatus::k_paused || status == ScriptCommandStatus::k_error) {
      return false;
    }
    next = command.next_linked_command_index;
    ++chain_position;
  }

  // Inferred group-completion rule: advance once when every command in the
  // chain is exhausted by the explicit predicate (unlimited limits never
  // exhaust). Group completion is not inferred from a single handler status.
  bool group_done{is_command_exhausted(root)};
  for (const std::uint32_t index : chain_indices) {
    group_done = group_done && is_command_exhausted(instance.linked_commands.at(index));
  }

  if (group_done) {
    instance.current_group_index += 1;
    App::Log::debug(LogCategory::Script,
        "instance {} advanced to group {}/{}",
        instance.instance_id,
        instance.current_group_index,
        instance.root_commands.size());
    if (instance.current_group_index >= instance.root_commands.size()) {
      finish_script_pass(instance);
    }
  }
  return true;
}

ScriptCommandStatus ScriptRuntime::dispatch_command(ScriptInstance& instance,
    RuntimeScriptCommand& command,
    const float script_delta_frames,
    const std::size_t group_index,
    const std::size_t chain_position,
    const std::size_t linked_index,
    const bool is_root) {
  const OpcodeInfo* info{opcode_info(command.opcode)};
  const std::uint32_t count_before{command.execution_count};

  // Snapshot the argument slice for the trace diff (only while tracing).
  std::vector<std::uint32_t> argument_snapshot;
  if (m_trace_enabled) {
    argument_snapshot.reserve(command.value_count);
    for (std::uint32_t index{0}; index < command.value_count; ++index) {
      const std::size_t pool_index{command.first_value_index + index};
      argument_snapshot.push_back(
          pool_index < instance.value_pool.size() ? instance.value_pool.at(pool_index).raw : 0U);
    }
  }

  HandlerResult result;
  if (info == nullptr) {
    result.status = ScriptCommandStatus::k_paused;
    result.pause_reason = ScriptPauseReason::k_unhandled_opcode;
    result.reason_text = fmt::format("unhandled opcode {:#010x}", command.opcode);
  } else if (info->support != OpcodeSupport::k_supported) {
    result.status = ScriptCommandStatus::k_paused;
    result.pause_reason = ScriptPauseReason::k_unhandled_opcode;
    result.reason_text = fmt::format("opcode {:#010x} ({}) is known but unsupported: {}",
        command.opcode,
        info->name,
        info->notes);
  } else if (info->expected_argument_count != 0U &&
             command.value_count < info->expected_argument_count) {
    result.status = ScriptCommandStatus::k_paused;
    result.pause_reason = ScriptPauseReason::k_invalid_argument_count;
    result.reason_text = fmt::format("{} expects {} arguments, got {}",
        info->name,
        info->expected_argument_count,
        command.value_count);
  } else if (precheck_completed(command)) {
    result.status = ScriptCommandStatus::k_completed;
  } else {
    switch (command.opcode) {
      case K_SELECT_RELATIVE_BODY_ANIMATION:
        result = handle_select_relative_body_animation(instance, command, script_delta_frames);
        break;
      case K_SET_SPRITE_TYPE:
        result = handle_set_sprite_type(instance, command);
        break;
      case K_SET_SPRITE_FRAME:
        result = handle_set_sprite_frame(instance, command);
        break;
      case K_SCALE_SPRITE_ON_X:
      case K_SCALE_SPRITE_ON_Y:
      case K_SET_SPRITE_ROLLING:
        result = handle_interpolated(instance, command, script_delta_frames);
        break;
      case K_DISPLAY_3D_SPRITE:
        result = handle_display_3d_sprite(instance, command, script_delta_frames);
        break;
      case K_PLAY_SOUND:
        result = handle_play_sound(instance, command);
        break;
      case K_PLAY_SYNC_SOUND:
        result = handle_play_sync_sound(instance, command);
        break;
      case K_STOP_SOUND:
        result = handle_stop_sound(instance, command);
        break;
      case K_WAIT:
        result = handle_wait(instance, command, script_delta_frames);
        break;
      default:
        result.status = ScriptCommandStatus::k_paused;
        result.pause_reason = ScriptPauseReason::k_unhandled_opcode;
        result.reason_text = fmt::format("unhandled opcode {:#010x}", command.opcode);
        break;
    }
  }

  if (result.status == ScriptCommandStatus::k_paused ||
      result.status == ScriptCommandStatus::k_error) {
    pause(instance,
        command,
        group_index,
        chain_position,
        linked_index,
        is_root,
        result.pause_reason,
        std::move(result.reason_text));
    return result.status;
  }

  if (m_trace_enabled) {
    CommandTraceEntry entry;
    entry.tick = m_tick;
    entry.instance_id = instance.instance_id;
    entry.group_index = group_index;
    entry.chain_position = chain_position;
    entry.opcode = command.opcode;
    entry.opcode_name =
        info != nullptr ? std::string{info->name} : fmt::format("{:#010x}", command.opcode);
    entry.status_before = count_before == 0U ? "idle" : "active";
    entry.status_after = status_name(result.status);
    entry.execution_count_before = count_before;
    entry.execution_count_after = command.execution_count;

    std::string mutated;
    for (std::uint32_t index{0}; index < command.value_count; ++index) {
      const std::size_t pool_index{command.first_value_index + index};
      const std::uint32_t after{
          pool_index < instance.value_pool.size() ? instance.value_pool.at(pool_index).raw : 0U};
      if (argument_snapshot.at(index) != after) {
        if (!mutated.empty()) {
          mutated += ", ";
        }
        mutated +=
            fmt::format("arg{} {:#010x}->{:#010x}", index, argument_snapshot.at(index), after);
      }
    }
    entry.mutated_arguments = std::move(mutated);
    append_trace(std::move(entry));
  }

  return result.status;
}

HandlerResult ScriptRuntime::handle_select_relative_body_animation(
    ScriptInstance& instance, RuntimeScriptCommand& command, const float script_delta_frames) {
  if (!instance.launch_context.character_id.has_value()) {
    return HandlerResult{.status = ScriptCommandStatus::k_error,
        .pause_reason = ScriptPauseReason::k_missing_resource,
        .reason_text = "Script_SelectRelativeBodyAnimation requires a character-bound instance"};
  }
  const Omikron::ScxScript& source{m_scx->scripts.at(instance.source_script_index)};
  const std::uint32_t base{command.first_value_index};
  const std::uint32_t binding_index{instance.value_pool.at(base).as_unsigned()};
  if (binding_index >= source.binding_table_a.entries.size()) {
    return HandlerResult{.status = ScriptCommandStatus::k_error,
        .pause_reason = ScriptPauseReason::k_out_of_range_index,
        .reason_text = fmt::format("binding table A index {} out of range ({} entries)",
            binding_index,
            source.binding_table_a.entries.size())};
  }

  const float previous{instance.value_pool.at(base + 2U).as_float()};
  const float current{instance.value_pool.at(base + 3U).as_float()};
  const RelativeBodyAnimationRequest request{
      .character_id = instance.launch_context.character_id.value(),
      .object_binding = source.binding_table_a.entries.at(binding_index).name,
      .animation_index = instance.value_pool.at(base + 1U).as_unsigned(),
      .previous_progress = previous,
      .current_progress = current,
      .body_animation_vector = {instance.value_pool.at(base + 4U).as_float(),
          instance.value_pool.at(base + 5U).as_float(),
          instance.value_pool.at(base + 6U).as_float()},
      .path_index = instance.value_pool.at(base + 7U).as_unsigned(),
      .subpath_index = instance.value_pool.at(base + 8U).as_unsigned(),
      .authored_offset = {instance.value_pool.at(base + 9U).as_float(),
          instance.value_pool.at(base + 10U).as_float(),
          instance.value_pool.at(base + 11U).as_float()},
      .first_tick = previous == 0.0F,
      .execution_count = command.execution_count,
      .execution_limit = command.execution_limit};
  auto applied{m_world->select_relative_body_animation(request)};
  if (!applied) {
    if (applied.error().error == BodyAnimationApplyError::k_character_unavailable) {
      // Runtime's native command returns false when a previously valid
      // character-bound body has deliberately left the world. Keep the
      // command unresolved; the owning world will dispose of the instance.
      return HandlerResult{.status = ScriptCommandStatus::k_running,
          .pause_reason = ScriptPauseReason::k_none,
          .reason_text = {}};
    }
    return HandlerResult{.status = ScriptCommandStatus::k_error,
        .pause_reason = ScriptPauseReason::k_missing_resource,
        .reason_text = fmt::format(
            "Script_SelectRelativeBodyAnimation: {}", applied.error().reason_text)};
  }

  if (current >= static_cast<float>(applied->max_frame_index)) {
    command.execution_count += 1U;
    if (command.execution_limit != K_INFINITE_EXECUTION_LIMIT &&
        command.execution_count >= command.execution_limit) {
      return HandlerResult{.status = ScriptCommandStatus::k_completed,
          .pause_reason = ScriptPauseReason::k_none,
          .reason_text = {}};
    }
    const float remainder{current - static_cast<float>(applied->max_frame_index)};
    instance.value_pool.at(base + 2U).set_float(0.0F);
    instance.value_pool.at(base + 3U).set_float(remainder + 1.0F);
    return HandlerResult{.status = ScriptCommandStatus::k_running,
        .pause_reason = ScriptPauseReason::k_none,
        .reason_text = {}};
  }

  instance.value_pool.at(base + 2U).set_float(current);
  instance.value_pool.at(base + 3U).set_float(current + script_delta_frames);
  return HandlerResult{.status = ScriptCommandStatus::k_running,
      .pause_reason = ScriptPauseReason::k_none,
      .reason_text = {}};
}

void ScriptRuntime::pause(ScriptInstance& instance,
    const RuntimeScriptCommand& command,
    const std::size_t group_index,
    const std::size_t chain_position,
    const std::size_t linked_index,
    const bool is_root,
    const ScriptPauseReason reason,
    std::string reason_text) {
  instance.paused = true;

  ScriptPauseInfo& info{instance.pause_info};
  info = ScriptPauseInfo{};
  info.scenario_name = m_scenario_name;
  info.script_index = instance.source_script_index;
  info.script_name = instance.script_name;
  info.instance_id = instance.instance_id;
  info.character_id = instance.launch_context.character_id;
  info.launch_parameter = instance.launch_context.parameter;
  info.current_group_index = group_index;
  info.chain_position = chain_position;
  info.is_root_command = is_root;
  info.command_index = is_root ? group_index : linked_index;
  info.file_offset = command.source_file_offset;
  info.opcode = command.opcode;
  const char* name{opcode_name(command.opcode)};
  info.opcode_name = name != nullptr ? name : fmt::format("{:#010x}", command.opcode);
  info.value_count = command.value_count;
  info.arguments.reserve(command.value_count);
  for (std::uint32_t index{0}; index < command.value_count; ++index) {
    const std::size_t pool_index{command.first_value_index + index};
    const Omikron::ScriptValue value{pool_index < instance.value_pool.size()
                                         ? instance.value_pool.at(pool_index)
                                         : Omikron::ScriptValue{}};
    info.arguments.push_back(ScriptArgumentView{.raw = value.raw,
        .as_signed = value.as_signed(),
        .as_unsigned = value.as_unsigned(),
        .as_float = value.as_float()});
  }
  info.execution_limit = command.execution_limit;
  info.execution_count = command.execution_count;
  info.next_command_index =
      command.next_linked_command_index.has_value()
          ? static_cast<std::int32_t>(command.next_linked_command_index.value())
          : -1;
  info.tick = m_tick;
  info.reason = reason;

  // Audio/music family: append audio-specific diagnostic context.
  if ((command.opcode & K_AUDIO_OPCODE_MASK) == K_AUDIO_OPCODE_FAMILY) {
    const Audio::AudioContextInfo context{m_world->audio_context()};
    reason_text += fmt::format(" | audio: available={}, format='{}', voices {}/{} active, music {}",
        context.available,
        context.negotiated_format,
        context.active_voices,
        context.free_voices,
        context.music_state);
    for (const std::string& event : context.recent_events) {
      reason_text += "\n  audio event: " + event;
    }
  }

  info.reason_text = std::move(reason_text);

  m_run_state = reason == ScriptPauseReason::k_unhandled_opcode
                    ? ScriptRunState::k_paused_on_unhandled
                    : ScriptRunState::k_paused_on_error;

  App::Log::warn(LogCategory::Script,
      "pause: {} (scenario '{}', script '{}' instance {}, group {}, chain {}, command {}, "
      "opcode {:#010x}): {}",
      pause_reason_name(reason),
      info.scenario_name,
      info.script_name,
      info.instance_id,
      group_index,
      chain_position,
      info.command_index,
      command.opcode,
      info.reason_text);
}

bool ScriptRuntime::precheck_completed(const RuntimeScriptCommand& command) {
  // Command eligibility is independent of the script-level repeat limit.
  return command.execution_limit != K_INFINITE_EXECUTION_LIMIT &&
         command.execution_count >= command.execution_limit;
}

bool ScriptRuntime::is_command_exhausted(const RuntimeScriptCommand& command) {
  // An unlimited (0xFFFFFFFF) execution limit never exhausts; a finite limit
  // exhausts once the execution count reaches it.
  return command.execution_limit != K_INFINITE_EXECUTION_LIMIT &&
         command.execution_count >= command.execution_limit;
}

void ScriptRuntime::finish_script_pass(ScriptInstance& instance) {
  instance.repeat_index += 1U;
  const bool repeats_forever{instance.repeat_limit == -1};
  const bool repeats_again{instance.repeat_limit > 0 &&
                           std::cmp_less(instance.repeat_index, instance.repeat_limit)};
  if (!repeats_forever && !repeats_again) {
    instance.completed = true;
    App::Log::debug(LogCategory::Script,
        "instance {} script '{}' completed at repeat {}",
        instance.instance_id,
        instance.script_name,
        instance.repeat_index);
    return;
  }

  for (RuntimeScriptCommand& command : instance.root_commands) {
    command.execution_count = command.initial_execution_count;
    reinitialize_command(instance, command);
  }
  for (RuntimeScriptCommand& command : instance.linked_commands) {
    command.execution_count = command.initial_execution_count;
    reinitialize_command(instance, command);
  }
  instance.current_group_index = 0;
  instance.elapsed_script_frames = 0.0F;
  instance.completed = false;
  instance.paused = false;
  instance.pause_info = ScriptPauseInfo{};
  instance.step_at_root = true;
  instance.step_linked_index = 0;
  instance.step_chain_position = 0;
  App::Log::trace(LogCategory::Script,
      "script '{}' repeat {} -> restart at group 0 (instance {})",
      instance.script_name,
      instance.repeat_index,
      instance.instance_id);
}

void ScriptRuntime::reset_instance_to_initial_state(ScriptInstance& instance) {
  reset_audio_commands(instance);
  instance.value_pool = m_scx->shared_values;
  for (RuntimeScriptCommand& command : instance.root_commands) {
    command.execution_count = command.initial_execution_count;
    reinitialize_command(instance, command);
  }
  for (RuntimeScriptCommand& command : instance.linked_commands) {
    command.execution_count = command.initial_execution_count;
    reinitialize_command(instance, command);
  }
  if (instance.launch_context.character_id.has_value()) {
    bool owns_body_animation{false};
    for (const RuntimeScriptCommand& command : instance.root_commands) {
      owns_body_animation =
          owns_body_animation || command.opcode == K_SELECT_RELATIVE_BODY_ANIMATION;
    }
    for (const RuntimeScriptCommand& command : instance.linked_commands) {
      owns_body_animation =
          owns_body_animation || command.opcode == K_SELECT_RELATIVE_BODY_ANIMATION;
    }
    if (owns_body_animation) {
      m_world->reset_body_animation(instance.launch_context.character_id.value_or(0));
    }
  }
  instance.current_group_index = 0;
  instance.repeat_index = instance.initial_repeat_index;
  instance.elapsed_script_frames = 0.0F;
  instance.completed = false;
  instance.paused = false;
  instance.pause_info = ScriptPauseInfo{};
  instance.sprite_remap.clear();
  instance.step_at_root = true;
  instance.step_linked_index = 0;
  instance.step_chain_position = 0;
}

std::expected<Sprite::SpriteHandle, std::string> ScriptRuntime::resolve_sprite(
    ScriptInstance& instance, const RuntimeScriptCommand& command) {
  const std::uint32_t source_index{instance.value_pool.at(command.first_value_index).as_unsigned()};
  const auto existing{instance.sprite_remap.find(source_index)};
  if (existing != instance.sprite_remap.end()) {
    return existing->second;
  }
  auto handle{m_world->ensure_sprite(source_index)};
  if (!handle) {
    return std::expected<Sprite::SpriteHandle, std::string>{std::unexpect, handle.error()};
  }
  instance.sprite_remap.emplace(source_index, handle.value());
  App::Log::debug(LogCategory::Script,
      "instance {} remapped source sprite {} -> runtime sprite {}:{}",
      instance.instance_id,
      source_index,
      handle->index,
      handle->generation);
  return handle;
}

HandlerResult ScriptRuntime::handle_set_sprite_type(
    ScriptInstance& instance, RuntimeScriptCommand& command) {
  const std::uint32_t base{command.first_value_index};
  const std::uint16_t sprite_type{
      static_cast<std::uint16_t>(instance.value_pool.at(base + 1U).as_unsigned())};

  auto sprite{resolve_sprite(instance, command)};
  if (!sprite) {
    return HandlerResult{.status = ScriptCommandStatus::k_error,
        .pause_reason = ScriptPauseReason::k_missing_resource,
        .reason_text = fmt::format("SetSpriteType: {}", sprite.error())};
  }

  // Always write the low 16 bits, even when zero — the default must remain a
  // real operation, never optimized away.
  m_world->set_sprite_type(sprite.value(), sprite_type);

  command.execution_count += 1;
  if (command.execution_limit != K_INFINITE_EXECUTION_LIMIT &&
      command.execution_count >= command.execution_limit) {
    return HandlerResult{.status = ScriptCommandStatus::k_completed,
        .pause_reason = ScriptPauseReason::k_none,
        .reason_text = {}};
  }
  return HandlerResult{.status = ScriptCommandStatus::k_running,
      .pause_reason = ScriptPauseReason::k_none,
      .reason_text = {}};
}

HandlerResult ScriptRuntime::handle_set_sprite_frame(
    ScriptInstance& instance, RuntimeScriptCommand& command) {
  const std::uint32_t base{command.first_value_index};
  const std::uint16_t frame_index{
      static_cast<std::uint16_t>(instance.value_pool.at(base + 1U).as_unsigned())};

  auto sprite{resolve_sprite(instance, command)};
  if (!sprite) {
    return HandlerResult{.status = ScriptCommandStatus::k_error,
        .pause_reason = ScriptPauseReason::k_missing_resource,
        .reason_text = fmt::format("SetSpriteFrame: {}", sprite.error())};
  }
  if (auto result{m_world->set_sprite_frame(sprite.value(), frame_index)}; !result) {
    return HandlerResult{.status = ScriptCommandStatus::k_error,
        .pause_reason = ScriptPauseReason::k_missing_resource,
        .reason_text = fmt::format("SetSpriteFrame: {}", result.error())};
  }

  command.execution_count += 1;
  if (command.execution_limit != K_INFINITE_EXECUTION_LIMIT &&
      command.execution_count >= command.execution_limit) {
    return HandlerResult{.status = ScriptCommandStatus::k_completed,
        .pause_reason = ScriptPauseReason::k_none,
        .reason_text = {}};
  }
  return HandlerResult{.status = ScriptCommandStatus::k_running,
      .pause_reason = ScriptPauseReason::k_none,
      .reason_text = {}};
}

HandlerResult ScriptRuntime::handle_interpolated(
    ScriptInstance& instance, RuntimeScriptCommand& command, const float script_delta_frames) {
  const std::uint16_t kind{interpolation_kind(command.opcode)};
  const std::uint32_t base{command.first_value_index};
  const float initial{instance.value_pool.at(base + 1U).as_float()};
  const float target{instance.value_pool.at(base + 2U).as_float()};
  const float current{instance.value_pool.at(base + 3U).as_float()};
  const float duration{instance.value_pool.at(base + 4U).as_float()};
  const float elapsed{instance.value_pool.at(base + 5U).as_float()};

  auto sprite{resolve_sprite(instance, command)};
  if (!sprite) {
    return HandlerResult{.status = ScriptCommandStatus::k_error,
        .pause_reason = ScriptPauseReason::k_missing_resource,
        .reason_text = fmt::format("{}: {}", opcode_name(command.opcode), sprite.error())};
  }

  if (elapsed >= duration) {
    // Completion branch: update the visible property first, then advance
    // execution state (Runtime order).
    apply_interpolated_value(sprite.value(), target, kind, /*is_completion=*/true);
    command.execution_count += 1;
    if (command.execution_limit != K_INFINITE_EXECUTION_LIMIT &&
        command.execution_count >= command.execution_limit) {
      return HandlerResult{.status = ScriptCommandStatus::k_completed,
          .pause_reason = ScriptPauseReason::k_none,
          .reason_text = {}};
    }
    instance.value_pool.at(base + 5U).set_float(0.0F);
    instance.value_pool.at(base + 3U) = instance.value_pool.at(base + 1U);
    return HandlerResult{.status = ScriptCommandStatus::k_running,
        .pause_reason = ScriptPauseReason::k_none,
        .reason_text = {}};
  }

  apply_interpolated_value(sprite.value(), current, kind, /*is_completion=*/false);
  const float next_current{current + (((target - initial) / duration) * script_delta_frames)};
  const float next_elapsed{elapsed + script_delta_frames};
  instance.value_pool.at(base + 3U).set_float(next_current);
  instance.value_pool.at(base + 5U).set_float(next_elapsed);
  return HandlerResult{.status = ScriptCommandStatus::k_running,
      .pause_reason = ScriptPauseReason::k_none,
      .reason_text = {}};
}

HandlerResult ScriptRuntime::handle_display_3d_sprite(
    ScriptInstance& instance, RuntimeScriptCommand& command, const float script_delta_frames) {
  const std::uint32_t base{command.first_value_index};
  const float duration{instance.value_pool.at(base + 2U).as_float()};
  const float elapsed{instance.value_pool.at(base + 3U).as_float()};

  if (duration == 0.0F) {
    return HandlerResult{.status = ScriptCommandStatus::k_error,
        .pause_reason = ScriptPauseReason::k_invalid_duration,
        .reason_text = "Display3DSprite: duration is exactly 0.0f"};
  }

  auto sprite{resolve_sprite(instance, command)};
  if (!sprite) {
    return HandlerResult{.status = ScriptCommandStatus::k_error,
        .pause_reason = ScriptPauseReason::k_missing_resource,
        .reason_text = fmt::format("Display3DSprite: {}", sprite.error())};
  }

  if (elapsed >= duration) {
    return HandlerResult{.status = ScriptCommandStatus::k_completed,
        .pause_reason = ScriptPauseReason::k_none,
        .reason_text = {}};
  }

  const std::uint32_t xyz_index{instance.value_pool.at(base + 1U).as_unsigned()};
  auto position{m_world->resolve_position(xyz_index)};
  if (!position) {
    return HandlerResult{.status = ScriptCommandStatus::k_error,
        .pause_reason = ScriptPauseReason::k_missing_resource,
        .reason_text = fmt::format("Display3DSprite: {}", position.error())};
  }
  m_world->set_sprite_position(sprite.value(), position.value());

  const float next_elapsed{elapsed + script_delta_frames};
  instance.value_pool.at(base + 3U).set_float(next_elapsed);

  if (next_elapsed >= duration) {
    command.execution_count += 1;
    if (command.execution_limit != K_INFINITE_EXECUTION_LIMIT &&
        command.execution_count >= command.execution_limit) {
      return HandlerResult{.status = ScriptCommandStatus::k_completed,
          .pause_reason = ScriptPauseReason::k_none,
          .reason_text = {}};
    }
    instance.value_pool.at(base + 3U).set_float(0.0F);
  }
  return HandlerResult{.status = ScriptCommandStatus::k_running,
      .pause_reason = ScriptPauseReason::k_none,
      .reason_text = {}};
}

HandlerResult ScriptRuntime::handle_wait(
    ScriptInstance& instance, RuntimeScriptCommand& command, const float script_delta_frames) {
  const std::uint32_t base{command.first_value_index};
  const float duration{instance.value_pool.at(base).as_float()};
  const float elapsed{instance.value_pool.at(base + 1U).as_float()};

  // Runtime's normal path for 0x06000017 compares elapsed against duration,
  // adds the global script delta, stores elapsed back to argument 1, then
  // increments the function progress once the duration is reached.
  if (duration <= 0.0F || elapsed >= duration) {
    command.execution_count += 1;
    return HandlerResult{.status = ScriptCommandStatus::k_completed,
        .pause_reason = ScriptPauseReason::k_none,
        .reason_text = {}};
  }

  const float next_elapsed{elapsed + script_delta_frames};
  instance.value_pool.at(base + 1U).set_float(next_elapsed);
  if (next_elapsed < duration) {
    return HandlerResult{.status = ScriptCommandStatus::k_running,
        .pause_reason = ScriptPauseReason::k_none,
        .reason_text = {}};
  }

  command.execution_count += 1;
  if (command.execution_limit != K_INFINITE_EXECUTION_LIMIT &&
      command.execution_count >= command.execution_limit) {
    return HandlerResult{.status = ScriptCommandStatus::k_completed,
        .pause_reason = ScriptPauseReason::k_none,
        .reason_text = {}};
  }

  instance.value_pool.at(base + 1U).set_float(0.0F);
  return HandlerResult{.status = ScriptCommandStatus::k_running,
      .pause_reason = ScriptPauseReason::k_none,
      .reason_text = {}};
}

namespace {

/// Completion predicate shared by the audio handlers: the command exhausts
/// when its (finite) execution limit is reached.
[[nodiscard]] HandlerResult audio_completion(RuntimeScriptCommand& command) {
  command.execution_count += 1;
  if (command.execution_limit != K_INFINITE_EXECUTION_LIMIT &&
      command.execution_count >= command.execution_limit) {
    return HandlerResult{.status = ScriptCommandStatus::k_completed,
        .pause_reason = ScriptPauseReason::k_none,
        .reason_text = {}};
  }
  return HandlerResult{.status = ScriptCommandStatus::k_running,
      .pause_reason = ScriptPauseReason::k_none,
      .reason_text = {}};
}

}  // namespace

HandlerResult ScriptRuntime::handle_play_sound(
    ScriptInstance& instance, RuntimeScriptCommand& command) {
  const std::uint32_t base{command.first_value_index};
  const std::uint32_t sound_index{instance.value_pool.at(base).as_unsigned()};
  const std::uint32_t flags{instance.value_pool.at(base + 1U).as_unsigned()};
  const std::uint32_t started{instance.value_pool.at(base + 2U).as_unsigned()};
  const std::int32_t object_index{instance.value_pool.at(base + 3U).as_signed()};

  // Preserve and log unknown flag bits; never assign invented meanings.
  const std::uint32_t unknown_bits{flags & ~1U};
  if (unknown_bits != 0U) {
    App::Log::debug(
        LogCategory::Script, "PlaySound: preserving unknown flag bits {:#010x}", unknown_bits);
  }

  if (started != 0U) {
    // Already started: do not start another voice (repeatable infinite-limit
    // command whose latch is set).
    return HandlerResult{.status = ScriptCommandStatus::k_completed,
        .pause_reason = ScriptPauseReason::k_none,
        .reason_text = {}};
  }

  const bool loop{(flags & 1U) != 0U};

  auto sound{m_world->resolve_sound(sound_index)};
  if (!sound) {
    App::Log::warn(LogCategory::Script,
        "PlaySound: sound index {} unavailable: {}",
        sound_index,
        sound.error());
    instance.value_pool.at(base + 2U).raw = 1;  // latch even on failure.
    return audio_completion(command);
  }
  if (!sound->resource.valid()) {
    App::Log::warn(LogCategory::Script,
        "PlaySound: sound index {} resolves to an invalid resource",
        sound_index);
    instance.value_pool.at(base + 2U).raw = 1;
    return audio_completion(command);
  }

  Audio::SoundPlayRequest request;
  request.resource = sound->resource;
  request.loop = loop;
  request.scenario_sound_index = static_cast<std::uint16_t>(sound_index);
  request.sound_name = sound->name;
  request.provenance = Audio::AudioProvenance{.origin = Audio::AudioOrigin::k_structured_script,
      .scenario_name = std::string{m_world->scenario_name()},
      .source_script_index = instance.source_script_index,
      .script_instance_id = instance.instance_id,
      .function_id = command.opcode};
  request.raw_flags = flags;

  if (object_index == -1) {
    // Nonspatial playback: no emitter, no owner token.
    request.owner = Audio::AudioOwnerToken{};
    request.emitter = std::nullopt;
  } else {
    auto owner{m_world->resolve_audio_owner(object_index)};
    if (!owner) {
      App::Log::warn(LogCategory::Script,
          "PlaySound: object index {} unavailable: {}",
          object_index,
          owner.error());
      instance.value_pool.at(base + 2U).raw = 1;
      return audio_completion(command);
    }
    auto position{m_world->resolve_owner_position(owner.value())};
    if (!position) {
      App::Log::warn(
          LogCategory::Script, "PlaySound: owner position unavailable: {}", position.error());
      instance.value_pool.at(base + 2U).raw = 1;
      return audio_completion(command);
    }
    request.owner = owner.value();
    request.emitter = Audio::SoundEmitterState{.position = position.value(),
        .velocity = {0.0F, 0.0F, 0.0F},
        .minimum_distance = K_PLAY_SOUND_MIN_DISTANCE,
        .maximum_distance = K_PLAY_SOUND_MAX_DISTANCE};
  }

  auto voice{m_world->play_sound(request)};
  if (!voice) {
    App::Log::warn(LogCategory::Script, "PlaySound: queue rejected: {}", voice.error());
  } else {
    App::Log::debug(LogCategory::Script,
        "PlaySound: queued voice {}:{} for sound '{}' ({} {})",
        voice->index,
        voice->generation,
        sound->name,
        loop ? "loop" : "once",
        request.emitter.has_value() ? "spatial" : "nonspatial");
  }
  instance.value_pool.at(base + 2U).raw = 1;  // latch even on failure.
  return audio_completion(command);
}

HandlerResult ScriptRuntime::handle_play_sync_sound(
    ScriptInstance& instance, RuntimeScriptCommand& command) {
  const std::uint32_t base{command.first_value_index};
  const float scheduled{instance.value_pool.at(base + 1U).as_float()};
  const std::uint32_t latch{instance.value_pool.at(base + 3U).as_unsigned()};

  if (latch != 0U) {
    // Already started once.
    return HandlerResult{.status = ScriptCommandStatus::k_completed,
        .pause_reason = ScriptPauseReason::k_none,
        .reason_text = {}};
  }

  // Wait until the scheduled script/scenario time is due.
  if (instance.elapsed_script_frames < scheduled) {
    return HandlerResult{.status = ScriptCommandStatus::k_running,
        .pause_reason = ScriptPauseReason::k_none,
        .reason_text = {}};
  }

  const std::uint32_t sound_index{instance.value_pool.at(base).as_unsigned()};
  const std::uint32_t flags{instance.value_pool.at(base + 2U).as_unsigned()};
  const std::int32_t object_index{instance.value_pool.at(base + 4U).as_signed()};
  const std::uint32_t unknown_bits{flags & ~1U};
  if (unknown_bits != 0U) {
    App::Log::debug(
        LogCategory::Script, "PlaySyncSound: preserving unknown flag bits {:#010x}", unknown_bits);
  }

  auto sound{m_world->resolve_sound(sound_index)};
  if (!sound) {
    App::Log::warn(LogCategory::Script,
        "PlaySyncSound: sound index {} unavailable: {}",
        sound_index,
        sound.error());
    return audio_completion(command);
  }
  if (!sound->resource.valid()) {
    App::Log::warn(LogCategory::Script,
        "PlaySyncSound: sound index {} resolves to an invalid resource",
        sound_index);
    return audio_completion(command);
  }

  Audio::SoundPlayRequest request;
  request.resource = sound->resource;
  request.loop = (flags & 1U) != 0U;
  request.scenario_sound_index = static_cast<std::uint16_t>(sound_index);
  request.sound_name = sound->name;
  request.provenance = Audio::AudioProvenance{.origin = Audio::AudioOrigin::k_structured_script,
      .scenario_name = std::string{m_world->scenario_name()},
      .source_script_index = instance.source_script_index,
      .script_instance_id = instance.instance_id,
      .function_id = command.opcode};
  request.raw_flags = flags;

  if (object_index == -1) {
    request.owner = Audio::AudioOwnerToken{};
    request.emitter = std::nullopt;
  } else {
    auto owner{m_world->resolve_audio_owner(object_index)};
    if (!owner) {
      App::Log::warn(LogCategory::Script,
          "PlaySyncSound: object index {} unavailable: {}",
          object_index,
          owner.error());
      return audio_completion(command);
    }
    auto position{m_world->resolve_owner_position(owner.value())};
    if (!position) {
      App::Log::warn(
          LogCategory::Script, "PlaySyncSound: owner position unavailable: {}", position.error());
      return audio_completion(command);
    }
    request.owner = owner.value();
    request.emitter = Audio::SoundEmitterState{.position = position.value(),
        .velocity = {0.0F, 0.0F, 0.0F},
        .minimum_distance = K_PLAY_SYNC_MIN_DISTANCE,
        .maximum_distance = K_PLAY_SYNC_MAX_DISTANCE};
  }

  auto voice{m_world->play_sound(request)};
  if (!voice) {
    App::Log::warn(LogCategory::Script, "PlaySyncSound: queue rejected: {}", voice.error());
    // Keep arg[3] = 0 (not started); the command still exhausts below.
  } else {
    // Store voiceIndex + 1 so a real index 0 never collides with "not started".
    instance.value_pool.at(base + 3U).raw = voice->index + 1U;
  }
  return audio_completion(command);
}

HandlerResult ScriptRuntime::handle_stop_sound(
    ScriptInstance& instance, RuntimeScriptCommand& command) {
  const std::uint32_t base{command.first_value_index};
  const std::uint32_t sound_index{instance.value_pool.at(base).as_unsigned()};
  const std::int32_t object_index{instance.value_pool.at(base + 1U).as_signed()};

  auto sound{m_world->resolve_sound(sound_index)};
  if (!sound) {
    App::Log::debug(LogCategory::Script,
        "StopSound: sound index {} unavailable: {}",
        sound_index,
        sound.error());
    return audio_completion(command);
  }
  auto owner{m_world->resolve_audio_owner(object_index)};
  if (!owner) {
    App::Log::debug(LogCategory::Script,
        "StopSound: object index {} unavailable: {}",
        object_index,
        owner.error());
    return audio_completion(command);
  }

  m_world->stop_sound(sound->resource, owner.value());
  return audio_completion(command);
}

void ScriptRuntime::reset_audio_commands(ScriptInstance& instance) {
  const auto stop_if_started = [&](const RuntimeScriptCommand& command) {
    const std::uint32_t base{command.first_value_index};
    const std::uint32_t pool_size{static_cast<std::uint32_t>(instance.value_pool.size())};

    bool started{false};
    std::uint32_t object_arg{0};
    if (command.opcode == K_PLAY_SOUND && command.value_count >= 4U) {
      started = instance.value_pool.at(base + 2U).as_unsigned() != 0U;
      object_arg = base + 3U;
    } else if (command.opcode == K_PLAY_SYNC_SOUND && command.value_count >= 5U) {
      started = instance.value_pool.at(base + 3U).as_unsigned() != 0U;
      object_arg = base + 4U;
    }
    if (!started || object_arg >= pool_size) {
      return;
    }

    const std::uint32_t sound_index{instance.value_pool.at(base).as_unsigned()};
    const std::int32_t object_index{instance.value_pool.at(object_arg).as_signed()};
    auto sound{m_world->resolve_sound(sound_index)};
    if (!sound || !sound->resource.valid()) {
      return;
    }
    auto owner{m_world->resolve_audio_owner(object_index)};
    if (!owner) {
      return;
    }
    m_world->stop_sound(sound->resource, owner.value());
  };

  for (const RuntimeScriptCommand& command : instance.root_commands) {
    stop_if_started(command);
  }
  for (const RuntimeScriptCommand& command : instance.linked_commands) {
    stop_if_started(command);
  }
}

void ScriptRuntime::apply_interpolated_value(const Sprite::SpriteHandle handle,
    const float value,
    const std::uint16_t kind,
    const bool is_completion) {
  switch (kind) {
    case K_KIND_SCALE_X:
      m_world->set_sprite_scale_x(handle, value);
      break;
    case K_KIND_SCALE_Y:
      m_world->set_sprite_scale_y(handle, value);
      break;
    case K_KIND_ROLL:
      if (is_completion) {
        // Runtime-faithful call boundary: the completion branch passes the
        // raw target (degrees) without the π/180 conversion (confirmed
        // asymmetry at 0x004A2940). See docs/ReverseEngineering.md.
        m_world->set_sprite_rotation(handle, value);
      } else {
        m_world->set_sprite_rotation(handle, value * K_DEGREES_TO_RADIANS);
      }
      break;
    default:
      break;
  }
}

void ScriptRuntime::append_trace(CommandTraceEntry entry) {
  m_trace.push_back(std::move(entry));
  while (m_trace.size() > k_trace_capacity) {
    m_trace.pop_front();
  }
}

void ScriptRuntime::set_user_paused(const bool paused) {
  m_user_paused = paused;
  if (m_user_paused && m_run_state == ScriptRunState::k_running) {
    m_run_state = ScriptRunState::k_user_paused;
  } else if (!m_user_paused && m_run_state == ScriptRunState::k_user_paused) {
    m_run_state = ScriptRunState::k_running;
  }
}

bool ScriptRuntime::user_paused() const {
  return m_user_paused;
}

void ScriptRuntime::step_tick(const float script_delta_frames) {
  const ScriptRunState previous{m_run_state};
  m_run_state = ScriptRunState::k_running;
  advance(script_delta_frames);
  if (m_run_state == ScriptRunState::k_running) {
    m_run_state = previous;
  }
}

void ScriptRuntime::step_command() {
  for (ScriptInstance& instance : m_instances) {
    if (instance.completed) {
      continue;
    }
    if (instance.current_group_index >= instance.root_commands.size()) {
      instance.completed = true;
      return;
    }
    ++m_tick;

    if (instance.step_at_root) {
      RuntimeScriptCommand& command{instance.root_commands.at(instance.current_group_index)};
      dispatch_command(instance, command, 0.0F, instance.current_group_index, 0, 0, true);
      if (command.next_linked_command_index.has_value()) {
        instance.step_linked_index = command.next_linked_command_index.value();
        instance.step_chain_position = 1;
        instance.step_at_root = false;
      }
      return;
    }

    const std::uint32_t index{instance.step_linked_index};
    if (index >= instance.linked_commands.size()) {
      instance.step_at_root = true;
      instance.step_chain_position = 0;
      return;
    }
    RuntimeScriptCommand& command{instance.linked_commands.at(index)};
    dispatch_command(instance,
        command,
        0.0F,
        instance.current_group_index,
        instance.step_chain_position,
        index,
        false);
    if (command.next_linked_command_index.has_value()) {
      instance.step_linked_index = command.next_linked_command_index.value();
      instance.step_chain_position += 1;
    } else {
      instance.step_at_root = true;
      instance.step_chain_position = 0;
    }
    return;
  }
}

std::expected<void, std::string> ScriptRuntime::reset_instance(const std::size_t instance_id) {
  for (ScriptInstance& instance : m_instances) {
    if (instance.instance_id != instance_id) {
      continue;
    }
    reset_instance_to_initial_state(instance);
    App::Log::debug(LogCategory::Script, "reset instance {}", instance_id);
    return {};
  }
  return std::expected<void, std::string>{
      std::unexpect, fmt::format("reset failed: no instance {}", instance_id)};
}

void ScriptRuntime::reset_all() {
  for (ScriptInstance& instance : m_instances) {
    reset_instance_to_initial_state(instance);
  }
  m_run_state = ScriptRunState::k_running;
  m_user_paused = false;
  App::Log::debug(LogCategory::Script, "reset all instances");
}

ScriptRunState ScriptRuntime::run_state() const {
  return m_run_state;
}

const std::vector<ScriptInstance>& ScriptRuntime::instances() const {
  return m_instances;
}

std::vector<ScriptInstance>& ScriptRuntime::instances() {
  return m_instances;
}

const ScriptInstance* ScriptRuntime::instance(const std::size_t instance_id) const {
  for (const ScriptInstance& candidate : m_instances) {
    if (candidate.instance_id == instance_id) {
      return &candidate;
    }
  }
  return nullptr;
}

const ScriptPauseInfo& ScriptRuntime::pause_info() const {
  for (const ScriptInstance& instance : m_instances) {
    if (instance.paused) {
      return instance.pause_info;
    }
  }
  static const ScriptPauseInfo k_empty{};
  return k_empty;
}

const std::deque<CommandTraceEntry>& ScriptRuntime::trace() const {
  return m_trace;
}

bool ScriptRuntime::trace_enabled() const {
  return m_trace_enabled;
}

void ScriptRuntime::set_trace_enabled(const bool enabled) {
  m_trace_enabled = enabled;
}

std::uint64_t ScriptRuntime::tick_count() const {
  return m_tick;
}

const Omikron::ScxData& ScriptRuntime::scx() const {
  return *m_scx;
}

float ScriptRuntime::last_real_delta_seconds() const {
  return m_last_real_delta_seconds;
}

float ScriptRuntime::last_script_delta_frames() const {
  return m_last_script_delta_frames;
}

bool ScriptRuntime::last_script_delta_clamped() const {
  return m_last_script_delta_clamped;
}

}  // namespace App::Script

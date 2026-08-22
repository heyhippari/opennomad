#include <fmt/format.h>
#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Core/Debug/DebugUI.hpp"
#include "Core/Debug/DebugUIInternal.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Scenario/ScenarioEngine.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "Core/Script/ScriptOpcode.hpp"
#include "Core/Script/ScriptRuntime.hpp"

namespace App::Debug {

namespace {

/// Human-readable run-state label for the script debugger.
const char* script_run_state_name(const Script::ScriptRunState state) {
  switch (state) {
    case Script::ScriptRunState::k_running:
      return "Running";
    case Script::ScriptRunState::k_user_paused:
      return "User-paused";
    case Script::ScriptRunState::k_paused_on_unhandled:
      return "Paused on unhandled opcode";
    case Script::ScriptRunState::k_paused_on_error:
      return "Paused on error";
    case Script::ScriptRunState::k_completed:
      return "Completed";
    default:
      return "Unknown";
  }
}

/// Human-readable label of one semantic parameter type.
const char* semantic_label(const std::uint16_t type) {
  switch (type) {
    case Script::k_semantic_sprite:
      return "sprite";
    case Script::k_semantic_unknown_7:
      return "unknown7";
    case Script::k_semantic_unknown_8:
      return "unknown8";
    case Script::k_semantic_xyz_pointer:
      return "xyz";
    case Script::k_semantic_duration:
      return "duration";
    case Script::k_semantic_progress_elapsed:
      return "elapsed";
    case Script::k_semantic_initial_scale:
      return "initial";
    case Script::k_semantic_target_scale:
      return "target";
    case Script::k_semantic_initial_roll:
      return "initial roll";
    case Script::k_semantic_target_roll:
      return "target roll";
    case Script::k_semantic_frame:
      return "frame";
    default:
      return nullptr;
  }
}

/// Copies one raw argument word to the clipboard in every interpretation.
std::string argument_text(const std::uint32_t raw) {
  const App::Omikron::ScriptValue value{.raw = raw};
  return fmt::format("raw {:#010x}  signed {}  unsigned {}  float {:.6g}",
      raw,
      value.as_signed(),
      value.as_unsigned(),
      static_cast<double>(value.as_float()));
}

}  // namespace

void DebugUI::show_script_command(Script::ScriptInstance& instance,
    Script::RuntimeScriptCommand& command,
    const std::size_t command_index,
    const bool is_root) {
  const Script::OpcodeInfo* info{Script::opcode_info(command.opcode)};
  const char* name{info == nullptr ? nullptr : info->name.data()};
  ImGui::Text("%s%s: %s (%#010x)",
      is_root ? "root " : "linked ",
      fmt::format("{}", command_index).c_str(),
      name == nullptr ? "unknown" : name,
      command.opcode);
  ImGui::Indent();
  ImGui::Text("args[%u..%u) next %d limit %#x count %u offset %#lx",
      command.first_value_index,
      command.first_value_index + command.value_count,
      command.next_linked_command_index.has_value()
          ? static_cast<int>(command.next_linked_command_index.value())
          : -1,
      command.execution_limit,
      command.execution_count,
      static_cast<unsigned long>(command.source_file_offset));
  for (std::uint32_t arg{0}; arg < command.value_count; ++arg) {
    const std::size_t pool_index{command.first_value_index + arg};
    if (pool_index >= instance.value_pool.size()) {
      ImGui::Text("  arg %u: <out of pool>", arg);
      continue;
    }
    const App::Omikron::ScriptValue& value{instance.value_pool.at(pool_index)};
    const char* label{nullptr};
    if (info != nullptr) {
      for (std::size_t param{0}; param < info->semantic_param_count; ++param) {
        if (info->semantic_params[param].argument_index == arg) {
          label = semantic_label(info->semantic_params[param].semantic_type);
          break;
        }
      }
    }
    ImGui::Text("  arg %u%s: raw %#010x signed %d unsigned %u float %.6g",
        arg,
        label == nullptr ? "" : fmt::format(" ({})", label).c_str(),
        value.raw,
        value.as_signed(),
        value.as_unsigned(),
        static_cast<double>(value.as_float()));
  }
  ImGui::Unindent();
}

void DebugUI::show_script_debugger() {
  ImGui::Begin("Script Debugger", &m_show_script_debugger);

  show_runtime_target_summary();
  ScenarioRuntime* scenario_runtime{m_runtime_context.resolved().runtime};
  if (scenario_runtime == nullptr) {
    ImGui::TextUnformatted("Selected target has no loaded script runtime.");
    ImGui::End();
    return;
  }
  Script::ScriptRuntime* runtime{scenario_runtime->script_runtime()};
  if (runtime == nullptr) {
    ImGui::TextUnformatted("Script runtime not initialised.");
    ImGui::End();
    return;
  }

  // --- Scenario overview ---
  ImGui::SeparatorText("Scenario");
  ImGui::Text("Path: %s", fmt::format("{}", scenario_runtime->script_scenario_name()).c_str());
  ImGui::Text("State: %s", script_run_state_name(runtime->run_state()));
  ImGui::Text("Tick: %llu", static_cast<unsigned long long>(runtime->tick_count()));
  ImGui::Text("Scripts: %lu, shared values: %lu",
      static_cast<unsigned long>(runtime->scx().scripts.size()),
      static_cast<unsigned long>(runtime->scx().shared_values.size()));
  ImGui::Text("Real delta:   %.6f s", static_cast<double>(runtime->last_real_delta_seconds()));
  ImGui::Text("Script delta: %.6f frames (30 Hz)%s",
      static_cast<double>(runtime->last_script_delta_frames()),
      runtime->last_script_delta_clamped() ? ", clamped to 3" : "");

  // --- Runtime controls ---
  ImGui::SeparatorText("Debug Overrides");
  ImGui::TextDisabled("These controls change SCX execution state.");
  const bool paused{runtime->run_state() != Script::ScriptRunState::k_running};
  if (ImGui::Button(paused ? "Resume" : "Pause")) {
    runtime->set_user_paused(!paused);
  }
  ImGui::SameLine();
  if (ImGui::Button("Step tick")) {
    runtime->step_tick(m_script_fixed_delta);
  }
  ImGui::SameLine();
  if (ImGui::Button("Step command")) {
    runtime->step_command();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset instances")) {
    runtime->reset_all();
  }
  ImGui::SliderFloat("Fixed delta (script frames)", &m_script_fixed_delta, 0.01F, 10.0F, "%.3f");
  ImGui::TextDisabled("Manual stepping uses 30 Hz script-frame units (1.0 = one frame).");

  bool trace{runtime->trace_enabled()};
  if (ImGui::Checkbox("Command trace", &trace)) {
    runtime->set_trace_enabled(trace);
  }

  // Debug-only manual activation, clearly marked as an override.
  ImGui::SeparatorText("Manual activation");
  ImGui::TextDisabled("Not used by the normal startup path.");
  const auto& scripts{runtime->scx().scripts};
  if (ImGui::BeginCombo("Source script",
          !m_script_selected_source.has_value()
              ? "(none)"
              : fmt::format("{}: {}",
                    m_script_selected_source.value(),
                    scripts.at(m_script_selected_source.value()).name)
                    .c_str())) {
    for (std::size_t index{0}; index < scripts.size(); ++index) {
      const bool selected{m_script_selected_source == index};
      if (ImGui::Selectable(
              fmt::format("{}: {}", index, scripts.at(index).name).c_str(), selected)) {
        m_script_selected_source = index;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Activate")) {
    if (m_script_selected_source.has_value()) {
      if (auto created{scenario_runtime->spawn_script_instance(m_script_selected_source.value())};
          created) {
        App::Log::warn(LogCategory::Debug,
            "manual debug activation of script {} (override)",
            m_script_selected_source.value());
      } else {
        App::Log::error(LogCategory::Debug, "manual activation failed: {}", created.error());
      }
    }
  }

  // --- Pause display ---
  if (runtime->run_state() == Script::ScriptRunState::k_paused_on_unhandled ||
      runtime->run_state() == Script::ScriptRunState::k_paused_on_error) {
    const Script::ScriptPauseInfo& info{runtime->pause_info()};
    ImGui::SeparatorText("Pause");
    ImGui::TextColored(K_WARNING_COLOR, "Reason: %s", Script::pause_reason_name(info.reason));
    ImGui::TextWrapped("%s", info.reason_text.c_str());
    ImGui::Text("Script %lu '%s' instance %lu group %lu chain %lu",
        static_cast<unsigned long>(info.script_index),
        info.script_name.c_str(),
        static_cast<unsigned long>(info.instance_id),
        static_cast<unsigned long>(info.current_group_index),
        static_cast<unsigned long>(info.chain_position));
    if (info.character_id.has_value()) {
      ImGui::Text("Launch: Character  Character: %d  Parameter: %d",
          info.character_id.value(),
          info.launch_parameter);
    }
    ImGui::Text("Command %s %lu, opcode %#010x (%s), file offset %#lx",
        info.is_root_command ? "root" : "linked",
        static_cast<unsigned long>(info.command_index),
        info.opcode,
        info.opcode_name.c_str(),
        static_cast<unsigned long>(info.file_offset));
    ImGui::Text("args %u, limit %#x, count %u, next %d",
        info.value_count,
        info.execution_limit,
        info.execution_count,
        info.next_command_index);
    for (std::size_t arg{0}; arg < info.arguments.size(); ++arg) {
      const Script::ScriptArgumentView& view{info.arguments.at(arg)};
      ImGui::Text("  arg %lu: raw %#010x signed %d unsigned %u float %.6g",
          static_cast<unsigned long>(arg),
          view.raw,
          view.as_signed,
          view.as_unsigned,
          static_cast<double>(view.as_float));
    }
    if (ImGui::Button("Copy pause diagnostics")) {
      std::string text{fmt::format(
          "pause: {}\nscript {} '{}' instance {} group {} chain {} command {} {}\nopcode "
          "{:#010x} ({}) file {:#x}\nargs {} limit {:#x} count {} next {}\n",
          Script::pause_reason_name(info.reason),
          info.script_index,
          info.script_name,
          info.instance_id,
          info.current_group_index,
          info.chain_position,
          info.is_root_command ? "root" : "linked",
          info.command_index,
          info.opcode,
          info.opcode_name,
          info.file_offset,
          info.value_count,
          info.execution_limit,
          info.execution_count,
          info.next_command_index)};
      for (std::size_t arg{0}; arg < info.arguments.size(); ++arg) {
        text += fmt::format("  arg {}: {}\n", arg, argument_text(info.arguments.at(arg).raw));
      }
      if (info.character_id.has_value()) {
        text += fmt::format("launch: character {} parameter {}\n",
            info.character_id.value(),
            info.launch_parameter);
      }
      ImGui::SetClipboardText(text.c_str());
    }
  }

  // --- Instance list ---
  ImGui::SeparatorText("Instances");
  const auto& instances{runtime->instances()};
  for (std::size_t index{0}; index < instances.size(); ++index) {
    const Script::ScriptInstance& instance{instances.at(index)};
    const std::string label{fmt::format("{}: '{}' (group {}/{}){}{}",
        instance.instance_id,
        instance.script_name,
        instance.current_group_index,
        instance.root_commands.size(),
        instance.completed ? " [completed]" : "",
        instance.paused ? " [paused]" : "")};
    if (ImGui::Selectable(label.c_str(), m_script_selected_instance == instance.instance_id)) {
      m_script_selected_instance = instance.instance_id;
    }
  }

  // --- Command/group inspector ---
  Script::ScriptInstance* selected{nullptr};
  for (Script::ScriptInstance& candidate : runtime->instances()) {
    if (candidate.instance_id == m_script_selected_instance) {
      selected = &candidate;
      break;
    }
  }
  if (selected != nullptr) {
    ImGui::SeparatorText("Selected instance");
    const Omikron::ScxScript& source_script{
        runtime->scx().scripts.at(selected->source_script_index)};
    ImGui::Text("Source script %lu, ID %u, field34 %d, sprite remaps %lu",
        static_cast<unsigned long>(selected->source_script_index),
        source_script.script_id,
        selected->execution_context_field_34,
        static_cast<unsigned long>(selected->sprite_remap.size()));
    if (selected->launch_context.character_id.has_value()) {
      ImGui::Text("Launch: Character  Character: %d  Parameter: %d",
          selected->launch_context.character_id.value(),
          selected->launch_context.parameter);
    } else {
      ImGui::TextUnformatted("Launch: World");
    }
    for (const auto& [source, handle] : selected->sprite_remap) {
      ImGui::Text("  source sprite %u -> runtime %u:%u", source, handle.index, handle.generation);
    }
    if (ImGui::Button("Reset this instance (debug override)")) {
      if (auto result{runtime->reset_instance(selected->instance_id)}; !result) {
        App::Log::warn(LogCategory::Debug, "reset failed: {}", result.error());
      }
    }

    if (ImGui::CollapsingHeader("Groups and commands")) {
      for (std::size_t group{0}; group < selected->root_commands.size(); ++group) {
        const bool is_current{group == selected->current_group_index};
        ImGui::Text("%sgroup %lu", is_current ? "> " : "  ", static_cast<unsigned long>(group));
        ImGui::Indent();
        show_script_command(*selected, selected->root_commands.at(group), group, true);
        std::optional<std::uint32_t> next{
            selected->root_commands.at(group).next_linked_command_index};
        while (next.has_value()) {
          if (*next >= selected->linked_commands.size()) {
            ImGui::Text("  <invalid next %u>", *next);
            break;
          }
          show_script_command(*selected, selected->linked_commands.at(*next), *next, false);
          next = selected->linked_commands.at(*next).next_linked_command_index;
        }
        ImGui::Unindent();
      }
    }
  }

  // --- Trace ---
  if (runtime->trace_enabled()) {
    ImGui::SeparatorText("Trace");
    if (ImGui::BeginChild("##ScriptTrace", ImVec2(0.0F, 180.0F), ImGuiChildFlags_Borders)) {
      for (const Script::CommandTraceEntry& entry : runtime->trace()) {
        ImGui::Text("tick %llu inst %lu group %lu chain %lu %s %s -> %s (count %u -> %u)",
            static_cast<unsigned long long>(entry.tick),
            static_cast<unsigned long>(entry.instance_id),
            static_cast<unsigned long>(entry.group_index),
            static_cast<unsigned long>(entry.chain_position),
            entry.opcode_name.c_str(),
            entry.status_before.c_str(),
            entry.status_after.c_str(),
            entry.execution_count_before,
            entry.execution_count_after);
        if (!entry.mutated_arguments.empty()) {
          ImGui::TextDisabled("    mutated: %s", entry.mutated_arguments.c_str());
        }
      }
    }
    ImGui::EndChild();
  }

  ImGui::End();
}

void DebugUI::show_scenarios() {
  ImGui::Begin("Scenarios", &m_show_scenarios);

  ScenarioManager* manager{m_context.scenario_manager};
  if (manager == nullptr) {
    ImGui::TextUnformatted("Scenario manager not available.");
    ImGui::End();
    return;
  }

  // --- Gameplay-mode slot ---
  ImGui::SeparatorText("Gameplay-mode slot");
  ImGui::Text("Mode: %s",
      fmt::format("{}", App::gameplay_mode_name(manager->current_gameplay_mode())).c_str());
  {
    const Omikron::ScxData* scx{manager->gameplay_mode_scx()};
    ImGui::Text("Path: %s",
        scx == nullptr
            ? "(not loaded)"
            : fmt::format("{}", App::gameplay_mode_scenario_path(manager->current_gameplay_mode()))
                  .c_str());
    ImGui::Text("Loaded: %s", scx == nullptr ? "no" : "yes");
    if (scx != nullptr) {
      ImGui::Text(
          "Scripts: %lu, active: %lu, sounds: %lu, sprites: %lu, models: %lu, shared values: %lu",
          static_cast<unsigned long>(scx->scripts.size()),
          static_cast<unsigned long>(manager->active_script_instances_total()),
          static_cast<unsigned long>(scx->sounds.size()),
          static_cast<unsigned long>(scx->sprites.size()),
          static_cast<unsigned long>(scx->models.size()),
          static_cast<unsigned long>(scx->shared_values.size()));
      if (ImGui::CollapsingHeader("Script templates##gameplay")) {
        for (std::size_t index{0}; index < scx->scripts.size(); ++index) {
          const Omikron::ScxScript& script{scx->scripts.at(index)};
          ImGui::Text("mode:%lu '%s' id %u — inactive",
              static_cast<unsigned long>(index),
              script.name.c_str(),
              script.script_id);
        }
      }
    }
  }

  ImGui::SeparatorText("World contexts");
  const auto contexts{manager->world_contexts()};
  for (std::size_t index{0}; index < contexts.size(); ++index) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const WorldSceneContext& context{contexts[index]};
    const std::string header{fmt::format("Context {} (scene {})", index, context.scene_id)};
    if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("Residency: %s", residency_name(context.residency));
      ImGui::Text("Cache index: %lu, generation: %u",
          static_cast<unsigned long>(index),
          context.generation);
      const char* decor{
          context.decor_path.has_value() ? context.decor_path->c_str() : "(not associated yet)"};
      ImGui::Text("Decor: %s", decor);
      if (!context.resolved_decor_path.empty()) {
        ImGui::Text("Resolved decor: %s", context.resolved_decor_path.c_str());
      }
      ImGui::Text(
          "Scenario: %s", context.scenario_path.empty() ? "(none)" : context.scenario_path.c_str());
      if (!context.resolved_scenario_path.empty()) {
        ImGui::Text("Resolved scenario: %s", context.resolved_scenario_path.c_str());
      }
      ImGui::Text("File size: %lu bytes", static_cast<unsigned long>(context.file_size_bytes));
      if (context.scx_data) {
        ImGui::Text(
            "Version: %u, scripts: %lu, sounds: %lu, sprites: %lu, models: %lu, shared values: %lu",
            context.scx_data->header.version,
            static_cast<unsigned long>(context.scx_data->scripts.size()),
            static_cast<unsigned long>(context.scx_data->sounds.size()),
            static_cast<unsigned long>(context.scx_data->sprites.size()),
            static_cast<unsigned long>(context.scx_data->models.size()),
            static_cast<unsigned long>(context.scx_data->shared_values.size()));
        if (ImGui::CollapsingHeader(fmt::format("Script templates##world{}", index).c_str())) {
          for (std::size_t script_index{0}; script_index < context.scx_data->scripts.size();
              ++script_index) {
            const Omikron::ScxScript& script{context.scx_data->scripts.at(script_index)};
            ImGui::Text("world:%lu '%s' id %u — inactive",
                static_cast<unsigned long>(script_index),
                script.name.c_str(),
                script.script_id);
          }
        }
      }
      if (!context.last_error.empty()) {
        ImGui::TextColored(K_WARNING_COLOR, "Last error: %s", context.last_error.c_str());
      }

      const std::uint32_t scene_id{context.scene_id};
      if (context.residency != WorldSceneResidencyState::Free) {
        ImGui::TextDisabled("Debug Overrides");
      }
      if (context.residency == WorldSceneResidencyState::LoadedInactive) {
        if (ImGui::Button(fmt::format("Activate##{}", index).c_str())) {
          if (auto result{manager->activate_world_context(scene_id)}; !result) {
            App::Log::error(
                LogCategory::Debug, "activate context {} failed: {}", scene_id, result.error());
          }
        }
      }
      if (context.residency == WorldSceneResidencyState::LoadedActive) {
        if (ImGui::Button(fmt::format("Deactivate##{}", index).c_str())) {
          if (auto result{manager->deactivate_world_context(scene_id)}; !result) {
            App::Log::error(
                LogCategory::Debug, "deactivate context {} failed: {}", scene_id, result.error());
          }
        }
      }
      if (context.residency != WorldSceneResidencyState::Free) {
        ImGui::SameLine();
        if (ImGui::Button(fmt::format("Unload##{}", index).c_str())) {
          if (context.residency == WorldSceneResidencyState::LoadedActive) {
            App::Log::warn(LogCategory::Debug, "deactivate context {} before unloading", scene_id);
          } else if (auto result{manager->unload_world_context(scene_id)}; !result) {
            App::Log::error(
                LogCategory::Debug, "unload context {} failed: {}", scene_id, result.error());
          }
        }
      }
    }
  }

  ImGui::SeparatorText("Debug Overrides");
  ImGui::TextDisabled("Mode switches replace only the gameplay-mode slot.");
  if (ImGui::Button("Switch to FirstPersonShooting (shoot2.scx)")) {
    if (auto result{manager->set_gameplay_mode(GameplayMode::FirstPersonShooting)}; !result) {
      App::Log::error(
          LogCategory::Debug, "switch to FirstPersonShooting failed: {}", result.error());
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Switch to HandToHandCombat (fight.scx)")) {
    if (auto result{manager->set_gameplay_mode(GameplayMode::HandToHandCombat)}; !result) {
      App::Log::error(LogCategory::Debug, "switch to HandToHandCombat failed: {}", result.error());
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Switch to Adventure (aventure.scx)")) {
    if (auto result{manager->set_gameplay_mode(GameplayMode::Adventure)}; !result) {
      App::Log::error(LogCategory::Debug, "switch to Adventure failed: {}", result.error());
    }
  }

  ImGui::End();
}

void DebugUI::show_area_script() {
  ImGui::Begin("Area Script", &m_show_area_script);

  const ScenarioEngine* engine{m_context.scenario_engine};
  if (engine == nullptr) {
    ImGui::TextUnformatted("Scenario engine not available.");
    ImGui::End();
    return;
  }
  const Script::AreaScriptRuntime* script{engine->area_script()};
  if (script == nullptr) {
    ImGui::TextUnformatted("Area script not loaded.");
    ImGui::End();
    return;
  }

  const char* state_name{"Unknown"};
  switch (script->state()) {
    case Script::AreaScriptState::k_ready:
      state_name = "Ready";
      break;
    case Script::AreaScriptState::k_running:
      state_name = "Running";
      break;
    case Script::AreaScriptState::k_waiting:
      state_name = "Waiting";
      break;
    case Script::AreaScriptState::k_paused_unsupported:
      state_name = "Paused (unsupported opcode)";
      break;
    case Script::AreaScriptState::k_completed:
      state_name = "Completed";
      break;
    case Script::AreaScriptState::k_failed:
      state_name = "Failed";
      break;
  }

  ImGui::Text("State: %s  active: %s  wait: %u",
      state_name,
      script->active() ? "yes" : "no",
      static_cast<unsigned int>(script->wait_state()));
  if (script->wait_info().interface.has_value()) {
    ImGui::Text("Wait interface: id=%u gen=%u",
        static_cast<unsigned int>(script->wait_info().interface->interface_id),
        script->wait_info().interface->generation);
  }
  if (script->wait_info().kind == Script::AreaWaitKind::k_character_script) {
    if (script->wait_info().character_script_instance.has_value()) {
      ImGui::Text("Waiting on character script instance %zu",
          script->wait_info().character_script_instance.value());
    }
    if (script->wait_info().character_script.has_value()) {
      const Script::AreaCharacterScriptRequest& request{
          script->wait_info().character_script.value()};
      ImGui::Text("Character: %d  Script ID: %u  Parameter: %d  Tracked: yes",
          request.character_id,
          request.script_id,
          request.parameter);
    }
  }
  ImGui::Text("Instruction pointer: %zu  executed: %zu",
      script->instruction_pointer(),
      script->executed_instruction_count());

  ImGui::SeparatorText("Variables");
  if (script->variables().empty()) {
    ImGui::TextUnformatted("(none)");
  } else {
    for (const auto& [id, value] : script->variables()) {
      ImGui::Text("%u = %d", static_cast<unsigned int>(id), value);
    }
  }

  if (script->state() == Script::AreaScriptState::k_paused_unsupported ||
      script->state() == Script::AreaScriptState::k_failed) {
    const Script::AreaPauseInfo& pause{script->pause_info()};
    ImGui::SeparatorText("Pause");
    ImGui::TextColored(K_WARNING_COLOR, "%s", pause.reason_text.c_str());
    ImGui::Text("offset %zu opcode %#010x (%s)",
        pause.offset,
        static_cast<unsigned int>(pause.opcode),
        pause.opcode_name.c_str());
    ImGui::Text("nearby: %s", pause.nearby_bytes.c_str());
  }

  ImGui::SeparatorText("Instruction trace");
  if (ImGui::BeginChild("##AreaTrace", ImVec2(0.0F, 240.0F), ImGuiChildFlags_Borders)) {
    for (const Script::AreaInstructionTrace& entry : script->trace()) {
      ImGui::Text("%zu %#010x %s",
          entry.offset,
          static_cast<unsigned int>(entry.opcode),
          entry.opcode_name.c_str());
      for (const std::int32_t operand : entry.operands) {
        ImGui::TextDisabled("    %d", operand);
      }
      if (!entry.effect.empty()) {
        ImGui::TextDisabled("    %s", entry.effect.c_str());
      }
    }
  }
  ImGui::EndChild();

  ImGui::End();
}

void DebugUI::show_startup() {
  ImGui::Begin("Startup / IAM", &m_show_startup);

  const ScenarioEngine* engine{m_context.scenario_engine};
  if (engine == nullptr) {
    ImGui::TextUnformatted("Scenario engine not available.");
    ImGui::End();
    return;
  }

  ImGui::SeparatorText("IAM/START");
  ImGui::Text(
      "Initial area: %d  linked area: %d", engine->initial_area_id(), engine->linked_area_id());

  ImGui::SeparatorText("Area mapping");
  const auto& mapping{engine->area_mapping_entries()};
  if (mapping.empty()) {
    ImGui::TextUnformatted("(empty)");
  } else {
    for (const auto& [area_id, linked] : mapping) {
      ImGui::Text("%d -> %d", area_id, linked);
    }
  }

  ImGui::SeparatorText("IAM/AREA record");
  const Omikron::IamAreaRecord* record{engine->area_record()};
  if (record == nullptr) {
    ImGui::TextUnformatted("(not loaded)");
  } else {
    ImGui::Text("Size: %zu bytes  script offset: %#x",
        record->record_size(),
        static_cast<unsigned int>(record->script_offset()));
    ImGui::Text("3DO: %s", record->model3do_name().c_str());
    ImGui::Text("SCX: %s", record->scenario_scx_name().c_str());
    ImGui::Text("MPT: %s", record->map_mpt_name().c_str());
    ImGui::Text("OPT: %s", record->options_opt_name().c_str());
    ImGui::Text("ANI: %s", record->animation_ani_name().c_str());
    ImGui::Text("Sky: %s", record->sky_3do_name().c_str());
    if (ImGui::CollapsingHeader("Tables")) {
      for (std::size_t index{0}; index < Omikron::IamAreaRecord::k_table_count; ++index) {
        const std::optional<std::size_t> stride{Omikron::IamAreaRecord::known_table_stride(index)};
        ImGui::Text("table %zu: offset %#x count %u stride %s",
            index,
            static_cast<unsigned int>(record->table_offset(index)),
            static_cast<unsigned int>(record->table_count(index)),
            stride.has_value() ? fmt::format("{}", *stride).c_str() : "unknown");
      }
    }
  }

  ImGui::SeparatorText("Initial AREA dependencies");
  ImGui::Text("Scenario SCX: %s", engine->initial_world_scenario_path().c_str());
  ImGui::Text("Decor 3DO: %s (%s)",
      engine->initial_world_decor_path().empty() ? "(none)"
                                                 : engine->initial_world_decor_path().c_str(),
      engine->initial_world_decor_state().empty() ? "not requested"
                                                  : engine->initial_world_decor_state().c_str());

  ImGui::SeparatorText("Active world context");
  const ScenarioManager& scenarios{engine->manager()};
  const WorldSceneContext* active_world{scenarios.active_world_context()};
  if (active_world == nullptr) {
    ImGui::TextUnformatted("(none)");
  } else {
    ImGui::Text("Scene ID: %u  generation: %u", active_world->scene_id, active_world->generation);
    ImGui::Text("Residency: %s", residency_name(active_world->residency));
    ImGui::Text("Scenario: %s",
        active_world->scenario_path.empty() ? "(none)" : active_world->scenario_path.c_str());
    if (!active_world->resolved_scenario_path.empty()) {
      ImGui::Text("Resolved scenario: %s", active_world->resolved_scenario_path.c_str());
    }
    ImGui::Text("Decor: %s",
        active_world->decor_path.has_value() ? active_world->decor_path->c_str() : "(none)");
    if (!active_world->resolved_decor_path.empty()) {
      ImGui::Text("Resolved decor: %s", active_world->resolved_decor_path.c_str());
    }
    ImGui::Text("Decor parsed: %s", active_world->decor_model.has_value() ? "yes" : "no");
    if (active_world->decor_model.has_value()) {
      ImGui::Text("meshes %lu materials %lu",
          static_cast<unsigned long>(active_world->decor_model->meshes.size()),
          static_cast<unsigned long>(active_world->decor_model->materials.size()));
    }
  }
  if (!engine->last_error().empty()) {
    ImGui::TextColored(K_WARNING_COLOR, "Last error: %s", engine->last_error().c_str());
  }
  ImGui::Text("Ticked: %s", engine->ticked() ? "yes" : "no");

  ImGui::End();
}

}  // namespace App::Debug

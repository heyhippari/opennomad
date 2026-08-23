#include <fmt/format.h>
#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Core/Debug/DebugEvidence.hpp"
#include "Core/Debug/DebugUI.hpp"
#include "Core/Debug/DebugUIInternal.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Scenario/ScenarioEngine.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "Core/Script/ScriptOpcode.hpp"
#include "Core/Script/ScriptRuntime.hpp"

namespace App::Debug {

namespace {

/// Human-readable run-state label for the SCX script inspector.
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
  ImGui::Text("%s %s: %s",
      is_root ? "Root command" : "-> SyncFunction",
      fmt::format("{}", command_index).c_str(),
      name == nullptr ? "unknown" : name);
  ImGui::Indent();
  ImGui::Text("Function ID: %#010x  execution: %u / %#x  source offset: %#lx",
      command.opcode,
      command.execution_count,
      command.execution_limit,
      static_cast<unsigned long>(command.source_file_offset));
  ImGui::Text("Arguments: [%u..%u)  next linked: %d",
      command.first_value_index,
      command.first_value_index + command.value_count,
      command.next_linked_command_index.has_value()
          ? static_cast<int>(command.next_linked_command_index.value())
          : -1);
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

void DebugUI::show_scx_script_inspector() {
  ImGui::Begin("SCX Script Inspector", &m_show_scx_script_inspector);

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

  if (ImGui::CollapsingHeader("Execution model", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextUnformatted("Retail Runtime");
    ImGui::BulletText("mutable primary loaded scripts execute directly");
    ImGui::BulletText("additional clone slots are created by Script_MakeInstance");
    ImGui::TextDisabled("[%s]", evidence_label(EvidenceConfidence::k_confirmed_runtime));
    ImGui::TextUnformatted("OpenNomad");
    ImGui::BulletText("parsed SCX definitions produce mutable ScriptInstance objects");
    ImGui::BulletText("safe modern representation; ownership is not Runtime-identical");
    ImGui::TextDisabled("[%s]", evidence_label(EvidenceConfidence::k_open_nomad_only));
    ImGui::TextDisabled("Structured SCX Script_* records are separate from the compact AREA VM.");
  }

  const auto& scripts{runtime->scx().scripts};
  if (ImGui::CollapsingHeader("SCX source scripts")) {
    ImGui::TextDisabled("Parsed serialized definitions; not inactive retail runtime templates.");
    for (std::size_t index{0}; index < scripts.size(); ++index) {
      const Omikron::ScxScript& script{scripts.at(index)};
      ImGui::Text("%zu: '%s'  ID %u  roots %u  linked %u",
          index,
          script.name.c_str(),
          script.script_id,
          script.root_command_count,
          script.linked_command_count);
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
  ImGui::SeparatorText("OpenNomad runtime instances");
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
    ImGui::Text("SCX source [%lu] '%s' (ID %u) -> OpenNomad runtime instance %lu",
        static_cast<unsigned long>(selected->source_script_index),
        source_script.name.c_str(),
        source_script.script_id,
        static_cast<unsigned long>(selected->instance_id));
    ImGui::Text("Context field34 %d, elapsed %.3f script frames, %s%s, sprite remaps %lu",
        selected->execution_context_field_34,
        static_cast<double>(selected->elapsed_script_frames),
        selected->completed ? "completed" : "active",
        selected->paused ? ", paused" : "",
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
    if (ImGui::CollapsingHeader("Groups & commands", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::TextDisabled("Group root command -> SyncFunction / linked chain");
      for (std::size_t group{0}; group < selected->root_commands.size(); ++group) {
        const bool is_current{group == selected->current_group_index};
        const std::string group_label{
            fmt::format("{}Group {}##ScriptGroup{}", is_current ? "> " : "", group, group)};
        if (!ImGui::TreeNodeEx(group_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
          continue;
        }
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
        ImGui::TreePop();
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
  if (selected != nullptr) {
    ImGui::SameLine();
    if (ImGui::Button("Reset selected instance")) {
      if (auto result{runtime->reset_instance(selected->instance_id)}; !result) {
        App::Log::warn(LogCategory::Debug, "reset failed: {}", result.error());
      }
    }
  }
  ImGui::SliderFloat("Fixed delta (script frames)", &m_script_fixed_delta, 0.01F, 10.0F, "%.3f");
  ImGui::TextDisabled("Manual stepping uses 30 Hz script-frame units (1.0 = one frame).");

  bool trace{runtime->trace_enabled()};
  if (ImGui::Checkbox("Command trace", &trace)) {
    runtime->set_trace_enabled(trace);
  }

  ImGui::TextUnformatted("Manual activation");
  ImGui::TextDisabled("Not used by the normal startup path; creates OpenNomad runtime state.");
  if (ImGui::BeginCombo("SCX source script",
          !m_script_selected_source.has_value()
              ? "(none)"
              : fmt::format("{}: {}",
                    m_script_selected_source.value(),
                    scripts.at(m_script_selected_source.value()).name)
                    .c_str())) {
    for (std::size_t index{0}; index < scripts.size(); ++index) {
      const bool source_selected{m_script_selected_source == index};
      if (ImGui::Selectable(
              fmt::format("{}: {}", index, scripts.at(index).name).c_str(), source_selected)) {
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

  ImGui::TextDisabled("Ownership and resources for the confirmed gameplay + two-world-slot model.");
  const std::vector<LoadedScenarioView> inventory{manager->scenario_inventory()};
  for (const LoadedScenarioView& scenario : inventory) {
    const bool gameplay{scenario.identity.role == ScenarioRole::GameplayMode};
    const std::string header{
        gameplay
            ? fmt::format(
                  "Gameplay-mode slot 0 ({})", gameplay_mode_name(manager->current_gameplay_mode()))
            : fmt::format("World slot {} (scene {})", scenario.identity.slot, scenario.scene_id)};
    if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
      continue;
    }

    ImGui::Text("Role: %s  slot: %u  generation: %u",
        gameplay ? "Gameplay Mode" : "World",
        scenario.identity.slot,
        scenario.identity.generation);
    const char* residency{residency_name(scenario.residency)};
    if (gameplay) {
      residency = scenario.loaded ? "Loaded" : "Not loaded";
    }
    ImGui::Text("Residency: %s", residency);
    ImGui::Text("Scenario path: %s",
        scenario.scenario_path.empty() ? "(none)" : scenario.scenario_path.c_str());
    ImGui::Text("Resolved path: %s",
        scenario.resolved_path.empty() ? "(unavailable)" : scenario.resolved_path.c_str());
    if (!gameplay) {
      ImGui::Text(
          "Decor path: %s", scenario.decor_path.empty() ? "(none)" : scenario.decor_path.c_str());
      ImGui::Text("Resolved decor: %s",
          scenario.resolved_decor_path.empty() ? "(unavailable)"
                                               : scenario.resolved_decor_path.c_str());
    }
    ImGui::Text("SCX file: version %u, %zu bytes", scenario.file_version, scenario.file_size);
    ImGui::Text("Runtime: %s  active instances: %zu  active voices: %zu",
        scenario.loaded ? "available" : "unavailable",
        scenario.active_script_instances,
        scenario.active_voices);
    ImGui::Text("Resources: SCX source scripts %zu | sounds %zu | sprites %zu | models %zu",
        scenario.script_count,
        scenario.sound_count,
        scenario.sprite_count,
        scenario.model_count);
    ImGui::Text("Shared values: %zu  render instances: %zu",
        scenario.shared_value_count,
        scenario.render_instances);
    if (!scenario.last_error.empty()) {
      ImGui::TextColored(K_WARNING_COLOR, "Last error: %s", scenario.last_error.c_str());
    }

    if (!gameplay && scenario.residency != WorldSceneResidencyState::Free) {
      ImGui::SeparatorText("Debug Overrides");
      if (scenario.residency == WorldSceneResidencyState::LoadedInactive &&
          ImGui::Button(fmt::format("Activate##{}", scenario.identity.slot).c_str())) {
        if (auto result{manager->activate_world_context(scenario.scene_id)}; !result) {
          App::Log::error(LogCategory::Debug,
              "activate context {} failed: {}",
              scenario.scene_id,
              result.error());
        }
      }
      if (scenario.residency == WorldSceneResidencyState::LoadedActive &&
          ImGui::Button(fmt::format("Deactivate##{}", scenario.identity.slot).c_str())) {
        if (auto result{manager->deactivate_world_context(scenario.scene_id)}; !result) {
          App::Log::error(LogCategory::Debug,
              "deactivate context {} failed: {}",
              scenario.scene_id,
              result.error());
        }
      }
      ImGui::SameLine();
      if (ImGui::Button(fmt::format("Unload##{}", scenario.identity.slot).c_str())) {
        if (scenario.residency == WorldSceneResidencyState::LoadedActive) {
          App::Log::warn(
              LogCategory::Debug, "deactivate context {} before unloading", scenario.scene_id);
        } else if (auto result{manager->unload_world_context(scenario.scene_id)}; !result) {
          App::Log::error(LogCategory::Debug,
              "unload context {} failed: {}",
              scenario.scene_id,
              result.error());
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

void DebugUI::show_area_vm() {
  ImGui::Begin("AREA VM", &m_show_area_vm);

  const ScenarioEngine* engine{m_context.scenario_engine};
  if (engine == nullptr) {
    ImGui::TextUnformatted("Scenario engine not available.");
    ImGui::End();
    return;
  }
  const Script::AreaScriptRuntime* script{engine->area_script()};
  if (script == nullptr) {
    ImGui::TextUnformatted("AREA bytecode context not loaded.");
    ImGui::End();
    return;
  }

  ImGui::TextDisabled(
      "Compact u8 AREA bytecode VM; separate from structured SCX Script_* execution.");
  ImGui::TextDisabled(
      "OpenNomad currently exposes one active context, not Runtime's 32-context registry.");
  ImGui::TextDisabled(
      "[%s] current representation", evidence_label(EvidenceConfidence::k_open_nomad_only));

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

  ImGui::Text("OpenNomad state: %s  active: %s  raw wait state: %u",
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

  ImGui::SeparatorText("Trace");
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

void DebugUI::show_runtime_overview() {
  ImGui::Begin("Runtime Overview", &m_show_runtime_overview);

  const ScenarioEngine* engine{m_context.scenario_engine};
  if (engine == nullptr) {
    ImGui::TextUnformatted("Scenario engine not available.");
    ImGui::End();
    return;
  }

  ImGui::TextDisabled(
      "Broad orchestration only; detailed ownership and execution have dedicated inspectors.");

  ImGui::SeparatorText("IAM / START");
  ImGui::Text(
      "Initial AREA: %d  linked AREA: %d", engine->initial_area_id(), engine->linked_area_id());
  ImGui::Text(
      "Current AREA record: %s", engine->area_record() == nullptr ? "unavailable" : "loaded");

  ImGui::SeparatorText("Scenario engine");
  ImGui::Text("Session scheduler: %s", engine->ticked() ? "ticking" : "not yet ticked");
  ImGui::Text("Gameplay mode: %s",
      fmt::format("{}", gameplay_mode_name(engine->manager().current_gameplay_mode())).c_str());

  ImGui::SeparatorText("Active contexts");
  const ScenarioManager& scenarios{engine->manager()};
  const WorldSceneContext* active_world{scenarios.active_world_context()};
  if (active_world == nullptr) {
    ImGui::TextUnformatted("(none)");
  } else {
    ImGui::Text("World: scene %u, generation %u, %s",
        active_world->scene_id,
        active_world->generation,
        residency_name(active_world->residency));
    ImGui::Text("Scenario: %s",
        active_world->scenario_path.empty() ? "(none)" : active_world->scenario_path.c_str());
  }
  ImGui::Text("Gameplay scenario: %s",
      scenarios.gameplay_scenario_path().empty()
          ? "(none)"
          : fmt::format("{}", scenarios.gameplay_scenario_path()).c_str());

  const InterfaceOpenRequest& request{engine->dispatcher().last_request()};
  ImGui::SeparatorText("Interface summary");
  ImGui::Text("Last request: ID %u, operands %d / %d",
      static_cast<unsigned int>(request.interface_id),
      request.operand_b,
      request.operand_c);
  ImGui::Text("Main menu: %s  preliminary interface 29: %s",
      engine->main_menu_active() ? "active" : "inactive",
      engine->preliminary_29_active() ? "active" : "inactive");
  if (!engine->last_error().empty()) {
    ImGui::TextColored(K_WARNING_COLOR, "Last error: %s", engine->last_error().c_str());
  }
  ImGui::End();
}

}  // namespace App::Debug

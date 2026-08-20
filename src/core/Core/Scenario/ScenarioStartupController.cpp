#include "Core/Scenario/ScenarioStartupController.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamStart.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "Core/Script/ScriptRuntime.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"

namespace App {

namespace {

constexpr std::string_view K_IAM_START_PATH{"IAM/START"};
constexpr std::string_view K_IAM_AREA_PATH{"IAM/AREA"};
constexpr std::string_view K_SCPTDATA_DIRECTORY{"SCPTDATA"};
/// Provisional directory for area decor models (same as Anekbah.3DO). The
/// original decor directory is recovered separately from the INI preference
/// system, which is deferred; this path is a best-effort, non-mandatory load.
constexpr std::string_view K_DECOR_DIRECTORY{"MESHES/DECORS"};
/// Extensions appended by the original AREA dependency loader.
constexpr std::string_view K_SCX_EXTENSION{".SCX"};
constexpr std::string_view K_3DO_EXTENSION{".3DO"};

/// Builds an archive-relative dependency path: directory/name + extension.
std::string dependency_path(const std::string_view directory,
    const std::string_view name,
    const std::string_view extension) {
  std::string path{directory};
  path += '/';
  path += name;
  path += extension;
  return path;
}

/// Opcodes recorded as provisional bootstrap actions in the startup trace.
bool is_provisional_trace_opcode(const std::uint32_t opcode) {
  switch (opcode) {
    case 0x38:
    case 0x4F:
    case 0x68:
    case 0x5F:
    case 0x60:
    case 0x83:
    case 0x76:
    case 0x77:
    case 0x84:
    case 0x85:
      return true;
    default:
      return false;
  }
}

}  // namespace

std::expected<std::vector<std::byte>, std::string> ScenarioStartupController::read_file(
    const std::string& relative_path) {
  APP_PROFILE_FUNCTION();

  auto loaded{load_game_file(relative_path)};
  if (!loaded) {
    return std::expected<std::vector<std::byte>, std::string>{
        std::unexpect, std::move(loaded).error()};
  }
  return std::move(loaded->bytes);
}

std::expected<void, std::string> ScenarioStartupController::select_permanent_mode_script(
    ScenarioManager& manager) {
  APP_PROFILE_FUNCTION();

  // Permanent gameplay-mode slot (aventure.SCX). Runtime selects this after
  // the startup videos and again after mode 3; it is not part of mode 2.
  if (auto result{manager.set_gameplay_mode(GameplayMode::Adventure)}; !result) {
    m_last_error = fmt::format("gameplay mode scenario load: {}", result.error());
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  return {};
}

void ScenarioStartupController::reset_session() {
  APP_PROFILE_FUNCTION();

  m_start.reset();
  m_area_archive.reset();
  m_area_slots = {};
  m_area_script.reset();
  m_start_bytes.clear();
  m_area_archive_bytes.clear();
  m_area_mapping.clear();
  m_initial_area_id = 0;
  m_linked_area_id = 0;
  m_grid_scx_path.clear();
  m_grid_3do_path.clear();
  m_grid_3do_state.clear();
  m_main_menu_active = false;
  m_active_handle.reset();
  m_last_error.clear();
  m_initialized = false;
  m_manager = nullptr;
  m_ticked = false;
  m_event_started = false;
  m_waiting_recorded = false;
}

std::expected<void, std::string> ScenarioStartupController::initialize_new_session(
    ScenarioManager& manager) {
  APP_PROFILE_FUNCTION();
  m_manager = &manager;

  // 1. IAM/START.
  auto start_file{read_file(std::string{K_IAM_START_PATH})};
  if (!start_file) {
    m_last_error = start_file.error();
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  m_start_bytes = std::move(start_file).value();

  auto start{Omikron::IamStart::load(std::span<const std::byte>{m_start_bytes})};
  if (!start) {
    m_last_error = start.error();
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  m_start.emplace(std::move(start).value());
  const Omikron::IamStart& start_view{*m_start};
  m_initial_area_id = start_view.initial_area_id();
  m_linked_area_id = start_view.linked_area_id();
  record("IAM_START.Loaded");
  record(
      "IAM_START.InitialArea", fmt::format("id={} linked={}", m_initial_area_id, m_linked_area_id));
  App::Log::info(LogCategory::Startup, "starting new session — area={}", m_initial_area_id);

  if (m_initial_area_id < 0) {
    m_last_error = fmt::format("initial area ID {} is negative", m_initial_area_id);
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }

  // Reproduce the original mapping assignment before the area is loaded.
  m_area_mapping[m_initial_area_id] = m_linked_area_id;

  // 2. IAM/AREA indexed archive, record <initial area>.
  auto area_file{read_file(std::string{K_IAM_AREA_PATH})};
  if (!area_file) {
    m_last_error = area_file.error();
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  m_area_archive_bytes = std::move(area_file).value();
  m_area_archive.emplace(std::span<const std::byte>{m_area_archive_bytes});

  const std::uint32_t area_id{static_cast<std::uint32_t>(m_initial_area_id)};
  auto record_span{m_area_archive->read_record(area_id)};
  if (!record_span) {
    m_last_error = record_span.error();
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }

  auto parsed_record{Omikron::IamAreaRecord::load(record_span.value())};
  if (!parsed_record) {
    m_last_error = parsed_record.error();
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  RuntimeAreaSlot& area_slot{m_area_slots.at(0)};
  area_slot.primary.emplace(std::move(parsed_record).value());
  area_slot.primary_area_id = m_initial_area_id;
  area_slot.secondary_area_id = m_linked_area_id;
  const Omikron::IamAreaRecord& area_record_view{*area_slot.primary};
  const std::size_t record_size{area_record_view.record_size()};
  const std::uint32_t script_offset{area_record_view.script_offset()};
  const std::string scx_name{area_record_view.scenario_scx_name()};
  const std::string model_name{area_record_view.model3do_name()};
  record("IAM_AREA.RecordLoaded", fmt::format("id={} size={:#x}", area_id, record_size));
  record("IAM_AREA.Parsed", fmt::format("scriptOffset={:#x}", script_offset));

  // 3. Dependencies in the original loader's order. For area 118 only the
  // scenario SCX and the primary 3DO names are populated.

  // GRID.3DO CPU ownership now lives in the world context (ScenarioManager
  // loads and parses it inside load_world_context). The startup controller
  // only derives the dependency name and records whether the dependency was
  // requested and whether it ultimately loaded.
  m_grid_3do_path.clear();
  if (model_name.empty()) {
    m_grid_3do_state = "absent: no model 3DO name in the area record";
    record("AreaDependency.GRID_3DO.SkippedUnavailable");
  } else {
    m_grid_3do_path = dependency_path(K_DECOR_DIRECTORY, model_name, K_3DO_EXTENSION);
    m_grid_3do_state = "requested";
    record("AreaDependency.GRID_3DO.Requested", m_grid_3do_path);
  }

  if (scx_name.empty()) {
    m_last_error = fmt::format("IAM/AREA record {} has no scenario SCX name", area_id);
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }

  m_grid_scx_path = dependency_path(K_SCPTDATA_DIRECTORY, scx_name, K_SCX_EXTENSION);

  const std::optional<std::string> decor_path{
      m_grid_3do_path.empty() ? std::nullopt : std::optional<std::string>{m_grid_3do_path}};
  auto world{manager.load_world_context(0, decor_path, m_grid_scx_path)};
  if (!world) {
    m_last_error = fmt::format("world scenario load: {}", world.error());
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  if (auto result{manager.activate_world_context(0)}; !result) {
    m_last_error = fmt::format("world activation: {}", result.error());
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }

  // Reflect the decor load result in the startup diagnostic without
  // retaining a duplicate parsed model: the model object lives in the world
  // context and is exposed through ScenarioManager.
  if (!m_grid_3do_path.empty()) {
    const WorldSceneContext* grid{manager.find_world_context(0)};
    if (grid != nullptr && grid->decor_model.has_value()) {
      m_grid_3do_state = "loaded";
      record("AreaDependency.GRID_3DO.Loaded", m_grid_3do_path);
    } else {
      m_grid_3do_state = "unavailable";
      App::Log::warn(
          LogCategory::Scenario, "GRID.3DO unavailable (non-fatal): {}", m_grid_3do_path);
      record("AreaDependency.GRID_3DO.Failed", m_grid_3do_path);
    }
  }
  record("AreaDependency.GRID_SCX.Loaded", "slot=world0");

  // 4. Area script context: create, queue event/state 1, activate. The
  // first interpreter tick runs in tick().
  m_area_script.emplace(area_record_view.script_bytes());
  Script::AreaScriptRuntime& area_script{*m_area_script};

  area_script.set_music_sink([this](const Audio::MusicTrackRequest& request) {
    record("Music.TrackRequested",
        fmt::format("track={} loop={} mode={}", request.track_id, request.loop, request.mode_flag));
    App::Log::debug(LogCategory::Script,
        "AREA opcode 0x67 — music track={} loop={} mode={}",
        request.track_id,
        request.loop,
        request.mode_flag);
    if (m_audio == nullptr) {
      App::Log::warn(LogCategory::Music,
          "track {} requested but no audio system is available",
          request.track_id);
      return;
    }
    if (auto result{m_audio->play_music_track(request)}; !result) {
      App::Log::warn(
          LogCategory::Music, "track {} play failed: {}", request.track_id, result.error());
      record("Music.TrackFailed", result.error());
    }
  });

  area_script.set_scx_script_sink([this](const Script::AreaScxScriptRequest& request)
                                      -> std::expected<std::size_t, std::string> {
    if (m_manager == nullptr) {
      return std::expected<std::size_t, std::string>{
          std::unexpect, "scenario manager is not available"};
    }
    WorldSceneContext* context{m_manager->active_world_context()};
    if (context == nullptr || !context->scx_data.has_value() || context->runtime == nullptr) {
      return std::expected<std::size_t, std::string>{std::unexpect, "no active world SCX runtime"};
    }

    const auto& scripts{context->scx_data->scripts};
    for (std::size_t index{0}; index < scripts.size(); ++index) {
      if (scripts.at(index).script_id != request.script_id) {
        continue;
      }
      auto created{context->runtime->spawn_script_instance(index)};
      if (!created) {
        return created;
      }
      record("AreaScript.ScxScriptStarted",
          fmt::format("id={} name='{}' instance={} args=({}, {})",
              request.script_id,
              scripts.at(index).name,
              created.value(),
              request.operand_b,
              request.operand_c));
      App::Log::debug(LogCategory::Script,
          "AREA opcode 0x39 — started GRID script {} '{}' as instance {}",
          request.script_id,
          scripts.at(index).name,
          created.value());
      return created;
    }
    return std::expected<std::size_t, std::string>{std::unexpect,
        fmt::format("SCX script ID {} not found in active world", request.script_id)};
  });

  area_script.set_interface_sink([this](const InterfaceOpenRequest& request)
                                     -> std::expected<InterfaceHandle, std::string> {
    record("Interface.OpenRequested",
        fmt::format(
            "id={} arg2={} arg3={}", request.interface_id, request.operand_b, request.operand_c));
    App::Log::debug(LogCategory::Script,
        "AREA opcode 0x46 — interface={} args=({},{})",
        request.interface_id,
        request.operand_b,
        request.operand_c);
    auto result{m_dispatcher.open(request)};
    if (!result) {
      App::Log::warn(LogCategory::Interface,
          "interface {} dispatch failed: {}",
          request.interface_id,
          result.error());
      return result;
    }
    // Startup-specific tracking: interface 29 is the main menu the
    // recovered AREA path must open. The generic dispatcher has no
    // knowledge of this.
    if (request.interface_id == k_main_menu_interface) {
      m_main_menu_active = true;
      m_active_handle = result.value();
      record("MainMenu.Active");
    }
    return result;
  });

  m_dispatcher.set_interface_completion_sink([this](const InterfaceCompletion& completion) {
    if (m_active_handle.has_value() && completion.handle == m_active_handle.value()) {
      m_main_menu_active = false;
      m_active_handle.reset();
    }
    if (auto result{complete_interface(completion)}; !result) {
      App::Log::warn(LogCategory::Interface, "interface completion ignored: {}", result.error());
    }
  });

  area_script.set_instruction_sink(
      [this](const std::uint32_t opcode, const std::vector<std::int32_t>& operands) {
        if (opcode == 0x0D) {
          const std::int32_t index{operands.empty() ? 0 : operands.front()};
          record("AreaScript.VariableSet", fmt::format("index={} value=1", index));
        } else if (opcode == 0x0E) {
          const std::int32_t index{operands.empty() ? 0 : operands.at(0)};
          const std::int32_t value{operands.size() >= 2 ? operands.at(1) : 0};
          record("AreaScript.VariableSet", fmt::format("index={} value={}", index, value));
        } else if (is_provisional_trace_opcode(opcode)) {
          record("AreaScript.BootstrapOpcode", fmt::format("opcode={:#x}", opcode));
        }
      });
  record("AreaContext.Created", fmt::format("area={}", m_initial_area_id));
  area_script.queue_event(1);
  record("AreaContext.EventQueued", "event=1");
  area_script.activate();
  record("AreaContext.Activated");

  m_initialized = true;
  return {};
}

std::expected<void, std::string> ScenarioStartupController::initialize(ScenarioManager& manager) {
  APP_PROFILE_FUNCTION();

  if (auto result{select_permanent_mode_script(manager)}; !result) {
    return result;
  }
  if (auto result{manager.reset_for_new_session()}; !result) {
    m_last_error = result.error();
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return result;
  }
  reset_session();
  return initialize_new_session(manager);
}

std::expected<void, std::string> ScenarioStartupController::tick(const float delta_seconds) {
  APP_PROFILE_FUNCTION();

  if (!m_initialized) {
    m_last_error = "startup not initialized";
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  if (!m_area_script.has_value()) {
    m_last_error = "area script context not initialized";
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }

  Script::AreaScriptRuntime& area_script{*m_area_script};
  m_ticked = true;

  // Opcode 0x39 bridges into the active world's SCX runtime and yields the
  // AREA VM until that concrete ScriptRuntime instance completes.
  if (area_script.state() == Script::AreaScriptState::k_waiting &&
      area_script.wait_info().kind == Script::AreaWaitKind::k_scx_script) {
    if (m_manager == nullptr || !area_script.wait_info().scx_script_instance.has_value()) {
      m_last_error = "AREA SCX-script wait has no scenario owner or instance ID";
      return std::expected<void, std::string>{std::unexpect, m_last_error};
    }
    const WorldSceneContext* context{m_manager->active_world_context()};
    if (context == nullptr || context->runtime == nullptr ||
        context->runtime->script_runtime() == nullptr) {
      m_last_error = "AREA is waiting on an SCX script but no active world runtime exists";
      return std::expected<void, std::string>{std::unexpect, m_last_error};
    }

    const Script::ScriptRuntime* script_runtime{context->runtime->script_runtime()};
    const std::size_t instance_id{area_script.wait_info().scx_script_instance.value()};
    const Script::ScriptInstance* instance{script_runtime->instance(instance_id)};
    if (instance == nullptr) {
      m_last_error = fmt::format("AREA is waiting on missing SCX script instance {}", instance_id);
      return std::expected<void, std::string>{std::unexpect, m_last_error};
    }
    if (instance->paused) {
      m_last_error = fmt::format("SCX script instance {} '{}' paused: {}",
          instance_id,
          instance->script_name,
          instance->pause_info.reason_text);
      return std::expected<void, std::string>{std::unexpect, m_last_error};
    }
    if (instance->completed) {
      if (auto completed{area_script.complete_scx_script_wait(instance_id)}; !completed) {
        m_last_error = completed.error();
        return std::expected<void, std::string>{std::unexpect, m_last_error};
      }
      record("AreaScript.ScxScriptCompleted", fmt::format("instance={}", instance_id));
    }
  }

  // Event 1 is recorded as started exactly once; a resumed script continues
  // from its existing instruction pointer on later frames without re-recording.
  if (!m_event_started) {
    record("AreaScript.EventStarted", "event=1");
    App::Log::info(LogCategory::Script, "AREA {} event 1 started", m_initial_area_id);
    m_event_started = true;
  }

  // A completion may have resumed the VM and immediately lead to another
  // wait kind in this same tick; allow that transition to be recorded.
  if (area_script.state() != Script::AreaScriptState::k_waiting) {
    m_waiting_recorded = false;
  }

  const Script::AreaScriptState state{area_script.run(delta_seconds)};

  if (state == Script::AreaScriptState::k_failed) {
    m_last_error = area_script.pause_info().reason_text;
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  // A waiting script is a harmless no-op across frames; record the transition
  // into waiting only once so per-frame ticks stay quiet.
  if (state != Script::AreaScriptState::k_waiting) {
    m_waiting_recorded = false;
  }
  if (state == Script::AreaScriptState::k_waiting && !m_waiting_recorded) {
    record("AreaContext.Waiting", fmt::format("state={}", area_script.wait_state()));
    m_waiting_recorded = true;
  }
  return {};
}

std::expected<void, std::string> ScenarioStartupController::complete_interface(
    const InterfaceCompletion& completion) {
  APP_PROFILE_FUNCTION();

  if (!m_area_script.has_value()) {
    return std::expected<void, std::string>{std::unexpect, "area script context not initialized"};
  }
  return m_area_script->complete_interface_wait(completion);
}

void ScenarioStartupController::set_trace_recorder(Startup::StartupTraceRecorder* trace) {
  m_trace = trace;
}

void ScenarioStartupController::set_audio_system(Audio::AudioSystem* audio) {
  m_audio = audio;
}

void ScenarioStartupController::open_preliminary_29() {
  APP_PROFILE_FUNCTION();

  m_preliminary_29_active = true;
  App::Log::debug(LogCategory::Startup, "preliminary interface 29 opened (splash)");
}

void ScenarioStartupController::close_preliminary_29() {
  APP_PROFILE_FUNCTION();

  if (m_preliminary_29_active) {
    m_preliminary_29_active = false;
    App::Log::debug(LogCategory::Startup, "preliminary interface 29 closed");
  }
}

void ScenarioStartupController::record(std::string name, std::string detail) {
  if (m_trace != nullptr) {
    m_trace->record(std::move(name), std::move(detail));
  }
}

const Omikron::IamAreaRecord* ScenarioStartupController::area_record() const {
  const RuntimeAreaSlot& slot{m_area_slots.at(0)};
  return slot.primary.has_value() ? &*slot.primary : nullptr;
}

std::optional<std::int32_t> ScenarioStartupController::area_mapping(
    const std::int32_t area_id) const {
  const auto found{m_area_mapping.find(area_id)};
  if (found == m_area_mapping.end()) {
    return std::nullopt;
  }
  return found->second;
}

const std::unordered_map<std::int32_t, std::int32_t>&
ScenarioStartupController::area_mapping_entries() const {
  return m_area_mapping;
}

const Script::AreaScriptRuntime* ScenarioStartupController::area_script() const {
  return m_area_script.has_value() ? &*m_area_script : nullptr;
}

}  // namespace App

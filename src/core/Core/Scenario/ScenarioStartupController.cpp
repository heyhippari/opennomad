#include "Core/Scenario/ScenarioStartupController.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Log.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamStart.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Resources.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
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
    case 0x5C:
    case 0x83:
    case 0x67:
    case 0x76:
      return true;
    default:
      return false;
  }
}

}  // namespace

std::expected<std::vector<std::byte>, std::string> ScenarioStartupController::read_file(
    const std::string& relative_path) {
  APP_PROFILE_FUNCTION();

  const std::filesystem::path root_relative{Resources::game_data_path(relative_path)};
  const std::filesystem::path resolved{Resources::resolve_case_insensitive(root_relative)};

  std::size_t size{0};
  void* raw{SDL_LoadFile(resolved.string().c_str(), &size)};
  if (raw == nullptr) {
    return std::expected<std::vector<std::byte>, std::string>{std::unexpect,
        fmt::format("cannot read '{}' (resolved '{}'): {}",
            relative_path,
            resolved.string(),
            SDL_GetError())};
  }

  std::vector<std::byte> bytes(size);
  if (size > 0) {
    std::memcpy(bytes.data(), raw, size);
  }
  SDL_free(raw);
  return bytes;
}

std::expected<void, std::string> ScenarioStartupController::select_permanent_mode_script(
    ScenarioManager& manager) {
  APP_PROFILE_FUNCTION();

  // Permanent gameplay-mode slot (aventure.SCX). Runtime selects this after
  // the startup videos and again after mode 3; it is not part of mode 2.
  if (auto result{manager.set_gameplay_mode(GameplayMode::Adventure)}; !result) {
    m_last_error = fmt::format("gameplay mode scenario load: {}", result.error());
    App::Log::error("Startup failed: {}", m_last_error);
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
  m_grid_3do_model.reset();
  m_last_error.clear();
  m_initialized = false;
  m_ticked = false;
}

std::expected<void, std::string> ScenarioStartupController::initialize_new_session(
    ScenarioManager& manager) {
  APP_PROFILE_FUNCTION();

  // 1. IAM/START.
  auto start_file{read_file(std::string{K_IAM_START_PATH})};
  if (!start_file) {
    m_last_error = start_file.error();
    App::Log::error("Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  m_start_bytes = std::move(start_file).value();

  auto start{Omikron::IamStart::load(std::span<const std::byte>{m_start_bytes})};
  if (!start) {
    m_last_error = start.error();
    App::Log::error("Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  m_start.emplace(std::move(start).value());
  const Omikron::IamStart& start_view{*m_start};
  m_initial_area_id = start_view.initial_area_id();
  m_linked_area_id = start_view.linked_area_id();
  record("IAM_START.Loaded");
  record("IAM_START.InitialArea",
      fmt::format("id={} linked={}", m_initial_area_id, m_linked_area_id));

  if (m_initial_area_id < 0) {
    m_last_error = fmt::format("initial area ID {} is negative", m_initial_area_id);
    App::Log::error("Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }

  // Reproduce the original mapping assignment before the area is loaded.
  m_area_mapping[m_initial_area_id] = m_linked_area_id;

  // 2. IAM/AREA indexed archive, record <initial area>.
  auto area_file{read_file(std::string{K_IAM_AREA_PATH})};
  if (!area_file) {
    m_last_error = area_file.error();
    App::Log::error("Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  m_area_archive_bytes = std::move(area_file).value();
  m_area_archive.emplace(std::span<const std::byte>{m_area_archive_bytes});

  const std::uint32_t area_id{static_cast<std::uint32_t>(m_initial_area_id)};
  auto record_span{m_area_archive->read_record(area_id)};
  if (!record_span) {
    m_last_error = record_span.error();
    App::Log::error("Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }

  auto parsed_record{Omikron::IamAreaRecord::load(record_span.value())};
  if (!parsed_record) {
    m_last_error = parsed_record.error();
    App::Log::error("Startup failed: {}", m_last_error);
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

  // GRID.3DO: the decor model is not required to render the menu, so its load
  // is best-effort and non-fatal; success or failure is represented honestly.
  m_grid_3do_path.clear();
  if (model_name.empty()) {
    m_grid_3do_state = "absent: no model 3DO name in the area record";
    record("AreaDependency.GRID_3DO.SkippedUnavailable");
  } else {
    m_grid_3do_path = dependency_path(K_DECOR_DIRECTORY, model_name, K_3DO_EXTENSION);
    if (auto model_file{read_file(m_grid_3do_path)}) {
      auto parsed_model{
          Omikron::Model3DO::load(std::span<const std::byte>{model_file.value()})};
      if (parsed_model) {
        m_grid_3do_model.emplace(std::move(parsed_model).value());
        m_grid_3do_state = "loaded";
        record("AreaDependency.GRID_3DO.Loaded", m_grid_3do_path);
      } else {
        m_grid_3do_state = parsed_model.error();
        App::Log::warn("GRID.3DO parse failed (non-fatal): {}", m_grid_3do_state);
        record("AreaDependency.GRID_3DO.Failed", m_grid_3do_path);
      }
    } else {
      m_grid_3do_state = model_file.error();
      App::Log::warn("GRID.3DO unavailable (non-fatal): {}", m_grid_3do_state);
      record("AreaDependency.GRID_3DO.Failed", m_grid_3do_path);
    }
  }

  if (scx_name.empty()) {
    m_last_error = fmt::format("IAM/AREA record {} has no scenario SCX name", area_id);
    App::Log::error("Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }

  m_grid_scx_path = dependency_path(K_SCPTDATA_DIRECTORY, scx_name, K_SCX_EXTENSION);

  const std::optional<std::string> decor_path{
      m_grid_3do_path.empty() ? std::nullopt : std::optional<std::string>{m_grid_3do_path}};
  auto world{manager.load_world_context(0, decor_path, m_grid_scx_path)};
  if (!world) {
    m_last_error = fmt::format("world scenario load: {}", world.error());
    App::Log::error("Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  if (auto result{manager.activate_world_context(0)}; !result) {
    m_last_error = fmt::format("world activation: {}", result.error());
    App::Log::error("Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  record("AreaDependency.GRID_SCX.Loaded", "slot=world0");

  // 4. Area script context: create, queue event/state 1, activate. The
  // first interpreter tick runs in tick().
  m_area_script.emplace(area_record_view.script_bytes());
  Script::AreaScriptRuntime& area_script{*m_area_script};
  area_script.set_interface_sink([this](const std::uint16_t interface_id,
                                    const std::int16_t operand_b,
                                    const std::int16_t operand_c) {
    const InterfaceOpenRequest request{
        .interface_id = interface_id, .operand_b = operand_b, .operand_c = operand_c};
    record("Interface.OpenRequested",
        fmt::format("id={} arg2={} arg3={}", interface_id, operand_b, operand_c));
    const std::expected<void, std::string> result{m_dispatcher.open(request)};
    if (!result) {
      App::Log::warn("interface {} dispatch failed: {}", interface_id, result.error());
    } else if (interface_id == InterfaceDispatcher::k_main_menu_interface) {
      record("MainMenu.Active");
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
    App::Log::error("Startup failed: {}", m_last_error);
    return result;
  }
  reset_session();
  return initialize_new_session(manager);
}

std::expected<void, std::string> ScenarioStartupController::tick() {
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
  record("AreaScript.EventStarted", "event=1");
  const Script::AreaScriptState state{area_script.run()};
  App::Log::info("area script state after first tick: {}", static_cast<int>(state));

  if (state == Script::AreaScriptState::k_failed) {
    m_last_error = area_script.pause_info().reason_text;
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  if (state == Script::AreaScriptState::k_waiting) {
    record("AreaContext.Waiting", fmt::format("state={}", area_script.wait_state()));
  }
  return {};
}

void ScenarioStartupController::set_trace_recorder(Startup::StartupTraceRecorder* trace) {
  m_trace = trace;
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

#include "Core/Scenario/ScenarioStartupController.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/GameState.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Interface/RuntimeText.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamCamera.hpp"
#include "Core/Omikron/IamCharacterDefinition.hpp"
#include "Core/Omikron/IamGlobal.hpp"
#include "Core/Omikron/IamObject.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Omikron/IamStart.hpp"
#include "Core/Omikron/IamZone.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "Core/Script/ScriptRuntime.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"
#include "Core/WorldPresentation.hpp"

namespace App {

namespace {

constexpr std::string_view K_IAM_START_PATH{"IAM/START"};
constexpr std::string_view K_IAM_GLOBAL_PATH{"IAM/GLOBAL"};
constexpr std::string_view K_IAM_AREA_PATH{"IAM/AREA"};
constexpr std::string_view K_IAM_SCENE_PATH{"IAM/SCENE"};
constexpr std::string_view K_IAM_OBJECT_PATH{"IAM/OBJECT"};
constexpr std::string_view K_SCPTDATA_DIRECTORY{"SCPTDATA"};
/// Provisional directory for area decor models. The original decor directory
/// is recovered separately from the INI preference system, which is deferred;
/// this path is a best-effort, non-mandatory load.
constexpr std::string_view K_DECOR_DIRECTORY{"MESHES/DECORS"};
/// Extensions appended by the original AREA dependency loader.
constexpr std::string_view K_SCX_EXTENSION{".SCX"};
constexpr std::string_view K_3DO_EXTENSION{".3DO"};

constexpr std::string_view compact_camera_definition_source_name(
    const CompactCameraDefinitionSource source) {
  switch (source) {
    case CompactCameraDefinitionSource::k_area:
      return "AREA";
    case CompactCameraDefinitionSource::k_scene:
      return "SCENE";
    case CompactCameraDefinitionSource::k_global:
      return "GLOBAL";
  }
  return "unknown";
}

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

/// Tests an authored IAM zone polygon against a live Runtime-space actor
/// position. IAM table-2 vertices remain serialized AREA integers, whereas
/// RuntimeCharacter::transform.translation is continuous world-space inches.
///
/// Convert the immutable polygon into Runtime space instead of quantizing the
/// moving actor back into AREA integers; root motion can be fractional.
[[nodiscard]] bool zone_contains_runtime_xz(
    const Omikron::IamZoneRecord& zone, const Runtime::Vec3& position) {
  bool inside{false};
  for (std::size_t current{0}, previous{zone.serialized_vertices.size() - 1U};
      current < zone.serialized_vertices.size();
      previous = current++) {
    const auto& vertex{zone.serialized_vertices.at(current)};
    const auto& prior{zone.serialized_vertices.at(previous)};
    const double vertex_x{static_cast<double>(Runtime::area_position_to_inches(vertex.at(0)))};
    const double vertex_z{static_cast<double>(Runtime::area_position_to_inches(vertex.at(2)))};
    const double prior_x{static_cast<double>(Runtime::area_position_to_inches(prior.at(0)))};
    const double prior_z{static_cast<double>(Runtime::area_position_to_inches(prior.at(2)))};
    const double actor_x{static_cast<double>(position.x)};
    const double actor_z{static_cast<double>(position.z)};
    if ((vertex_z > actor_z) == (prior_z > actor_z)) {
      continue;
    }
    const double left{(actor_x - vertex_x) * (prior_z - vertex_z)};
    const double right{(prior_x - vertex_x) * (actor_z - vertex_z)};
    if ((prior_z > vertex_z) ? (left < right) : (left > right)) {
      inside = !inside;
    }
  }
  return inside;
}

struct PreparedAreaWorld {
  WorldSceneContext* context{nullptr};
  std::optional<std::string> decor_path;
  std::string scenario_path;
  std::string sky_name;
};

/// Derives authored AREA dependencies and prepares one inactive world context.
/// Both initial startup and native transitions use this single path.
std::expected<PreparedAreaWorld, std::string> prepare_area_world(ScenarioManager& manager,
    const std::uint32_t world_scene_id,
    const std::int32_t area_id,
    const Omikron::IamAreaRecord& area_record) {
  const std::string scx_name{area_record.scenario_scx_name()};
  if (scx_name.empty()) {
    return std::expected<PreparedAreaWorld, std::string>{
        std::unexpect, fmt::format("IAM/AREA record {} has no scenario SCX name", area_id)};
  }

  const std::string model_name{area_record.model3do_name()};
  std::optional<std::string> decor_path;
  if (!model_name.empty()) {
    decor_path = dependency_path(K_DECOR_DIRECTORY, model_name, K_3DO_EXTENSION);
  }
  const std::string scenario_path{dependency_path(K_SCPTDATA_DIRECTORY, scx_name, K_SCX_EXTENSION)};

  auto world{manager.load_world_context(world_scene_id, decor_path, scenario_path)};
  if (!world) {
    return std::expected<PreparedAreaWorld, std::string>{
        std::unexpect, fmt::format("world scenario load for AREA {}: {}", area_id, world.error())};
  }

  const auto fail_after_world_load =
      [&manager, world_scene_id](
          std::string error) -> std::expected<PreparedAreaWorld, std::string> {
    if (auto unloaded{manager.unload_world_context(world_scene_id)}; !unloaded) {
      error = fmt::format("{}; rollback failed: {}", error, unloaded.error());
    }
    return std::expected<PreparedAreaWorld, std::string>{std::unexpect, std::move(error)};
  };

  const GameState* const game_state{manager.game_state()};
  ScenarioRuntime* const runtime{world.value()->runtime.get()};
  if (game_state == nullptr || runtime == nullptr) {
    return fail_after_world_load(
        fmt::format("AREA {} object placement owner was not initialized", area_id));
  }
  if (auto materialized{runtime->object_placement_runtime().materialize_area_objects(
          area_id, area_record, *game_state)};
      !materialized) {
    return fail_after_world_load(
        fmt::format("AREA {} object materialization: {}", area_id, materialized.error()));
  }

  return PreparedAreaWorld{.context = world.value(),
      .decor_path = std::move(decor_path),
      .scenario_path = scenario_path,
      .sky_name = area_record.sky_3do_name()};
}

/// Opcodes recorded as provisional bootstrap actions in the startup trace.
bool is_provisional_trace_opcode(const std::uint32_t opcode) {
  switch (opcode) {
    case 0x38:
    case 0x3B:
    case 0x3C:
    case 0x4E:
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
  m_global.reset();
  m_area_archive.reset();
  m_scene_archive.reset();
  m_object_archive.reset();
  for (std::size_t index{0}; index < m_area_slots.size(); ++index) {
    RuntimeAreaSlot& slot{m_area_slots.at(index)};
    slot.scene_script.reset();
    slot.scene.reset();
    slot.primary.reset();
    slot.primary_area_id = -1;
    slot.secondary_area_id = -1;
    slot.scene_id = -1;
    slot.world_scene_id = static_cast<std::uint32_t>(index);
    m_area_scripts.at(index).reset();
    m_area_script_sequences.at(index) = 0;
    m_area_event_started_recorded.at(index) = false;
    m_area_waiting_recorded.at(index) = false;
  }
  m_active_area_slot = 0;
  m_area_transition.reset();
  m_next_area_script_sequence = 1;
  m_next_area_transition_generation = 1;
  m_next_camera_operation_generation = 1;
  m_active_zones.clear();
  m_zone_contacts.clear();
  m_zone_qualification_diagnostics.clear();
  m_start_bytes.clear();
  m_global_bytes.clear();
  m_area_archive_bytes.clear();
  m_scene_archive_bytes.clear();
  m_object_archive_bytes.clear();
  m_initial_area_id = 0;
  m_linked_area_id = 0;
  m_initial_world_scenario_path.clear();
  m_initial_world_decor_path.clear();
  m_initial_world_decor_state.clear();
  m_main_menu_active = false;
  m_active_handle.reset();
  m_last_error.clear();
  m_initialized = false;
  m_manager = nullptr;
  m_dialog_takeover_active = false;
  m_dialog_takeover_id.reset();
  m_ticked = false;
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
  if (auto initialized_game_state{manager.initialize_game_state(start_view)};
      !initialized_game_state) {
    m_last_error = fmt::format("IAM/START persistent state: {}", initialized_game_state.error());
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
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
  GameState* const game_state{manager.game_state()};
  if (game_state == nullptr) {
    m_last_error = "IAM/START persistent state was not retained";
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  if (auto mapped{game_state->set_area_mapping(m_initial_area_id, m_linked_area_id)}; !mapped) {
    m_last_error = mapped.error();
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }

  // 2. Session-global IAM camera definitions. Runtime makes this namespace
  // available before any resident compact context can select a camera.
  auto global_file{read_file(std::string{K_IAM_GLOBAL_PATH})};
  if (!global_file) {
    m_last_error = global_file.error();
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  m_global_bytes = std::move(global_file).value();
  auto global{Omikron::IamGlobal::load(std::span<const std::byte>{m_global_bytes})};
  if (!global) {
    m_last_error = global.error();
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  m_global.emplace(std::move(global).value());
  record("IAM_GLOBAL.Loaded", fmt::format("cameras={}", m_global->cameras().size()));

  // 3. IAM/AREA indexed archive, record <initial area>.
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
  RuntimeAreaSlot& area_slot{m_area_slots.at(m_active_area_slot)};
  area_slot.primary.emplace(std::move(parsed_record).value());
  area_slot.primary_area_id = m_initial_area_id;
  area_slot.secondary_area_id = m_linked_area_id;
  if (auto refreshed{refresh_active_zones()}; !refreshed) {
    m_last_error = fmt::format("initial resident zone refresh: {}", refreshed.error());
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  const Omikron::IamAreaRecord& area_record_view{*area_slot.primary};
  const std::size_t record_size{area_record_view.record_size()};
  const std::uint32_t script_offset{area_record_view.script_offset()};
  record("IAM_AREA.RecordLoaded", fmt::format("id={} size={:#x}", area_id, record_size));
  record("IAM_AREA.Parsed", fmt::format("scriptOffset={:#x}", script_offset));

  // 4. Dependencies in the original loader's order. The names are supplied
  // by the active IAM/AREA record; GRID is merely the value used by area 118,
  // not a special world-scenario role.

  // Decor CPU ownership lives in the world context. Startup retains only the
  // initial dependency path/state for historical diagnostics.
  auto prepared{
      prepare_area_world(manager, area_slot.world_scene_id, m_initial_area_id, area_record_view)};
  if (!prepared) {
    m_last_error = prepared.error();
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  m_initial_world_scenario_path = prepared->scenario_path;
  m_initial_world_decor_path = prepared->decor_path.value_or(std::string{});
  if (m_initial_world_decor_path.empty()) {
    m_initial_world_decor_state = "absent: no model 3DO name in the area record";
    record("AreaDependency.Decor.SkippedUnavailable");
  } else {
    m_initial_world_decor_state = "requested";
    record("AreaDependency.Decor.Requested", m_initial_world_decor_path);
  }

  if (auto result{manager.activate_world_context(area_slot.world_scene_id)}; !result) {
    m_last_error = fmt::format("world activation: {}", result.error());
    App::Log::error(LogCategory::Startup, "Startup failed: {}", m_last_error);
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }

  // Reflect the decor load result in the startup diagnostic without
  // retaining a duplicate parsed model: the model object lives in the world
  // context and is exposed through ScenarioManager.
  if (!m_initial_world_decor_path.empty()) {
    if (prepared->context != nullptr && prepared->context->decor_model.has_value()) {
      m_initial_world_decor_state = "loaded";
      record("AreaDependency.Decor.Loaded", m_initial_world_decor_path);
    } else {
      m_initial_world_decor_state = "unavailable";
      App::Log::warn(LogCategory::Scenario,
          "World decor unavailable (non-fatal): {}",
          m_initial_world_decor_path);
      record("AreaDependency.Decor.Failed", m_initial_world_decor_path);
    }
  }
  record("AreaDependency.Scenario.Loaded",
      fmt::format("slot=world{} path={}", m_active_area_slot, m_initial_world_scenario_path));

  // 5. Area script context: create, queue event/state 1, activate. The
  // first interpreter tick runs in tick().
  const std::size_t area_script_owner_slot{m_active_area_slot};
  Script::AreaScriptRuntime& area_script{
      m_area_scripts.at(area_script_owner_slot).emplace(area_record_view.script_bytes())};
  m_area_script_sequences.at(area_script_owner_slot) = m_next_area_script_sequence++;
  m_area_event_started_recorded.at(area_script_owner_slot) = false;
  m_area_waiting_recorded.at(area_script_owner_slot) = false;
  bind_compact_state_services(area_script, area_script_owner_slot, false);

  area_script.set_area_transition_sink(
      [this, area_script_owner_slot](const Script::AreaTransitionRequest& request) {
        return begin_area_transition(area_script_owner_slot, request);
      });

  area_script.set_area_release_sink([this](const Script::AreaReleaseRequest& request) {
    return release_area(request);
  });
  area_script.set_area_scene_attach_sink([this](const Script::AreaSceneAttachRequest& request) {
    return attach_area_scene(request);
  });
  area_script.set_area_address_placement_sink(
      [this](const Script::AreaAddressPlacementRequest& request) {
        return place_current_character_at_address(request);
      });
  area_script.set_address_flag_sink([this](const Script::AreaAddressFlagRequest& request) {
    return set_address_flag(request);
  });
  area_script.set_persistent_object_collection_sink(
      [this](const Script::AreaPersistentObjectCollectionRequest& request) {
        return add_object_to_persistent_collection(request);
      });
  area_script.set_character_selection_sink(
      [this, area_script_owner_slot](const Script::AreaCharacterSelectionRequest& request) {
        return select_current_character(area_script_owner_slot, request);
      });
  area_script.set_character_deactivation_sink(
      [this, area_script_owner_slot](const Script::AreaCharacterDeactivationRequest& request) {
        return deactivate_owner_character(area_script_owner_slot, request);
      });

  area_script.set_dialog_sink(
      [this](const Script::AreaDialogRequest& request) -> std::expected<void, std::string> {
        return start_compact_dialog(request);
      });

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

  area_script.set_scx_script_sink(
      [this, area_script_owner_slot](const Script::AreaScxScriptRequest& request) {
        return launch_scx_script(area_script_owner_slot, request);
      });

  area_script.set_character_script_sink(
      [this, area_script_owner_slot](const Script::AreaCharacterScriptRequest& request) {
        return launch_character_script(area_script_owner_slot, request);
      });

  area_script.set_character_activation_sink(
      [this, area_script_owner_slot](const Script::AreaCharacterActivationRequest& request) {
        return activate_primary_character(area_script_owner_slot, request);
      });

  area_script.set_presentation_sink([this](const Script::AreaPresentationRequest& request) {
    if (m_manager == nullptr) {
      App::Log::warn(LogCategory::Scenario,
          "AREA presentation mode {} requested without a scenario manager",
          request.mode);
      return;
    }

    m_manager->world_presentation().enqueue_fade(WorldFadeCommand{.mode = request.mode,
        .color = request.color,
        .duration_units = request.duration_units,
        .delay_units = request.delay_units});

    record("AreaScript.PresentationRequested",
        fmt::format("mode={} color={:#010x} duration={} delay={}",
            request.mode,
            request.color,
            request.duration_units,
            request.delay_units));
  });

  area_script.set_cinematic_letterbox_sink(
      [this](const Script::AreaCinematicLetterboxRequest& request) {
        if (m_manager == nullptr) {
          App::Log::warn(LogCategory::Scenario,
              "AREA cinematic letterbox requested without a scenario manager");
          return;
        }

        m_manager->world_presentation().enqueue_letterbox(
            WorldLetterboxCommand{.enabled = request.enabled});
        App::Log::debug(
            LogCategory::Scenario, "cinematic letterbox requested — enabled={}", request.enabled);
      });

  area_script.set_camera_sink(
      [this, area_script_owner_slot](const Script::AreaCameraRequest& request) {
        return enqueue_compact_camera(area_script_owner_slot, request);
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

std::expected<Script::AreaTransitionHandle, std::string>
ScenarioStartupController::begin_area_transition(
    const std::size_t owner_slot, const Script::AreaTransitionRequest& request) {
  if (m_manager == nullptr || !m_area_archive.has_value()) {
    return std::expected<Script::AreaTransitionHandle, std::string>{
        std::unexpect, "AREA transition coordinator is not initialized"};
  }
  if (owner_slot >= m_area_slots.size() || !m_area_slots.at(owner_slot).primary.has_value()) {
    return std::expected<Script::AreaTransitionHandle, std::string>{
        std::unexpect, "AREA transition source is not resident"};
  }
  if (m_area_transition.has_value()) {
    return std::expected<Script::AreaTransitionHandle, std::string>{
        std::unexpect, "AREA transition coordinator is busy"};
  }
  if (request.target_area_id < 0) {
    return std::expected<Script::AreaTransitionHandle, std::string>{
        std::unexpect, fmt::format("target AREA ID {} is negative", request.target_area_id)};
  }

  const std::size_t destination_slot{owner_slot == 0U ? 1U : 0U};
  const Script::AreaTransitionHandle handle{.generation = m_next_area_transition_generation++};
  m_area_transition.emplace(PendingAreaTransition{.handle = handle,
      .request = request,
      .source_slot = owner_slot,
      .destination_slot = destination_slot,
      .error = {}});
  record("AreaTransition.Accepted",
      fmt::format("generation={} sourceSlot={} destinationSlot={} target={}",
          handle.generation,
          owner_slot,
          destination_slot,
          request.target_area_id));
  App::Log::info(LogCategory::Scenario,
      "AREA opcode 0x2F — accepted transition to AREA {} from resident slot {} as generation {}",
      request.target_area_id,
      owner_slot,
      handle.generation);
  return handle;
}

std::expected<std::size_t, std::string> ScenarioStartupController::launch_scx_script(
    const std::size_t owner_slot, const Script::AreaScxScriptRequest& request) {
  if (m_manager == nullptr) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "scenario manager is not available"};
  }
  if (owner_slot >= m_area_slots.size()) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "generic SCX-script owner slot is out of range"};
  }

  const RuntimeAreaSlot& slot{m_area_slots.at(owner_slot)};
  const Omikron::ScxData* const scx{m_manager->world_context_scx(slot.world_scene_id)};
  ScenarioRuntime* const scenario_runtime{m_manager->world_runtime(slot.world_scene_id)};
  const WorldSceneContext* const owner_context{m_manager->find_world_context(slot.world_scene_id)};
  if (scx == nullptr || scenario_runtime == nullptr || owner_context == nullptr) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "generic SCX-script owner has no loaded world SCX runtime"};
  }

  std::optional<std::size_t> source_script_index;
  for (std::size_t index{0}; index < scx->scripts.size(); ++index) {
    if (scx->scripts.at(index).script_id == request.script_id) {
      source_script_index = index;
      break;
    }
  }
  if (!source_script_index.has_value()) {
    return std::expected<std::size_t, std::string>{std::unexpect,
        fmt::format("SCX script ID {} not found in owner world {}",
            request.script_id,
            slot.world_scene_id)};
  }

  auto created{scenario_runtime->spawn_script_instance(source_script_index.value())};
  if (!created) {
    return created;
  }

  // Runtime's generic 0x39/0x3A launch path performs the same post-launch
  // presentation handoff recovered for the character-script launch family:
  // clamp operand B at zero, preserve the current camera state, then select
  // controller mode 13. Mode 13 consumes the live named .3DO camera selected
  // by the structured child.
  const std::int16_t camera_duration_units{
      std::max<std::int16_t>(std::int16_t{0}, request.operand_b)};
  m_manager->world_presentation().enqueue_camera(
      WorldCameraCommand{.kind = WorldCameraCommandKind::k_controller_mode,
          .scene_id = owner_context->scene_id,
          .scene_generation = owner_context->generation,
          .controller_mode = 13U,
          .duration_units = camera_duration_units});

  const Omikron::ScxScript& script{scx->scripts.at(source_script_index.value())};
  record("AreaScript.ScxScriptStarted",
      fmt::format("ownerSlot={} id={} name='{}' instance={} args=({}, {}) cameraMode=13",
          owner_slot,
          request.script_id,
          script.name,
          created.value(),
          request.operand_b,
          request.operand_c));
  App::Log::debug(LogCategory::Script,
      "AREA generic SCX launch — owner slot={} script {} '{}' instance={} cameraMode=13 "
      "duration={}",
      owner_slot,
      request.script_id,
      script.name,
      created.value(),
      camera_duration_units);
  return created;
}

std::expected<void, std::string> ScenarioStartupController::activate_primary_character(
    const std::size_t owner_slot, const Script::AreaCharacterActivationRequest& request) {
  if (request.character_id == -1) {
    return set_current_character_presentation(true);
  }
  if (m_manager == nullptr || owner_slot >= m_area_slots.size()) {
    return std::expected<void, std::string>{
        std::unexpect, "primary AREA character activation has no resident owner"};
  }
  const RuntimeAreaSlot& slot{m_area_slots.at(owner_slot)};
  if (!slot.primary.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, "primary AREA character activation owner has no parsed AREA"};
  }
  ScenarioRuntime* const scenario_runtime{m_manager->world_runtime(slot.world_scene_id)};
  if (scenario_runtime == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, "primary AREA character activation owner has no world runtime"};
  }

  std::expected<void, std::string> activated;
  if (slot.scene.has_value() && slot.scene->character_by_id(request.character_id).has_value()) {
    activated = scenario_runtime->character_runtime().ensure_scene_character(
        slot.primary_area_id, slot.scene_id, *slot.scene, request.character_id);
    if (activated) {
      activated = scenario_runtime->character_runtime().set_presentation_enabled(
          request.character_id, true);
    }
  } else {
    activated = scenario_runtime->activate_character(slot.primary_area_id, *slot.primary, request);
  }
  if (!activated) {
    return activated;
  }

  if (const Character::RuntimeCharacter* const character{
          scenario_runtime->character_runtime().find(request.character_id)};
      character != nullptr) {
    record("AreaScript.CharacterActivated",
        fmt::format("character={} model={} area={}",
            character->character_id,
            character->model_resource_name,
            character->area_id));
    App::Log::info(LogCategory::Scenario,
        "character activated — id={} model={} area={}",
        character->character_id,
        character->model_resource_name,
        character->area_id);
  }
  return {};
}

std::expected<void, std::string> ScenarioStartupController::install_primary_area_script(
    const std::size_t owner_slot) {
  if (m_manager == nullptr || owner_slot >= m_area_slots.size()) {
    return std::expected<void, std::string>{
        std::unexpect, "cannot install primary AREA script without a resident owner"};
  }
  RuntimeAreaSlot& slot{m_area_slots.at(owner_slot)};
  if (!slot.primary.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, "cannot install primary AREA script without a parsed AREA record"};
  }

  m_area_scripts.at(owner_slot).reset();
  m_area_script_sequences.at(owner_slot) = 0;
  m_area_event_started_recorded.at(owner_slot) = false;
  m_area_waiting_recorded.at(owner_slot) = false;

  // A zero primary-event pointer is a legitimate resident AREA with no event-1
  // bytecode. Do not point AreaScriptRuntime at the beginning of the AREA header.
  if (slot.primary->script_offset() == 0U) {
    return {};
  }

  Script::AreaScriptRuntime& script{
      m_area_scripts.at(owner_slot).emplace(slot.primary->script_bytes())};
  m_area_script_sequences.at(owner_slot) = m_next_area_script_sequence++;
  bind_scene_compact_services(script, owner_slot, false);
  script.set_area_transition_sink([this, owner_slot](const Script::AreaTransitionRequest& request) {
    return begin_area_transition(owner_slot, request);
  });
  script.set_character_activation_sink(
      [this, owner_slot](const Script::AreaCharacterActivationRequest& request) {
        return activate_primary_character(owner_slot, request);
      });
  script.set_instruction_sink(
      [this, owner_slot](const std::uint32_t opcode, const std::vector<std::int32_t>& operands) {
        if (opcode == 0x0D) {
          const std::int32_t index{operands.empty() ? 0 : operands.front()};
          record("AreaScript.VariableSet",
              fmt::format("area={} slot={} index={} value=1",
                  m_area_slots.at(owner_slot).primary_area_id,
                  owner_slot,
                  index));
        } else if (opcode == 0x0E) {
          const std::int32_t index{operands.empty() ? 0 : operands.at(0)};
          const std::int32_t value{operands.size() >= 2 ? operands.at(1) : 0};
          record("AreaScript.VariableSet",
              fmt::format("area={} slot={} index={} value={}",
                  m_area_slots.at(owner_slot).primary_area_id,
                  owner_slot,
                  index,
                  value));
        } else if (is_provisional_trace_opcode(opcode)) {
          record("AreaScript.BootstrapOpcode",
              fmt::format("area={} slot={} opcode={:#x}",
                  m_area_slots.at(owner_slot).primary_area_id,
                  owner_slot,
                  opcode));
        }
      });

  script.queue_event(1);
  script.activate();
  record("AreaContext.Created",
      fmt::format("area={} slot={} sequence={}",
          slot.primary_area_id,
          owner_slot,
          m_area_script_sequences.at(owner_slot)));
  record("AreaContext.EventQueued",
      fmt::format("area={} slot={} event=1", slot.primary_area_id, owner_slot));
  App::Log::info(LogCategory::Script,
      "AREA {} resident primary event 1 queued — slot={} sequence={}",
      slot.primary_area_id,
      owner_slot,
      m_area_script_sequences.at(owner_slot));
  return {};
}

std::expected<void, std::string> ScenarioStartupController::service_area_transition() {
  if (!m_area_transition.has_value()) {
    return {};
  }

  PendingAreaTransition& transition{m_area_transition.value()};
  if (!transition.error.empty()) {
    return std::expected<void, std::string>{std::unexpect, transition.error};
  }

  const auto fail_transition = [this, &transition](
                                   std::string error) -> std::expected<void, std::string> {
    transition.error =
        fmt::format("AREA transition to {} failed: {}", transition.request.target_area_id, error);
    m_last_error = transition.error;
    record("AreaTransition.Failed", transition.error);
    App::Log::error(LogCategory::Scenario, "{}", transition.error);
    return std::expected<void, std::string>{std::unexpect, transition.error};
  };

  if (m_manager == nullptr || !m_area_archive.has_value()) {
    return fail_transition("coordinator ownership is unavailable");
  }
  if (transition.source_slot >= m_area_slots.size()) {
    return fail_transition("requesting resident AREA context has an invalid owner slot");
  }
  std::optional<Script::AreaScriptRuntime>& source_script{
      m_area_scripts.at(transition.source_slot)};
  if (!source_script.has_value()) {
    return fail_transition("requesting resident AREA context no longer exists");
  }
  if (transition.source_slot != m_active_area_slot) {
    return fail_transition("active resident AREA slot changed before commit");
  }

  Script::AreaScriptRuntime& area_script{source_script.value()};
  if (area_script.state() != Script::AreaScriptState::k_waiting ||
      area_script.wait_info().kind != Script::AreaWaitKind::k_area_transition ||
      !area_script.wait_info().area_transition_handle.has_value() ||
      area_script.wait_info().area_transition_handle.value() != transition.handle) {
    return fail_transition("requesting AREA context is not waiting on the accepted generation");
  }

  auto record_span{
      m_area_archive->read_record(static_cast<std::uint32_t>(transition.request.target_area_id))};
  if (!record_span) {
    return fail_transition(record_span.error());
  }
  auto parsed_record{Omikron::IamAreaRecord::load(record_span.value())};
  if (!parsed_record) {
    return fail_transition(parsed_record.error());
  }

  Omikron::IamAreaRecord destination_record{std::move(parsed_record).value()};
  RuntimeAreaSlot& destination_slot{m_area_slots.at(transition.destination_slot)};
  auto prepared{prepare_area_world(*m_manager,
      destination_slot.world_scene_id,
      transition.request.target_area_id,
      destination_record)};
  if (!prepared) {
    return fail_transition(prepared.error());
  }

  record("AreaTransition.TargetPrepared",
      fmt::format("generation={} area={} slot={} model='{}' scx='{}' sky='{}'",
          transition.handle.generation,
          transition.request.target_area_id,
          transition.destination_slot,
          destination_record.model3do_name(),
          destination_record.scenario_scx_name(),
          destination_record.sky_3do_name()));
  if (!prepared->sky_name.empty()) {
    record("AreaTransition.SkyPreserved", prepared->sky_name);
  }

  destination_slot.primary.emplace(std::move(destination_record));
  destination_slot.primary_area_id = transition.request.target_area_id;
  destination_slot.secondary_area_id = -1;
  destination_slot.scene.reset();
  destination_slot.scene_id = -1;
  destination_slot.scene_script.reset();

  // Runtime creates the destination AREA's compact context as part of loading
  // the resident AREA and queues event 1 immediately. It does not replace the
  // source context: both remain registered until the source explicitly runs
  // 0x30. The scheduler below services the older source registration first,
  // so the concrete AREA118 -> AREA222 path performs 0x47 before AREA222's
  // event 1 launches IMPASSE script 20 and switches the camera to mode 13.
  if (auto installed{install_primary_area_script(transition.destination_slot)}; !installed) {
    return fail_transition(fmt::format("destination primary context: {}", installed.error()));
  }
  if (auto refreshed{refresh_active_zones()}; !refreshed) {
    return fail_transition(fmt::format("resident zone refresh: {}", refreshed.error()));
  }

  const Script::AreaTransitionHandle completed_handle{transition.handle};
  const std::int16_t target_area_id{transition.request.target_area_id};
  if (auto completed{area_script.complete_area_transition(completed_handle)}; !completed) {
    return fail_transition(completed.error());
  }

  record("AreaTransition.Prepared",
      fmt::format("generation={} sourceSlot={} destinationSlot={} target={} resumeIp={:#x}",
          completed_handle.generation,
          transition.source_slot,
          transition.destination_slot,
          target_area_id,
          area_script.instruction_pointer()));
  App::Log::info(LogCategory::Scenario,
      "AREA {} prepared in resident slot {} with primary context — source resumes at ip={:#x}",
      target_area_id,
      transition.destination_slot,
      area_script.instruction_pointer());
  App::Log::debug(LogCategory::Scenario,
      "AREA transition generation {} prepared from slot {} to slot {}",
      completed_handle.generation,
      transition.source_slot,
      transition.destination_slot);
  m_area_transition.reset();
  return {};
}

std::optional<std::size_t> ScenarioStartupController::resident_area_slot(
    const std::int32_t area_id) const {
  for (std::size_t index{0}; index < m_area_slots.size(); ++index) {
    const RuntimeAreaSlot& slot{m_area_slots.at(index)};
    if (slot.primary.has_value() && slot.primary_area_id == area_id) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<ResolvedCompactCamera> ScenarioStartupController::resolve_compact_camera(
    const std::int16_t camera_id) const {
  for (std::size_t index{0}; index < m_area_slots.size(); ++index) {
    const RuntimeAreaSlot& slot{m_area_slots.at(index)};
    if (slot.primary.has_value()) {
      const std::optional<Omikron::IamCameraRecord> camera{slot.primary->camera_by_id(camera_id)};
      if (camera.has_value()) {
        return ResolvedCompactCamera{.camera = camera.value(),
            .source = CompactCameraDefinitionSource::k_area,
            .resident_slot = index,
            .area_id = slot.primary_area_id,
            .scene_id = -1};
      }
    }
    if (slot.scene.has_value()) {
      const std::optional<Omikron::IamCameraRecord> camera{slot.scene->camera_by_id(camera_id)};
      if (camera.has_value()) {
        return ResolvedCompactCamera{.camera = camera.value(),
            .source = CompactCameraDefinitionSource::k_scene,
            .resident_slot = index,
            .area_id = slot.primary_area_id,
            .scene_id = slot.scene_id};
      }
    }
  }

  if (m_global.has_value()) {
    const std::optional<Omikron::IamCameraRecord> camera{m_global->camera_by_id(camera_id)};
    if (camera.has_value()) {
      return ResolvedCompactCamera{.camera = camera.value(),
          .source = CompactCameraDefinitionSource::k_global,
          .resident_slot = std::nullopt,
          .area_id = -1,
          .scene_id = -1};
    }
  }
  return std::nullopt;
}

std::expected<std::optional<Script::AreaCameraOperationHandle>, std::string>
ScenarioStartupController::enqueue_compact_camera(
    const std::size_t owner_slot, const Script::AreaCameraRequest& request) {
  if (m_manager == nullptr || owner_slot >= m_area_slots.size()) {
    const std::string error{
        fmt::format("compact camera {} requested without a resident owner", request.camera_id)};
    App::Log::warn(LogCategory::Scenario, "{}", error);
    return std::expected<std::optional<Script::AreaCameraOperationHandle>, std::string>{
        std::unexpect, error};
  }
  const RuntimeAreaSlot& slot{m_area_slots.at(owner_slot)};
  const std::int16_t camera_id{static_cast<std::int16_t>(request.camera_id)};
  const std::optional<ResolvedCompactCamera> resolved{resolve_compact_camera(camera_id)};
  const WorldSceneContext* context{m_manager->find_world_context(slot.world_scene_id)};
  if (!resolved.has_value()) {
    App::Log::debug(LogCategory::Scenario,
        "compact camera {} resolved to no-op because no authored AREA/SCENE/GLOBAL record was "
        "found",
        request.camera_id);
    return std::expected<std::optional<Script::AreaCameraOperationHandle>, std::string>{
        std::in_place, std::nullopt};
  }
  if (context == nullptr) {
    const std::string error{
        fmt::format("compact camera {} requested without an owner world", request.camera_id)};
    App::Log::warn(LogCategory::Scenario, "{}", error);
    return std::expected<std::optional<Script::AreaCameraOperationHandle>, std::string>{
        std::unexpect, error};
  }

  const Omikron::IamCameraRecord camera{resolved->camera};
  if (request.wait_for_completion && camera.camera_type != 12U) {
    return std::expected<std::optional<Script::AreaCameraOperationHandle>, std::string>{
        std::unexpect,
        fmt::format("tracked camera {} uses unsupported controller mode {}",
            request.camera_id,
            camera.camera_type)};
  }

  if (resolved.has_value()) {
    switch (resolved->source) {
      case CompactCameraDefinitionSource::k_area:
        App::Log::debug(LogCategory::Scenario,
            "compact camera {} resolved from AREA slot={} area={}",
            request.camera_id,
            resolved->resident_slot.value_or(0U),
            resolved->area_id);
        break;
      case CompactCameraDefinitionSource::k_scene:
        App::Log::debug(LogCategory::Scenario,
            "compact camera {} resolved from SCENE slot={} scene={} area={}",
            request.camera_id,
            resolved->resident_slot.value_or(0U),
            resolved->scene_id,
            resolved->area_id);
        break;
      case CompactCameraDefinitionSource::k_global:
        App::Log::debug(
            LogCategory::Scenario, "compact camera {} resolved from IAM/GLOBAL", request.camera_id);
        break;
    }
  }

  const std::optional<std::uint64_t> operation_generation{
      request.wait_for_completion
          ? std::optional<std::uint64_t>{m_next_camera_operation_generation++}
          : std::nullopt};
  const std::optional<ControlledCharacterRef> controlled{m_manager->controlled_character()};
  const std::int16_t participant_a_character_id{
      static_cast<std::int16_t>(controlled.has_value() ? controlled->character_id : -1)};
  const WorldCameraAttachmentParticipants participants{
      .participant_a_character_id = participant_a_character_id, .participant_b_character_id = -1};

  // Preserve serialized vectors/selectors and the controlled participant ID
  // captured by Runtime's camera controller at submission. WorldCameraSystem
  // resolves that stable identity to a live pose on every update.
  m_manager->world_presentation().enqueue_camera(WorldCameraCommand{.scene_id = context->scene_id,
      .scene_generation = context->generation,
      .camera_id = request.camera_id,
      .source_area_id = slot.primary_area_id,
      .operation_generation = operation_generation,
      .attachment_participants = participants,
      .serialized_eye = camera.serialized_eye,
      .serialized_target = camera.serialized_target,
      .runtime_eye = Runtime::iam_camera_vector_to_runtime(camera.serialized_eye),
      .runtime_target = Runtime::iam_camera_vector_to_runtime(camera.serialized_target),
      .duration_units = request.duration_units,
      .flags = request.flags,
      .wait_for_completion = request.wait_for_completion,
      .camera_type = camera.camera_type,
      .roll_units = camera.roll_units,
      .horizontal_fov_units = camera.horizontal_fov_units,
      .roll_degrees = Runtime::area_angle_to_degrees(camera.roll_units),
      .horizontal_fov_degrees = Runtime::area_angle_to_degrees(camera.horizontal_fov_units),
      .target_attachment_selector = camera.target_attachment_selector,
      .eye_attachment_selector = camera.eye_attachment_selector,
      .tail_fields = camera.tail_fields});
  const std::string camera_source_name{resolved.has_value()
                                          ? std::string{compact_camera_definition_source_name(
                                              resolved->source)}
                                          : std::string{"ACTIVE"}};
  record("AreaScript.CameraRequested",
      fmt::format("id={} duration={} flags={} type={} hFov={}deg source={} operation={}",
          request.camera_id,
          request.duration_units,
          request.flags,
          camera.camera_type,
          Runtime::area_angle_to_degrees(camera.horizontal_fov_units),
          camera_source_name,
          operation_generation.has_value() ? fmt::format("{}", operation_generation.value())
                                           : std::string{"none"}));
  return std::optional<Script::AreaCameraOperationHandle>{
      operation_generation.has_value()
          ? std::optional<Script::AreaCameraOperationHandle>{
                Script::AreaCameraOperationHandle{.generation = operation_generation.value()}}
          : std::nullopt};
}

void ScenarioStartupController::bind_compact_state_services(Script::AreaScriptRuntime& runtime,
    const std::size_t owner_slot,
    const bool prefer_scene_definition) {
  runtime.set_global_variable_read_sink(
      [this](const std::uint16_t id) -> std::expected<std::int32_t, std::string> {
        if (m_manager == nullptr || m_manager->game_state() == nullptr) {
          return std::expected<std::int32_t, std::string>{
              std::unexpect, "session game state is not initialized"};
        }
        return m_manager->game_state()->global_variable(id);
      });
  runtime.set_global_variable_write_sink(
      [this](const std::uint16_t id, const std::int32_t value) -> std::expected<void, std::string> {
        if (m_manager == nullptr || m_manager->game_state() == nullptr) {
          return std::expected<void, std::string>{
              std::unexpect, "session game state is not initialized"};
        }
        return m_manager->game_state()->set_global_variable(id, value);
      });
  runtime.set_global_variable_snapshot_sink([this]() -> std::span<const std::int32_t> {
    if (m_manager == nullptr || m_manager->game_state() == nullptr) {
      return {};
    }
    return m_manager->game_state()->global_variables();
  });
  runtime.set_zone_activation_sink([this](const Script::AreaZoneActivationRequest& request) {
    return set_zone_activation(request);
  });
  runtime.set_object_placement_state_sink(
      [this, owner_slot](const Script::AreaObjectPlacementStateRequest& request) {
        return set_object_placement_state(owner_slot, request);
      });
  runtime.set_object_activation_sink([this](const Script::AreaObjectActivationRequest& request) {
    return present_compact_object(request);
  });
  runtime.set_current_character_move_sink(
      [this](const Script::AreaCurrentCharacterMoveRequest& request) {
        return select_current_character_move(request);
      });
  runtime.set_current_character_controller_sink(
      [this](const Script::AreaCurrentCharacterControllerRequest& request) {
        return set_current_character_controller(request);
      });
  runtime.set_character_value_read_sink(
      [this, owner_slot, prefer_scene_definition](const Script::AreaCharacterValueRequest& request)
          -> std::expected<std::int32_t, std::string> {
        return character_value(owner_slot, prefer_scene_definition, request);
      });
  runtime.set_character_value_write_sink(
      [this, owner_slot, prefer_scene_definition](const Script::AreaCharacterValueRequest& request,
          const std::int32_t value) -> std::expected<void, std::string> {
        return set_character_value(owner_slot, prefer_scene_definition, request, value);
      });
}

std::expected<std::int16_t, std::string> ScenarioStartupController::ensure_character_value_profile(
    const std::size_t owner_slot,
    const bool prefer_scene_definition,
    const std::int16_t requested_character_id) {
  if (m_manager == nullptr || m_manager->game_state() == nullptr) {
    return std::expected<std::int16_t, std::string>{
        std::unexpect, "session game state is not initialized"};
  }

  std::int16_t character_id{requested_character_id};
  std::size_t definition_owner_slot{owner_slot};
  bool scene_first{prefer_scene_definition};
  if (requested_character_id == -1) {
    const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
    if (!current.has_value()) {
      return std::expected<std::int16_t, std::string>{
          std::unexpect, "current controlled character is not established"};
    }
    character_id = current->character_id;
    scene_first = false;
    auto* const resident{
        std::ranges::find(m_area_slots, current->world_scene_id, &RuntimeAreaSlot::world_scene_id)};
    if (resident == m_area_slots.end()) {
      return std::expected<std::int16_t, std::string>{std::unexpect,
          fmt::format("current character {} world {} has no resident AREA owner",
              character_id,
              current->world_scene_id)};
    }
    definition_owner_slot = static_cast<std::size_t>(std::distance(m_area_slots.begin(), resident));
  }

  GameState& game_state{*m_manager->game_state()};
  if (game_state.has_character_profile(character_id)) {
    return character_id;
  }
  if (definition_owner_slot >= m_area_slots.size()) {
    return std::expected<std::int16_t, std::string>{
        std::unexpect, "character-value owner AREA slot is out of range"};
  }

  const RuntimeAreaSlot& slot{m_area_slots.at(definition_owner_slot)};
  const auto area_values =
      [&slot, character_id]() -> std::optional<Omikron::IamCharacterValueInitialState> {
    if (!slot.primary.has_value()) {
      return std::nullopt;
    }
    const auto definition{slot.primary->character_definition_by_character_id(character_id)};
    return definition.has_value()
               ? std::optional<Omikron::IamCharacterValueInitialState>{definition->values}
               : std::nullopt;
  };
  const auto scene_values =
      [&slot, character_id]() -> std::optional<Omikron::IamCharacterValueInitialState> {
    if (!slot.scene.has_value()) {
      return std::nullopt;
    }
    const auto definition{slot.scene->character_definition_by_character_id(character_id)};
    return definition.has_value()
               ? std::optional<Omikron::IamCharacterValueInitialState>{definition->values}
               : std::nullopt;
  };

  std::optional<Omikron::IamCharacterValueInitialState> initial_values;
  if (scene_first) {
    initial_values = scene_values();
    if (!initial_values.has_value()) {
      initial_values = area_values();
    }
  } else {
    initial_values = area_values();
    if (!initial_values.has_value()) {
      initial_values = scene_values();
    }
  }
  if (!initial_values.has_value()) {
    return std::expected<std::int16_t, std::string>{std::unexpect,
        fmt::format("character {} has no definition in compact owner slot {}",
            character_id,
            definition_owner_slot)};
  }

  game_state.ensure_character_profile(character_id, initial_values.value());
  return character_id;
}

std::expected<std::int32_t, std::string> ScenarioStartupController::character_value(
    const std::size_t owner_slot,
    const bool prefer_scene_definition,
    const Script::AreaCharacterValueRequest& request) {
  auto character_id{
      ensure_character_value_profile(owner_slot, prefer_scene_definition, request.character_id)};
  if (!character_id) {
    return std::expected<std::int32_t, std::string>{std::unexpect, character_id.error()};
  }
  return m_manager->game_state()->character_value(character_id.value(), request.value_kind);
}

std::expected<void, std::string> ScenarioStartupController::set_character_value(
    const std::size_t owner_slot,
    const bool prefer_scene_definition,
    const Script::AreaCharacterValueRequest& request,
    const std::int32_t value) {
  auto character_id{
      ensure_character_value_profile(owner_slot, prefer_scene_definition, request.character_id)};
  if (!character_id) {
    return std::expected<void, std::string>{std::unexpect, character_id.error()};
  }
  return m_manager->game_state()->set_character_value(
      character_id.value(), request.value_kind, value);
}

void ScenarioStartupController::bind_scene_compact_services(Script::AreaScriptRuntime& runtime,
    const std::size_t owner_slot,
    const bool prefer_scene_definition) {
  bind_compact_state_services(runtime, owner_slot, prefer_scene_definition);
  runtime.set_area_release_sink([this](const Script::AreaReleaseRequest& request) {
    return release_area(request);
  });
  runtime.set_area_scene_attach_sink([this](const Script::AreaSceneAttachRequest& request) {
    return attach_area_scene(request);
  });
  runtime.set_area_address_placement_sink(
      [this](const Script::AreaAddressPlacementRequest& request) {
        return place_current_character_at_address(request);
      });
  runtime.set_address_flag_sink([this](const Script::AreaAddressFlagRequest& request) {
    return set_address_flag(request);
  });
  runtime.set_persistent_object_collection_sink(
      [this](const Script::AreaPersistentObjectCollectionRequest& request) {
        return add_object_to_persistent_collection(request);
      });
  runtime.set_character_selection_sink(
      [this, owner_slot](const Script::AreaCharacterSelectionRequest& request) {
        return select_current_character(owner_slot, request);
      });
  runtime.set_character_deactivation_sink(
      [this, owner_slot](const Script::AreaCharacterDeactivationRequest& request) {
        return deactivate_owner_character(owner_slot, request);
      });
  runtime.set_dialog_sink(
      [this](const Script::AreaDialogRequest& request) -> std::expected<void, std::string> {
        return start_compact_dialog(request);
      });
  runtime.set_music_sink([this](const Audio::MusicTrackRequest& request) {
    if (m_audio != nullptr) {
      if (auto result{m_audio->play_music_track(request)}; !result) {
        App::Log::warn(
            LogCategory::Music, "SCENE music {} failed: {}", request.track_id, result.error());
      }
    }
  });
  runtime.set_camera_sink([this, owner_slot](const Script::AreaCameraRequest& request) {
    return enqueue_compact_camera(owner_slot, request);
  });
  runtime.set_presentation_sink([this](const Script::AreaPresentationRequest& request) {
    if (m_manager == nullptr) {
      return;
    }
    m_manager->world_presentation().enqueue_fade(WorldFadeCommand{.mode = request.mode,
        .color = request.color,
        .duration_units = request.duration_units,
        .delay_units = request.delay_units});
  });
  runtime.set_cinematic_letterbox_sink(
      [this](const Script::AreaCinematicLetterboxRequest& request) {
        if (m_manager == nullptr) {
          return;
        }
        m_manager->world_presentation().enqueue_letterbox(
            WorldLetterboxCommand{.enabled = request.enabled});
      });
  runtime.set_scx_script_sink([this, owner_slot](const Script::AreaScxScriptRequest& request) {
    return launch_scx_script(owner_slot, request);
  });
  runtime.set_character_script_sink(
      [this, owner_slot](const Script::AreaCharacterScriptRequest& request)
          -> std::expected<std::size_t, std::string> {
        return launch_character_script(owner_slot, request);
      });
  runtime.set_character_activation_sink([this, owner_slot, prefer_scene_definition](
                                            const Script::AreaCharacterActivationRequest& request)
                                            -> std::expected<void, std::string> {
    if (request.character_id == -1) {
      return set_current_character_presentation(true);
    }
    if (m_manager == nullptr) {
      return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
    }
    const RuntimeAreaSlot& slot{m_area_slots.at(owner_slot)};
    ScenarioRuntime* scenario_runtime{m_manager->world_runtime(slot.world_scene_id)};
    if (scenario_runtime == nullptr || !slot.primary.has_value()) {
      return std::expected<void, std::string>{
          std::unexpect, "SCENE owner has no AREA character runtime"};
    }
    if (prefer_scene_definition && slot.scene.has_value() &&
        slot.scene->character_by_id(request.character_id).has_value()) {
      if (auto activated{scenario_runtime->character_runtime().ensure_scene_character(
              slot.primary_area_id, slot.scene_id, *slot.scene, request.character_id)};
          !activated) {
        return activated;
      }
      return scenario_runtime->character_runtime().set_presentation_enabled(
          request.character_id, true);
    }
    return scenario_runtime->activate_character(slot.primary_area_id, *slot.primary, request);
  });
  runtime.set_interface_sink(
      [this](const InterfaceOpenRequest& request) -> std::expected<InterfaceHandle, std::string> {
        return m_dispatcher.open(request);
      });
}

std::expected<void, std::string> ScenarioStartupController::attach_area_scene(
    const Script::AreaSceneAttachRequest& request) {
  if (m_manager == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  if (request.scene_id < 0) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("SCENE ID {} is negative", request.scene_id)};
  }

  GameState* const game_state{m_manager->game_state()};
  if (game_state == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "session game state is not initialized"};
  }

  const auto slot_index{resident_area_slot(request.area_id)};
  if (!slot_index.has_value()) {
    // Runtime 0x00403950 always commits the AREA -> SCENE mapping through
    // 0x0040B120, even when neither of its two resident AREA slots matches.
    // The SCENE loader/materializer lives inside the resident-slot match, so a
    // nonresident AREA must not touch IAM/SCENE yet: the AREA loader consumes
    // this mapping if/when that AREA becomes resident later.
    if (auto mapped{game_state->set_area_mapping(request.area_id, request.scene_id)}; !mapped) {
      return mapped;
    }
    if (auto refreshed{refresh_active_zones()}; !refreshed) {
      return refreshed;
    }
    record("AreaScript.AreaSceneMapped",
        fmt::format("area={} scene={} resident=false", request.area_id, request.scene_id));
    App::Log::debug(LogCategory::Scenario,
        "AREA {} selected SCENE {} for future residency",
        request.area_id,
        request.scene_id);
    return {};
  }

  if (!m_scene_archive.has_value()) {
    auto archive_bytes{read_file(std::string{K_IAM_SCENE_PATH})};
    if (!archive_bytes) {
      return std::expected<void, std::string>{std::unexpect, archive_bytes.error()};
    }
    m_scene_archive_bytes = std::move(archive_bytes).value();
    m_scene_archive.emplace(std::span<const std::byte>{m_scene_archive_bytes});
  }
  auto record_bytes{m_scene_archive->read_record(static_cast<std::uint32_t>(request.scene_id))};
  if (!record_bytes) {
    return std::expected<void, std::string>{std::unexpect, record_bytes.error()};
  }
  auto parsed_scene{Omikron::IamSceneRecord::load(record_bytes.value())};
  if (!parsed_scene) {
    return std::expected<void, std::string>{std::unexpect, parsed_scene.error()};
  }

  RuntimeAreaSlot& slot{m_area_slots.at(slot_index.value())};
  if (!slot.primary.has_value()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("AREA {} resident slot has no parsed AREA record", request.area_id)};
  }
  ScenarioRuntime* scenario_runtime{m_manager->world_runtime(slot.world_scene_id)};
  if (scenario_runtime == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("AREA {} has no prepared world runtime", request.area_id)};
  }

  if (slot.scene.has_value()) {
    // The compact context must release its borrowed byte span before the owned
    // scene record is replaced, then only SCENE-owned entities are removed.
    slot.scene_script.reset();
    std::optional<std::int16_t> preserved_character_id;
    if (const auto current{m_manager->controlled_character()};
        current.has_value() && current->world_scene_id == slot.world_scene_id) {
      preserved_character_id = current->character_id;
    }
    scenario_runtime->character_runtime().dematerialize_scene_characters(
        slot.primary_area_id, slot.scene_id, preserved_character_id);
    scenario_runtime->object_placement_runtime().dematerialize_scene_objects(
        slot.primary_area_id, slot.scene_id);
    slot.scene.reset();
    slot.scene_id = -1;
  }

  slot.scene.emplace(std::move(parsed_scene).value());
  slot.scene_id = request.scene_id;
  const std::size_t source_slot{m_active_area_slot};
  const std::uint32_t source_world_scene_id{m_area_slots.at(source_slot).world_scene_id};
  const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
  const bool transfer_current{source_slot != slot_index.value() && current.has_value() &&
                              current->world_scene_id == source_world_scene_id};
  if (transfer_current) {
    if (auto transferred{
            m_manager->transfer_controlled_character(source_world_scene_id, slot.world_scene_id)};
        !transferred) {
      slot.scene.reset();
      slot.scene_id = -1;
      return transferred;
    }
  }
  if (auto materialized{scenario_runtime->character_runtime().preload_scene_characters(
          slot.primary_area_id, slot.scene_id, *slot.scene)};
      !materialized) {
    const std::string preload_error{std::move(materialized).error()};
    if (transfer_current) {
      if (auto returned{
              m_manager->transfer_controlled_character(slot.world_scene_id, source_world_scene_id)};
          !returned) {
        return std::expected<void, std::string>{std::unexpect,
            fmt::format("SCENE character preload failed: {}; current-character rollback failed: {}",
                preload_error,
                returned.error())};
      }
    }
    scenario_runtime->character_runtime().dematerialize_scene_characters(
        slot.primary_area_id, slot.scene_id);

    slot.scene.reset();
    slot.scene_id = -1;
    return std::expected<void, std::string>{std::unexpect, preload_error};
  }
  if (auto materialized_objects{
          scenario_runtime->object_placement_runtime().materialize_scene_objects(
              slot.primary_area_id, slot.scene_id, *slot.scene, *game_state)};
      !materialized_objects) {
    const std::string materialize_error{std::move(materialized_objects).error()};
    scenario_runtime->object_placement_runtime().dematerialize_scene_objects(
        slot.primary_area_id, slot.scene_id);
    scenario_runtime->character_runtime().dematerialize_scene_characters(
        slot.primary_area_id, slot.scene_id);
    if (transfer_current) {
      if (auto returned{
              m_manager->transfer_controlled_character(slot.world_scene_id, source_world_scene_id)};
          !returned) {
        return std::expected<void, std::string>{std::unexpect,
            fmt::format(
                "SCENE object materialization failed: {}; current-character rollback failed: {}",
                materialize_error,
                returned.error())};
      }
    }
    slot.scene.reset();
    slot.scene_id = -1;
    return std::expected<void, std::string>{std::unexpect, materialize_error};
  }
  if (slot.scene->script_offset() != 0U) {
    slot.scene_script.emplace(slot.scene->script_bytes());
    bind_scene_compact_services(*slot.scene_script, slot_index.value());
    slot.scene_script->queue_event(1);
    slot.scene_script->activate();
    App::Log::info(LogCategory::Script, "SCENE {} event 1 queued", request.scene_id);
  }

  if (source_slot != slot_index.value()) {
    if (auto switched{
            m_manager->switch_active_world_context(source_world_scene_id, slot.world_scene_id)};
        !switched) {
      const std::string switch_error{std::move(switched).error()};
      if (transfer_current) {
        if (auto returned{m_manager->transfer_controlled_character(
                slot.world_scene_id, source_world_scene_id)};
            !returned) {
          return std::expected<void, std::string>{std::unexpect,
              fmt::format(
                  "world residency switch failed: {}; current-character rollback failed: {}",
                  switch_error,
                  returned.error())};
        }
      }
      slot.scene_script.reset();
      scenario_runtime->character_runtime().dematerialize_scene_characters(
          slot.primary_area_id, slot.scene_id);
      scenario_runtime->object_placement_runtime().dematerialize_scene_objects(
          slot.primary_area_id, slot.scene_id);
      slot.scene.reset();
      slot.scene_id = -1;
      return std::expected<void, std::string>{std::unexpect, switch_error};
    }
    m_active_area_slot = slot_index.value();
  }
  if (auto mapped{game_state->set_area_mapping(request.area_id, request.scene_id)}; !mapped) {
    return mapped;
  }
  game_state->set_current_area(request.area_id);
  if (auto refreshed{refresh_active_zones()}; !refreshed) {
    return refreshed;
  }
  const Omikron::IamSceneRecord& scene{slot.scene.value()};
  App::Log::info(LogCategory::Scenario,
      "AREA {} attached SCENE {} — chars={} objects={} zones={} cameras={}",
      request.area_id,
      request.scene_id,
      scene.table_count(0),
      scene.table_count(1),
      scene.table_count(2),
      scene.table_count(6));
  return {};
}

std::expected<void, std::string> ScenarioStartupController::release_area(
    const Script::AreaReleaseRequest& request) {
  if (m_manager == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  const auto slot_index{resident_area_slot(request.area_id)};
  if (!slot_index.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("AREA {} is not resident for release", request.area_id)};
  }
  if (slot_index.value() == m_active_area_slot) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("AREA {} is currently active and cannot be released", request.area_id)};
  }
  RuntimeAreaSlot& slot{m_area_slots.at(slot_index.value())};
  if (const auto current{m_manager->controlled_character()};
      current.has_value() && current->world_scene_id == slot.world_scene_id) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("AREA {} cannot be released because it owns current controlled character {}",
            request.area_id,
            current->character_id)};
  }
  if (ScenarioRuntime* runtime{m_manager->world_runtime(slot.world_scene_id)};
      runtime != nullptr && slot.scene.has_value()) {
    slot.scene_script.reset();
    runtime->character_runtime().dematerialize_scene_characters(
        slot.primary_area_id, slot.scene_id);
    runtime->object_placement_runtime().dematerialize_scene_objects(
        slot.primary_area_id, slot.scene_id);
  }
  slot.scene_script.reset();
  slot.scene.reset();
  slot.scene_id = -1;
  if (auto unloaded{m_manager->unload_world_context(slot.world_scene_id)}; !unloaded) {
    return unloaded;
  }
  slot.primary.reset();
  slot.primary_area_id = -1;
  slot.secondary_area_id = -1;
  if (auto refreshed{refresh_active_zones()}; !refreshed) {
    return refreshed;
  }
  GameState* const game_state{m_manager->game_state()};
  if (game_state == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "session game state is not initialized"};
  }
  if (auto unmapped{game_state->set_area_mapping(request.area_id, -1)}; !unmapped) {
    return unmapped;
  }
  App::Log::info(LogCategory::Scenario,
      "AREA {} released from resident slot {}",
      request.area_id,
      slot_index.value());
  return {};
}

std::expected<void, std::string> ScenarioStartupController::select_current_character(
    const std::size_t owner_slot, const Script::AreaCharacterSelectionRequest& request) {
  if (m_manager == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
  const GameState* const existing_game_state{m_manager->game_state()};
  if (current.has_value() && current->character_id == request.character_id &&
      existing_game_state != nullptr && existing_game_state->current_character().has_value() &&
      existing_game_state->current_character()->character_id == request.character_id) {
    return {};
  }
  if (owner_slot >= m_area_slots.size()) {
    return std::expected<void, std::string>{std::unexpect, "AREA owner slot is out of range"};
  }

  const RuntimeAreaSlot& slot{m_area_slots.at(owner_slot)};
  if (!slot.primary.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, "current-character selection owner has no parsed AREA record"};
  }
  ScenarioRuntime* const runtime{m_manager->world_runtime(slot.world_scene_id)};
  if (runtime == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, "current-character selection owner has no world runtime"};
  }

  std::optional<Omikron::IamCharacterDefinition> definition;
  if (slot.primary->character_by_id(request.character_id).has_value()) {
    if (auto materialized{runtime->character_runtime().ensure_area_character(
            slot.primary_area_id, *slot.primary, request.character_id)};
        !materialized) {
      return materialized;
    }
    definition = slot.primary->character_definition_by_character_id(request.character_id);
    if (!definition.has_value() && slot.scene.has_value()) {
      definition = slot.scene->character_definition_by_character_id(request.character_id);
    }
  } else if (slot.scene.has_value() &&
             slot.scene->character_by_id(request.character_id).has_value()) {
    if (auto materialized{runtime->character_runtime().ensure_scene_character(
            slot.primary_area_id, slot.scene_id, *slot.scene, request.character_id)};
        !materialized) {
      return materialized;
    }
    definition = slot.scene->character_definition_by_character_id(request.character_id);
    if (!definition.has_value()) {
      definition = slot.primary->character_definition_by_character_id(request.character_id);
    }
  } else {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("character ID {} is not present in owner AREA or attached SCENE table 0",
            request.character_id)};
  }

  const Character::RuntimeCharacter* const character{
      runtime->character_runtime().find(request.character_id)};
  if (character == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("selected character {} was not materialized", request.character_id)};
  }
  if (!definition.has_value()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format(
            "selected character {} has no authored table-4 definition", request.character_id)};
  }
  GameState* const game_state{m_manager->game_state()};
  if (game_state == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "session game state is not initialized"};
  }
  game_state->ensure_character_profile(request.character_id, definition->values);
  game_state->establish_current_character(definition.value());
  m_manager->set_controlled_character(ControlledCharacterRef{
      .character_id = request.character_id, .world_scene_id = slot.world_scene_id});

  // Becoming the persistent current character installs the adventure CTL
  // controller from the definition's authored control set. The controller is
  // created disabled: scripted cinematic animation keeps owning the pose
  // until compact 0x68 enables controller participation.
  if (auto controller{runtime->character_runtime().ensure_adventure_controller(
          request.character_id, definition->adventure_control_set)};
      !controller) {
    return controller;
  }
  record("AreaScript.CurrentCharacterSelected",
      fmt::format("character={} world={} model={}",
          character->character_id,
          slot.world_scene_id,
          character->model_resource_name));
  App::Log::info(LogCategory::Scenario,
          "CurrentCharacterChanged — ownerSlot={} id={} world={} model={}",
          owner_slot,
      character->character_id,
      slot.world_scene_id,
      character->model_resource_name);
  return {};
}

std::expected<void, std::string> ScenarioStartupController::set_current_character_presentation(
    const bool enabled) {
  if (m_manager == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
  if (!current.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, "current controlled character is not established"};
  }
  ScenarioRuntime* const runtime{m_manager->world_runtime(current->world_scene_id)};
  if (runtime == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("current controlled character {} world {} is not resident",
            current->character_id,
            current->world_scene_id)};
  }
  if (auto changed{
          runtime->character_runtime().set_presentation_enabled(current->character_id, enabled)};
      !changed) {
    return changed;
  }
  App::Log::debug(LogCategory::Scenario,
      "current character presentation {} — id={} world={}",
      enabled ? "enabled" : "disabled",
      current->character_id,
      current->world_scene_id);
  return {};
}

std::expected<void, std::string> ScenarioStartupController::select_current_character_move(
    const Script::AreaCurrentCharacterMoveRequest& request) {
  if (m_manager == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
  if (!current.has_value()) {
    App::Log::debug(LogCategory::Scenario,
        "current character move/control selection {} ignored — no controlled character",
        request.move_id);
    return {};
  }
  ScenarioRuntime* const runtime{m_manager->world_runtime(current->world_scene_id)};
  Character::RuntimeCharacter* const character{
      runtime == nullptr ? nullptr : runtime->character_runtime().find(current->character_id)};
  if (character == nullptr) {
    App::Log::debug(LogCategory::Scenario,
        "current character move/control selection {} ignored — character {} world {} is not "
        "resident",
        request.move_id,
        current->character_id,
        current->world_scene_id);
    return {};
  }
  if (character->ctl_controller.has_value()) {
    // Runtime 0x0041B6F0 -> 0x0046ACE0 -> 0x0045A630: exact move-ID lookup
    // and controller move switch against the current character's CTL bank.
    if (auto selected{character->ctl_controller->select_move(
            static_cast<std::uint32_t>(request.move_id))};
        !selected) {
      App::Log::warn(LogCategory::Scenario,
          "current character move {} failed — id={} world={}: {}",
          request.move_id,
          current->character_id,
          current->world_scene_id,
          selected.error());
      return std::expected<void, std::string>{std::unexpect, std::move(selected).error()};
    }
  } else {
    App::Log::debug(LogCategory::Scenario,
        "current character move {} ignored — character {} has no adventure CTL controller",
        request.move_id,
        current->character_id);
    return {};
  }
  App::Log::debug(LogCategory::Scenario,
      "current character move/control selection {} — id={} world={}",
      request.move_id,
      current->character_id,
      current->world_scene_id);
  return {};
}

std::expected<void, std::string> ScenarioStartupController::set_current_character_controller(
    const Script::AreaCurrentCharacterControllerRequest& request) {
  if (m_manager == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
  if (!current.has_value()) {
    App::Log::debug(LogCategory::Scenario,
        "current character controller {} ignored — no controlled character",
        request.enabled ? "enabled" : "disabled");
    return {};
  }
  ScenarioRuntime* const runtime{m_manager->world_runtime(current->world_scene_id)};
  Character::RuntimeCharacter* const character{
      runtime == nullptr ? nullptr : runtime->character_runtime().find(current->character_id)};
  if (character == nullptr) {
    App::Log::debug(LogCategory::Scenario,
        "current character controller {} ignored — character {} world {} is not resident",
        request.enabled ? "enabled" : "disabled",
        current->character_id,
        current->world_scene_id);
    return {};
  }
  // 0x68/0x69 only gate participation of the already-initialized adventure
  // CTL controller: no repositioning, no transform reset, no explicit state
  // selection and no bank loading. On enable, the normal player direct-
  // control flags (native 0x81) become active; the first enabled service
  // applies the current CTL state's authored animation as the base pose.
  character->controller_enabled = request.enabled;
  if (character->ctl_controller.has_value()) {
    character->ctl_controller->set_player_direct_control(request.enabled);
  }
    App::Log::info(LogCategory::Scenario,
      "{} — controlledCharacter={} world={} controllerEnabled={} directControl={}",
      request.enabled ? "ControllerOn" : "ControllerOff",
      current->character_id,
      current->world_scene_id,
      character->controller_enabled,
      character->ctl_controller.has_value() &&
          character->ctl_controller->direct_control_active());
  return {};
}

void ScenarioStartupController::service_ctl_controller(const float delta_seconds) {
  if (m_manager == nullptr) {
    return;
  }
  const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
  if (!current.has_value()) {
    return;
  }
  ScenarioRuntime* const runtime{m_manager->world_runtime(current->world_scene_id)};
  Character::RuntimeCharacter* const character{
      runtime == nullptr ? nullptr : runtime->character_runtime().find(current->character_id)};
  if (character == nullptr || !character->ctl_controller.has_value() ||
      !character->controller_enabled || !character->active || !character->area_present) {
    return;
  }

  Character::CtlController& controller{character->ctl_controller.value()};
  controller.service(delta_seconds, m_manager->ctl_input_mask(), *character);

  // One-shot animation-linked audio markers resolve through the owner
  // world's SCX DEAD0003 hID table at the live character position.
  for (const Character::CtlController::SoundMarkerEvent& event :
      controller.take_sound_marker_events()) {
    runtime->play_ctl_sound_marker(event.sound_hid, character->transform.translation);
  }
}

std::expected<void, std::string> ScenarioStartupController::start_compact_dialog(
    const Script::AreaDialogRequest& request) {
  if (m_manager == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  if (auto started{m_manager->start_dialog(static_cast<std::uint16_t>(request.dialog_id))};
      !started) {
    return std::expected<void, std::string>{std::unexpect, started.error()};
  }

  m_dialog_takeover_active = true;
  m_dialog_takeover_id = request.dialog_id;
  record("AreaScript.DialogStarted", fmt::format("id={}", request.dialog_id));
  record("DialogTakeover.Entered", fmt::format("id={}", request.dialog_id));
  App::Log::info(LogCategory::Script, "AREA opcode 0x3D — started dialog {}", request.dialog_id);
  return {};
}

std::expected<void, std::string> ScenarioStartupController::set_object_placement_state(
    const std::size_t owner_slot, const Script::AreaObjectPlacementStateRequest& request) {
  if (m_manager == nullptr || m_manager->game_state() == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, "persistent IAM game state is not initialized"};
  }
  if (owner_slot >= m_area_slots.size()) {
    return std::expected<void, std::string>{
        std::unexpect, "object-placement owner AREA slot is out of range"};
  }

  const RuntimeAreaSlot& slot{m_area_slots.at(owner_slot)};
  if (!slot.primary.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, "object-placement owner has no parsed AREA record"};
  }

  std::optional<std::int16_t> persistent_state_index;
  std::optional<std::int32_t> scene_owner;
  if (const auto area_placement{slot.primary->object_by_id(request.object_id)};
      area_placement.has_value()) {
    persistent_state_index = area_placement->persistent_state_index;
  } else if (slot.scene.has_value()) {
    if (const auto scene_placement{slot.scene->object_by_id(request.object_id)};
        scene_placement.has_value()) {
      persistent_state_index = scene_placement->persistent_state_index;
      scene_owner = slot.scene_id;
    }
  }

  if (!persistent_state_index.has_value()) {
    // Retail 0x4D explicitly tolerates an absent target. 0x4C falls through
    // to a null dereference instead; turn that trusted-data invariant into a
    // structured diagnostic rather than reproducing the crash.
    if (!request.enabled) {
      App::Log::debug(LogCategory::Script,
          "DisableObjectPlacement ignored missing object {} in owner AREA {}",
          request.object_id,
          slot.primary_area_id);
      return {};
    }
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("object placement {} was not found in owner AREA {} or attached SCENE",
            request.object_id,
            slot.primary_area_id)};
  }
  if (persistent_state_index.value() < 0) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("object placement {} has negative packed-state index {}",
            request.object_id,
            persistent_state_index.value())};
  }

  GameState& game_state{*m_manager->game_state()};
  const std::size_t state_index{static_cast<std::size_t>(persistent_state_index.value())};
  auto old_state{game_state.packed_state(state_index)};
  if (!old_state) {
    return std::expected<void, std::string>{std::unexpect, old_state.error()};
  }

  // Bit 0 is the placement-exists/materialization gate. 0x4C/0x4D only
  // operate on an already-existing placement and never create/destroy it.
  if ((old_state.value() & 0x01U) == 0U) {
    record("AreaScript.ObjectPlacementStateIgnored",
        fmt::format("id={} stateIndex={} state={} reason=not-materialized",
            request.object_id,
            state_index,
            old_state.value()));
    return {};
  }

  const std::uint8_t new_state{static_cast<std::uint8_t>(
      request.enabled ? (old_state.value() | 0x02U) : (old_state.value() & ~0x02U))};
  if (auto written{game_state.set_packed_state(state_index, new_state)}; !written) {
    return written;
  }

  ScenarioRuntime* const runtime{m_manager->world_runtime(slot.world_scene_id)};
  if (runtime == nullptr) {
    auto rollback{game_state.set_packed_state(state_index, old_state.value())};
    return std::expected<void, std::string>{std::unexpect,
        rollback ? std::string{"object-placement owner has no world runtime"}
                 : fmt::format("object-placement owner has no world runtime; persistent-state "
                               "rollback failed: {}",
                       rollback.error())};
  }
  auto updated{runtime->object_placement_runtime().set_enabled(
      slot.primary_area_id, scene_owner, request.object_id, request.enabled)};
  if (!updated) {
    auto rollback{game_state.set_packed_state(state_index, old_state.value())};
    return std::expected<void, std::string>{std::unexpect,
        rollback
            ? updated.error()
            : fmt::format(
                  "{}; persistent-state rollback failed: {}", updated.error(), rollback.error())};
  }

  record("AreaScript.ObjectPlacementState",
      fmt::format("id={} source={} stateIndex={} {}->{} enabled={}",
          request.object_id,
          scene_owner.has_value() ? fmt::format("SCENE {}", scene_owner.value())
                                  : fmt::format("AREA {}", slot.primary_area_id),
          state_index,
          old_state.value(),
          new_state,
          request.enabled));
  App::Log::debug(LogCategory::Scenario,
      "object placement {} {} — AREA {}{} packedState[{}]={}",
      request.object_id,
      request.enabled ? "enabled" : "disabled",
      slot.primary_area_id,
      scene_owner.has_value() ? fmt::format(" SCENE {}", scene_owner.value()) : std::string{},
      state_index,
      new_state);
  return {};
}

std::expected<void, std::string> ScenarioStartupController::present_compact_object(
    const Script::AreaObjectActivationRequest& request) {
  if (request.object_id == -1) {
    record("AreaScript.ObjectActivate", "id=-1 skipped");
    return {};
  }
  if (request.object_id < 0) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("OBJECTS ID {} is negative", request.object_id)};
  }
  if (m_manager == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  if (m_area_slots.at(m_active_area_slot).primary_area_id == 222 &&
      (request.object_id == 405 || request.object_id == 406)) {
    App::Log::info(LogCategory::Scenario, "AREA 222 sequence: object{}", request.object_id);
    record("Area222.CameraSequence", fmt::format("object{}", request.object_id));
  }

  if (!m_object_archive.has_value()) {
    auto bytes{read_file(std::string{K_IAM_OBJECT_PATH})};
    if (!bytes) {
      // Object presentation is explicitly fire-and-forget.  A missing archive
      // must be observable but cannot deadlock an otherwise valid compact
      // scenario context (and preserves title-screen fixtures without OBJECT).
      App::Log::warn(LogCategory::Scenario,
          "OBJECTS ID {} skipped because IAM/OBJECT could not be loaded: {}",
          request.object_id,
          bytes.error());
      record("AreaScript.ObjectActivateUnavailable", fmt::format("id={}", request.object_id));
      return {};
    }
    m_object_archive_bytes = std::move(bytes).value();
    m_object_archive.emplace(std::span<const std::byte>{m_object_archive_bytes},
        Omikron::IamObjectRecord::k_serialized_size,
        Omikron::IamObjectRecord::k_archive_stride);
  }

  auto object_bytes{m_object_archive->read_record(static_cast<std::uint16_t>(request.object_id))};
  if (!object_bytes) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("IAM/OBJECT {}: {}", request.object_id, object_bytes.error())};
  }
  auto object{Omikron::IamObjectRecord::load(object_bytes.value())};
  if (!object) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("IAM/OBJECT {}: {}", request.object_id, object.error())};
  }

  // Runtime branches only on type 0x10, which owns the distinct IMAGES path.
  // Every other object type continues through the recovered VOICEOFF/subtitle
  // presentation path; do not narrow that retail behavior to an invented
  // "voice-over type 0".
  constexpr std::uint16_t k_image_type{0x10U};
  if (object->object_type() == k_image_type) {
    App::Log::debug(LogCategory::Scenario,
        "OBJECTS ID {} type {:#x} uses the deferred IMAGES presentation path",
        request.object_id,
        object->object_type());
    record("AreaScript.ObjectActivateUnsupported",
        fmt::format("id={} type={:#x} path=IMAGES", request.object_id, object->object_type()));
    return {};
  }

  const WorldSceneContext* context{m_manager->active_world_context()};
  if (context == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, "OBJECTS activation has no active world presentation context"};
  }
  const std::string& stem{object->audio_stem()};
  std::optional<std::string> audio_path;
  if (!stem.empty()) {
    audio_path = stem.starts_with("ZVOT") || stem.starts_with("ZVOP")
                     ? "VOICEOFF/JINGOFF3.ADP"
                     : fmt::format("VOICEOFF/{}.ADP", stem);
  }
  constexpr std::uint32_t k_minimum_world_text_duration_ms{2000U};
  constexpr std::uint32_t k_world_text_milliseconds_per_raw_byte{80U};
  const std::uint32_t world_text_duration_ms{std::max(k_minimum_world_text_duration_ms,
      static_cast<std::uint32_t>(object->subtitle().size()) *
          k_world_text_milliseconds_per_raw_byte)};

  if (audio_path.has_value()) {
    m_manager->world_presentation().enqueue_voice_over(
        WorldVoiceOverCommand{.scene_id = context->scene_id,
            .scene_generation = context->generation,
            .object_id = request.object_id,
            .audio_path = audio_path.value()});
  }
  m_manager->world_presentation().enqueue_world_text(WorldTextCommand{.scene_id = context->scene_id,
      .scene_generation = context->generation,
      .document = Interface::parse_runtime_text(object->subtitle()),
      .provenance = WorldTextProvenance{.source_kind = TextSourceKind::k_iam_object,
          .object_id = request.object_id,
          .audio_resource = audio_path.value_or(""),
          .role = TextPresentationRole::k_unknown,
          .modernization_policy = TextModernizationPolicy::k_faithful_only},
      .duration_ms = world_text_duration_ms});
  record("AreaScript.ObjectActivate",
      fmt::format("id={} type={:#x} voice={} worldTextMs={}",
          request.object_id,
          object->object_type(),
          audio_path.value_or("<none>"),
          world_text_duration_ms));
  App::Log::debug(LogCategory::Scenario,
      "OBJECTS ID {} voice-over '{}' world-text={} ms",
      request.object_id,
      audio_path.value_or("<none>"),
      world_text_duration_ms);
  return {};
}

std::expected<void, std::string> ScenarioStartupController::deactivate_owner_character(
    const std::size_t owner_slot, const Script::AreaCharacterDeactivationRequest& request) {
  if (request.character_id == -1) {
    return set_current_character_presentation(false);
  }
  if (m_manager == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
  if (current.has_value() && current->character_id == request.character_id) {
    return {};
  }
  if (owner_slot >= m_area_slots.size()) {
    return std::expected<void, std::string>{std::unexpect, "AREA owner slot is out of range"};
  }
  const RuntimeAreaSlot& slot{m_area_slots.at(owner_slot)};
  if (!slot.primary.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, "character deactivation owner has no parsed AREA record"};
  }
  const bool in_area{slot.primary->character_by_id(request.character_id).has_value()};
  const bool in_scene{
      slot.scene.has_value() && slot.scene->character_by_id(request.character_id).has_value()};
  if (!in_area && !in_scene) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("character ID {} is not present in owner AREA or attached SCENE table 0",
            request.character_id)};
  }
  ScenarioRuntime* const runtime{m_manager->world_runtime(slot.world_scene_id)};
  if (runtime == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, "character deactivation owner has no world runtime"};
  }
  if (runtime->character_runtime().find(request.character_id) == nullptr) {
    return {};
  }
  return runtime->character_runtime().deactivate_character(request.character_id);
}

std::expected<std::size_t, std::string> ScenarioStartupController::launch_character_script(
    const std::size_t owner_slot, const Script::AreaCharacterScriptRequest& request) {
  if (m_manager == nullptr) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "scenario manager is not available"};
  }
  if (owner_slot >= m_area_slots.size()) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "character-script owner slot is out of range"};
  }

  const RuntimeAreaSlot& slot{m_area_slots.at(owner_slot)};
  const Omikron::ScxData* scx{m_manager->world_context_scx(slot.world_scene_id)};
  ScenarioRuntime* const scenario_runtime{m_manager->world_runtime(slot.world_scene_id)};
  if (scx == nullptr || scenario_runtime == nullptr) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "character-script owner has no loaded SCX/character runtime"};
  }

  std::int16_t character_id{0};
  std::string_view target_name;
  switch (request.target) {
    case Script::AreaCharacterScriptTarget::k_explicit:
      if (!request.character_id.has_value()) {
        return std::expected<std::size_t, std::string>{
            std::unexpect, "explicit character-script request has no character ID"};
      }
      character_id = request.character_id.value();
      target_name = "explicit";
      break;
    case Script::AreaCharacterScriptTarget::k_current: {
      const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
      if (!current.has_value()) {
        return std::expected<std::size_t, std::string>{
            std::unexpect, "current controlled character is not established"};
      }
      if (current->world_scene_id != slot.world_scene_id) {
        return std::expected<std::size_t, std::string>{std::unexpect,
            fmt::format("current controlled character {} belongs to world {}, but character-script "
                        "owner is world {}",
                current->character_id,
                current->world_scene_id,
                slot.world_scene_id)};
      }
      character_id = current->character_id;
      target_name = "current";
      break;
    }
  }

  std::optional<std::size_t> source_script_index;
  for (std::size_t index{0}; index < scx->scripts.size(); ++index) {
    if (scx->scripts.at(index).script_id == request.script_id) {
      source_script_index = index;
      break;
    }
  }
  if (!source_script_index.has_value()) {
    return std::expected<std::size_t, std::string>{std::unexpect,
        fmt::format("character-script ID {} was not found in owner world {}",
            request.script_id,
            slot.world_scene_id)};
  }

  auto created{scenario_runtime->spawn_character_script_instance(
      source_script_index.value(), character_id, 0)};
  if (!created) {
    return created;
  }

  // Native 0x2E/0x3B/0x3C/0x5A all perform the same post-launch camera
  // operation: clamp the Scalar16 duration at zero, copy the live camera state,
  // then switch the camera controller to mode 13 (0x0D). Keep this operation
  // ordered in the same presentation mailbox as ordinary IAM camera commands.
  const WorldSceneContext* const owner_context{m_manager->find_world_context(slot.world_scene_id)};
  if (owner_context == nullptr) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "character-script camera controller has no owner world context"};
  }
  const std::int16_t camera_duration_units{
      std::max<std::int16_t>(std::int16_t{0}, request.camera_duration_units)};
  m_manager->world_presentation().enqueue_camera(
      WorldCameraCommand{.kind = WorldCameraCommandKind::k_controller_mode,
          .scene_id = owner_context->scene_id,
          .scene_generation = owner_context->generation,
          .controller_mode = 13U,
          .duration_units = camera_duration_units});

  const bool tracked{request.mode == Script::AreaCharacterScriptLaunchMode::k_tracked};
  const Omikron::ScxScript& script{scx->scripts.at(source_script_index.value())};
  record("AreaScript.CharacterScriptStarted",
      fmt::format(
          "ownerSlot={} target={} character={} script={} name='{}' cameraDuration={} mode={} "
          "instance={}",
          owner_slot,
          target_name,
          character_id,
          request.script_id,
          script.name,
          request.camera_duration_units,
          tracked ? "tracked" : "fire-and-forget",
          created.value()));
  App::Log::debug(LogCategory::Script,
      "AREA character-script launch — owner slot={} target={} character={} script {} '{}' as "
      "instance {}, {}",
      owner_slot,
      target_name,
      character_id,
      request.script_id,
      script.name,
      created.value(),
      tracked ? "tracked" : "fire-and-forget");
  return created;
}

std::expected<void, std::string> ScenarioStartupController::place_current_character_at_address(
    const Script::AreaAddressPlacementRequest& request) {
  if (m_manager == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
  if (!current.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, "current controlled character is not established"};
  }
  std::optional<Omikron::IamAreaAddressRecord> resolved_address;
  for (const RuntimeAreaSlot& slot : m_area_slots) {
    if (!slot.primary.has_value()) {
      continue;
    }
    const auto address{slot.primary->address_by_id(request.address_id)};
    if (!address.has_value()) {
      continue;
    }
    if (resolved_address.has_value()) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("address {} is ambiguous across resident AREAs", request.address_id)};
    }
    resolved_address = address;
  }
  if (!resolved_address.has_value()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("address {} was not found in resident AREAs", request.address_id)};
  }

  ScenarioRuntime* const runtime{m_manager->world_runtime(current->world_scene_id)};
  if (runtime == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("current controlled character {} world {} is not resident",
            current->character_id,
            current->world_scene_id)};
  }
  auto placed{runtime->character_runtime().place_character_at_address(
      current->character_id, resolved_address.value())};
  if (placed) {
    App::Log::info(
        LogCategory::Scenario, "current character placed at address {}", request.address_id);
  }
  return placed;
}

std::expected<void, std::string> ScenarioStartupController::set_address_flag(
    const Script::AreaAddressFlagRequest& request) {
  if (m_manager == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  if (request.address_id < 0) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("ADDRESSES ID {} is negative", request.address_id)};
  }
  GameState* const game_state{m_manager->game_state()};
  if (game_state == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, "persistent IAM game state is not initialized"};
  }
  auto updated{game_state->set_address_flag(
      static_cast<std::uint16_t>(request.address_id), request.enabled)};
  if (updated) {
    App::Log::debug(LogCategory::Script,
        "persistent ADDRESS {} {}",
        request.address_id,
        request.enabled ? "set" : "cleared");
  }
  return updated;
}

std::expected<void, std::string> ScenarioStartupController::add_object_to_persistent_collection(
    const Script::AreaPersistentObjectCollectionRequest& request) {
  if (m_manager == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  if (request.collection_kind < 0) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("persistent object collection kind {} is negative", request.collection_kind)};
  }
  if (request.object_id < 0) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("OBJECTS ID {} is negative", request.object_id)};
  }
  GameState* const game_state{m_manager->game_state()};
  if (game_state == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, "persistent IAM game state is not initialized"};
  }
  auto added{game_state->add_object_to_collection(
      static_cast<std::uint16_t>(request.collection_kind), request.object_id)};
  if (!added) {
    return std::expected<void, std::string>{std::unexpect, added.error()};
  }
  if (added.value()) {
    App::Log::debug(LogCategory::Script,
        "persistent object collection {} inserted OBJECTS ID {}",
        request.collection_kind,
        request.object_id);
  }
  return {};
}

std::expected<void, std::string> ScenarioStartupController::refresh_active_zones() {
  if (m_manager == nullptr || m_manager->game_state() == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, "persistent IAM game state is not initialized"};
  }

  GameState& game_state{*m_manager->game_state()};
  std::vector<ActiveZoneRef> rebuilt;
  std::size_t area_count{0};
  std::size_t scene_count{0};

  const auto append_enabled = [&game_state, &rebuilt, &area_count, &scene_count](
                                  const std::vector<Omikron::IamZoneRecord>& zones,
                                  const std::size_t resident_slot,
                                  const ActiveZoneSource source,
                                  const std::int32_t area_id,
                                  const std::int32_t scene_id) -> std::expected<void, std::string> {
    for (const Omikron::IamZoneRecord& zone : zones) {
      const std::uint16_t raw_zone_id{static_cast<std::uint16_t>(zone.zone_id)};
      auto enabled{game_state.zone_flag(raw_zone_id)};
      if (!enabled) {
        return std::expected<void, std::string>{std::unexpect,
            fmt::format("cannot read persistent ZONE {}: {}", raw_zone_id, enabled.error())};
      }
      if (!enabled.value()) {
        continue;
      }
      rebuilt.push_back(ActiveZoneRef{.resident_slot = resident_slot,
          .source = source,
          .area_id = area_id,
          .scene_id = scene_id,
          .zone = zone});
      if (source == ActiveZoneSource::k_area) {
        ++area_count;
      } else {
        ++scene_count;
      }
    }
    return {};
  };

  for (std::size_t index{0}; index < m_area_slots.size(); ++index) {
    const RuntimeAreaSlot& slot{m_area_slots.at(index)};
    if (slot.primary.has_value()) {
      const std::vector<Omikron::IamAreaZoneRecord> zones{slot.primary->zones()};
      if (auto appended{
              append_enabled(zones, index, ActiveZoneSource::k_area, slot.primary_area_id, -1)};
          !appended) {
        return appended;
      }
    }
    if (slot.scene.has_value()) {
      const std::vector<Omikron::IamSceneZoneRecord> zones{slot.scene->zones()};
      if (auto appended{append_enabled(
              zones, index, ActiveZoneSource::k_scene, slot.primary_area_id, slot.scene_id)};
          !appended) {
        return appended;
      }
    }
  }

  m_active_zones = std::move(rebuilt);
  App::Log::debug(LogCategory::Scenario,
      "active zones rebuilt — total={} area={} scene={}",
      m_active_zones.size(),
      area_count,
      scene_count);
  return {};
}

std::expected<void, std::string> ScenarioStartupController::set_zone_activation(
    const Script::AreaZoneActivationRequest& request) {
  if (m_manager == nullptr || m_manager->game_state() == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, "persistent IAM game state is not initialized"};
  }

  const std::uint16_t raw_zone_id{static_cast<std::uint16_t>(request.zone_id)};
  if (auto updated{m_manager->game_state()->set_zone_flag(raw_zone_id, request.enabled)};
      !updated) {
    return updated;
  }
  // Runtime changes the START-backed bit before it enters its common resident
  // zone refresh path. Do not roll that persistent mutation back on refresh
  // failure.
  if (auto refreshed{refresh_active_zones()}; !refreshed) {
    return refreshed;
  }
  if (request.enabled) {
    for (const ActiveZoneRef& zone : m_active_zones) {
      if (static_cast<std::uint16_t>(zone.zone.zone_id) != raw_zone_id) {
        continue;
      }
      App::Log::info(LogCategory::Scenario,
          "ZoneActivated — source={} ownerSlot={} area={} scene={} zone={} qualifies={}",
          zone.source == ActiveZoneSource::k_area ? "AREA" : "SCENE",
          zone.resident_slot,
          zone.area_id,
          zone.scene_id,
          zone.zone.zone_id,
          zone_contact_reporting_enabled(zone));
    }
  }
  App::Log::debug(
      LogCategory::Script, "{} ZONE {}", request.enabled ? "activate" : "deactivate", raw_zone_id);
  return {};
}

std::expected<void, std::string> ScenarioStartupController::service_scx_script_wait(
    Script::AreaScriptRuntime& area_script, const std::size_t owner_slot) {
  if (area_script.state() != Script::AreaScriptState::k_waiting ||
      area_script.wait_info().kind != Script::AreaWaitKind::k_scx_script) {
    return {};
  }
  if (m_manager == nullptr || owner_slot >= m_area_slots.size() ||
      !area_script.wait_info().scx_script_instance.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, "AREA SCX-script wait has no resident owner or instance ID"};
  }

  const RuntimeAreaSlot& slot{m_area_slots.at(owner_slot)};
  ScenarioRuntime* const scenario_runtime{m_manager->world_runtime(slot.world_scene_id)};
  if (scenario_runtime == nullptr || scenario_runtime->script_runtime() == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        "AREA is waiting on an SCX script but its owner world runtime does not exist"};
  }

  const std::size_t instance_id{area_script.wait_info().scx_script_instance.value()};
  const Script::ScriptInstance* const instance{
      scenario_runtime->script_runtime()->instance(instance_id)};
  if (instance == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("AREA is waiting on missing SCX-script instance {} in owner world {}",
            instance_id,
            slot.world_scene_id)};
  }
  if (instance->paused) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("SCX script instance {} '{}' paused: {}",
            instance_id,
            instance->script_name,
            instance->pause_info.reason_text)};
  }
  if (!instance->completed) {
    return {};
  }
  if (auto completed{area_script.complete_scx_script_wait(instance_id)}; !completed) {
    return completed;
  }
  record("AreaScript.ScxScriptCompleted",
      fmt::format("ownerSlot={} instance={}", owner_slot, instance_id));
  return {};
}

std::expected<void, std::string> ScenarioStartupController::service_character_script_wait(
    Script::AreaScriptRuntime& area_script, const std::size_t owner_slot) {
  if (area_script.state() != Script::AreaScriptState::k_waiting ||
      area_script.wait_info().kind != Script::AreaWaitKind::k_character_script) {
    return {};
  }
  if (m_manager == nullptr || !area_script.wait_info().character_script_instance.has_value() ||
      !area_script.wait_info().character_script.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, "AREA character-script wait has no scenario owner or instance ID"};
  }
  if (owner_slot >= m_area_slots.size()) {
    return std::expected<void, std::string>{
        std::unexpect, "AREA character-script wait owner slot is out of range"};
  }

  const RuntimeAreaSlot& slot{m_area_slots.at(owner_slot)};
  ScenarioRuntime* const scenario_runtime{m_manager->world_runtime(slot.world_scene_id)};
  if (scenario_runtime == nullptr || scenario_runtime->script_runtime() == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        "AREA is waiting on a character script but its owner world runtime does not exist"};
  }

  const Script::ScriptRuntime* script_runtime{scenario_runtime->script_runtime()};
  const std::size_t instance_id{area_script.wait_info().character_script_instance.value()};
  const Script::AreaCharacterScriptRequest wait_request{
      area_script.wait_info().character_script.value()};
  const Script::ScriptInstance* instance{script_runtime->instance(instance_id)};
  if (instance == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("AREA is waiting on missing character-script instance {}", instance_id)};
  }
  if (instance->paused &&
      instance->pause_info.reason != Script::ScriptPauseReason::k_unhandled_opcode) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("character-script instance {} '{}' paused with an error: {}",
            instance_id,
            instance->script_name,
            instance->pause_info.reason_text)};
  }
  if (!instance->completed) {
    return {};
  }
  if (auto completed{area_script.complete_character_script_wait(instance_id)}; !completed) {
    return completed;
  }
    record("AreaScript.CharacterScriptCompleted",
      fmt::format("ownerSlot={} instance={}", owner_slot, instance_id));
    App::Log::info(LogCategory::Script,
      "CompactWaitResumed — ownerSlot={} area={} scene={} compactIp=+{:#x} target={} "
      "script={} instance={} name='{}' group={} paused={} execution={}/{}",
      owner_slot,
      slot.primary_area_id,
      slot.scene_id,
      area_script.instruction_pointer(),
      wait_request.character_id.value_or(-1),
      wait_request.script_id,
      instance_id,
      instance->script_name,
      instance->current_group_index,
      instance->paused,
      instance->root_commands.empty() ? 0U : instance->root_commands.at(0).execution_count,
      instance->root_commands.empty() ? 0U : instance->root_commands.at(0).execution_limit);
  return {};
}

std::expected<void, std::string> ScenarioStartupController::service_camera_completions() {
  if (m_manager == nullptr) {
    return {};
  }

  while (std::optional<WorldCameraOperationCompletion> completed{
      m_manager->world_presentation().take_camera_completion()}) {
    const Script::AreaCameraOperationHandle handle{.generation = completed->operation_generation};
    Script::AreaScriptRuntime* target{nullptr};
    const auto consider = [&target, handle](Script::AreaScriptRuntime* runtime) {
      if (target != nullptr || runtime == nullptr ||
          runtime->state() != Script::AreaScriptState::k_waiting ||
          runtime->wait_info().kind != Script::AreaWaitKind::k_camera ||
          !runtime->wait_info().camera_operation.has_value() ||
          runtime->wait_info().camera_operation.value() != handle) {
        return;
      }
      target = runtime;
    };

    for (std::size_t index{0}; index < m_area_slots.size(); ++index) {
      std::optional<Script::AreaScriptRuntime>& primary_script{m_area_scripts.at(index)};
      if (primary_script.has_value()) {
        consider(&primary_script.value());
      }
      RuntimeAreaSlot& slot{m_area_slots.at(index)};
      if (slot.scene_script.has_value()) {
        consider(&slot.scene_script.value());
      }
    }
    for (const std::unique_ptr<ZoneContactContext>& contact : m_zone_contacts) {
      consider(contact != nullptr && contact->script != nullptr ? contact->script.get() : nullptr);
    }

    if (target == nullptr) {
      App::Log::debug(LogCategory::Scenario,
          "stale camera completion ignored — generation={} camera={} scene={} worldGeneration={}",
          completed->operation_generation,
          completed->camera_id,
          completed->scene_id,
          completed->scene_generation);
      continue;
    }
    if (auto resumed{target->complete_camera_wait(handle)}; !resumed) {
      return resumed;
    }
    if (completed->source_area_id == 222 &&
        (completed->camera_id == 4291U || completed->camera_id == 4292U)) {
      record("Area222.CameraSequence", fmt::format("{} completed", completed->camera_id));
    }
  }
  return {};
}

std::expected<void, std::string> ScenarioStartupController::service_area_scripts(
    const float delta_seconds) {
  std::vector<std::size_t> order;
  order.reserve(m_area_scripts.size());
  for (std::size_t index{0}; index < m_area_scripts.size(); ++index) {
    if (m_area_scripts.at(index).has_value()) {
      order.push_back(index);
    }
  }
  std::ranges::sort(order, [this](const std::size_t lhs, const std::size_t rhs) {
    return m_area_script_sequences.at(lhs) < m_area_script_sequences.at(rhs);
  });

  for (const std::size_t owner_slot : order) {
    std::optional<Script::AreaScriptRuntime>& primary_script{m_area_scripts.at(owner_slot)};
    if (!primary_script.has_value()) {
      continue;
    }
    Script::AreaScriptRuntime& script{primary_script.value()};

    if (auto serviced{service_scx_script_wait(script, owner_slot)}; !serviced) {
      return serviced;
    }
    if (auto serviced{service_character_script_wait(script, owner_slot)}; !serviced) {
      return serviced;
    }

    if (!m_area_event_started_recorded.at(owner_slot) &&
        (script.state() == Script::AreaScriptState::k_ready ||
            script.state() == Script::AreaScriptState::k_running)) {
      const std::int32_t area_id{m_area_slots.at(owner_slot).primary_area_id};
      if (m_area_script_sequences.at(owner_slot) == 1U) {
        // Preserve startup trace compatibility for the initial AREA context.
        record("AreaScript.EventStarted", "event=1");
      } else {
        record(
            "AreaScript.EventStarted", fmt::format("area={} slot={} event=1", area_id, owner_slot));
      }
      App::Log::info(LogCategory::Script,
          "AREA {} resident primary event 1 started — slot={}",
          area_id,
          owner_slot);
      m_area_event_started_recorded.at(owner_slot) = true;
    }

    if (script.state() == Script::AreaScriptState::k_ready ||
        script.state() == Script::AreaScriptState::k_running) {
      const Script::AreaScriptState state{script.run(delta_seconds)};
      if (state == Script::AreaScriptState::k_failed) {
        return std::expected<void, std::string>{std::unexpect,
            fmt::format("AREA {} primary compact VM failed: {}",
                m_area_slots.at(owner_slot).primary_area_id,
                script.pause_info().reason_text)};
      }
      if (state == Script::AreaScriptState::k_paused_unsupported) {
        App::Log::warn(LogCategory::Script,
            "AREA {} primary compact VM paused — unsupported opcode={:#04x} offset=+{:#x} "
            "bytes=[{}]",
            m_area_slots.at(owner_slot).primary_area_id,
            script.pause_info().opcode,
            script.pause_info().offset,
            script.pause_info().nearby_bytes);
      }
    } else if (script.state() == Script::AreaScriptState::k_failed) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("AREA {} primary compact VM failed: {}",
              m_area_slots.at(owner_slot).primary_area_id,
              script.pause_info().reason_text)};
    }

    if (script.state() == Script::AreaScriptState::k_waiting) {
      if (!m_area_waiting_recorded.at(owner_slot)) {
        if (m_area_script_sequences.at(owner_slot) == 1U) {
          record("AreaContext.Waiting", fmt::format("state={}", script.wait_state()));
        } else {
          record("AreaContext.Waiting",
              fmt::format("area={} slot={} state={}",
                  m_area_slots.at(owner_slot).primary_area_id,
                  owner_slot,
                  script.wait_state()));
        }
        m_area_waiting_recorded.at(owner_slot) = true;
      }
    } else {
      m_area_waiting_recorded.at(owner_slot) = false;
    }

    // 0x30 can release the context's backing resident AREA from inside that
    // same context. AreaScriptRuntime owns its bytecode, so let the instruction
    // dispatcher return and reach EndEvent before destroying the modern object.
    if (!m_area_slots.at(owner_slot).primary.has_value() &&
        (script.state() == Script::AreaScriptState::k_ready ||
            script.state() == Script::AreaScriptState::k_completed)) {
      m_area_scripts.at(owner_slot).reset();
      m_area_script_sequences.at(owner_slot) = 0;
      m_area_event_started_recorded.at(owner_slot) = false;
      m_area_waiting_recorded.at(owner_slot) = false;
    }
  }
  return {};
}

void ScenarioStartupController::service_scene_scripts(const float delta_seconds) {
  for (std::size_t index{0}; index < m_area_slots.size(); ++index) {
    RuntimeAreaSlot& slot{m_area_slots.at(index)};
    if (!slot.scene_script.has_value()) {
      continue;
    }
    Script::AreaScriptRuntime& script{slot.scene_script.value()};
    if (auto serviced{service_scx_script_wait(script, index)}; !serviced) {
      App::Log::warn(LogCategory::Script,
          "SCENE {} SCX-script wait failed: {}",
          slot.scene_id,
          serviced.error());
      continue;
    }
    if (auto serviced{service_character_script_wait(script, index)}; !serviced) {
      App::Log::warn(LogCategory::Script,
          "SCENE {} character-script wait failed: {}",
          slot.scene_id,
          serviced.error());
      continue;
    }
    if (script.state() != Script::AreaScriptState::k_ready &&
        script.state() != Script::AreaScriptState::k_running) {
      continue;
    }
    const Script::AreaScriptState previous_state{script.state()};
    const std::optional<std::uint16_t> previous_event{script.active_event()};
    const Script::AreaScriptState state{script.run(delta_seconds)};
    if (previous_state != Script::AreaScriptState::k_waiting &&
        state == Script::AreaScriptState::k_waiting) {
      App::Log::info(LogCategory::Script,
          "CompactWaitEntered — source=SCENE ownerSlot={} area={} scene={} ip=+{:#x} kind={} "
          "runtimeState={} scxInstance={} characterInstance={}",
          index,
          slot.primary_area_id,
          slot.scene_id,
          script.instruction_pointer(),
          static_cast<unsigned int>(script.wait_info().kind),
          script.wait_info().runtime_state,
          script.wait_info().scx_script_instance.value_or(0U),
          script.wait_info().character_script_instance.value_or(0U));
    }
    if (previous_event.has_value() && !script.active_event().has_value()) {
      App::Log::info(LogCategory::Script,
          "CompactEventEnded — source=SCENE ownerSlot={} area={} scene={} event={} ip=+{:#x}",
          index,
          slot.primary_area_id,
          slot.scene_id,
          previous_event.value(),
          script.instruction_pointer());
    }
    if (state == Script::AreaScriptState::k_paused_unsupported) {
      App::Log::warn(LogCategory::Script,
          "SCENE {} compact VM paused — unsupported opcode={:#04x} offset=+{:#x} bytes=[{}]",
          slot.scene_id,
          script.pause_info().opcode,
          script.pause_info().offset,
          script.pause_info().nearby_bytes);
    } else if (state == Script::AreaScriptState::k_failed) {
      App::Log::warn(LogCategory::Script,
          "SCENE {} compact VM failed: {}",
          slot.scene_id,
          script.pause_info().reason_text);
    }
  }
}

bool ScenarioStartupController::zone_contact_backing_resident(
    const ZoneContactContext& contact) const {
  if (contact.resident_slot >= m_area_slots.size()) {
    return false;
  }
  const RuntimeAreaSlot& slot{m_area_slots.at(contact.resident_slot)};
  if (slot.primary_area_id != contact.area_id ||
      (contact.source == ActiveZoneSource::k_scene && slot.scene_id != contact.scene_id)) {
    return false;
  }
  std::vector<Omikron::IamZoneRecord> zones;
  if (contact.source == ActiveZoneSource::k_area) {
    if (slot.primary.has_value()) {
      zones = slot.primary->zones();
    }
  } else if (slot.scene.has_value()) {
    zones = slot.scene->zones();
  }
  return std::ranges::any_of(zones, [&contact](const Omikron::IamZoneRecord& candidate) {
    return candidate.zone_id == contact.zone.zone_id &&
           candidate.event_offsets == contact.zone.event_offsets;
  });
}

bool ScenarioStartupController::zone_contact_spatially_matches(
    const ZoneContactContext& contact) const {
  if (m_manager == nullptr || contact.resident_slot >= m_area_slots.size()) {
    return false;
  }
  const RuntimeAreaSlot& slot{m_area_slots.at(contact.resident_slot)};
  const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
  if (!current.has_value() || current->world_scene_id != slot.world_scene_id) {
    return false;
  }
  ScenarioRuntime* const runtime{m_manager->world_runtime(slot.world_scene_id)};
  const Character::RuntimeCharacter* const character{
      runtime == nullptr ? nullptr : runtime->character_runtime().find(current->character_id)};
  if (character == nullptr || !character->active || !character->area_present) {
    return false;
  }
  return zone_contains_runtime_xz(contact.zone, character->transform.translation) &&
         contact.zone.accepts_orientation(character->serialized_orientation_units);
}

bool ScenarioStartupController::zone_contact_reporting_enabled(
    const ZoneContactContext& contact) const {
  if (m_manager == nullptr || contact.resident_slot >= m_area_slots.size()) {
    return false;
  }
  const RuntimeAreaSlot& slot{m_area_slots.at(contact.resident_slot)};
  const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
  if (!current.has_value() || current->world_scene_id != slot.world_scene_id) {
    return false;
  }
  ScenarioRuntime* const runtime{m_manager->world_runtime(slot.world_scene_id)};
  const Character::RuntimeCharacter* const character{
      runtime == nullptr ? nullptr : runtime->character_runtime().find(current->character_id)};
  // TEMPORARY: OpenNomad does not yet model Runtime's separate current-character
  // spatial trigger proxy/contact-state machinery. controller_enabled currently
  // prevents scripted presentation motion from being mistaken for native spatial-
  // proxy contact updates. Remove this guard only when the Runtime-style proxy is
  // implemented.
  if (character == nullptr || !character->active || !character->area_present ||
      !character->controller_enabled) {
    return false;
  }
  return std::ranges::any_of(m_active_zones, [&contact](const ActiveZoneRef& active) {
    return active.resident_slot == contact.resident_slot && active.source == contact.source &&
           active.area_id == contact.area_id && active.scene_id == contact.scene_id &&
           active.zone.zone_id == contact.zone.zone_id &&
           active.zone.event_offsets == contact.zone.event_offsets;
  }) && zone_contact_spatially_matches(contact);
}

bool ScenarioStartupController::zone_contact_reporting_enabled(
    const ActiveZoneRef& active_zone) const {
  if (m_manager == nullptr || active_zone.resident_slot >= m_area_slots.size()) {
    return false;
  }
  const RuntimeAreaSlot& slot{m_area_slots.at(active_zone.resident_slot)};
  const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
  if (!current.has_value() || current->world_scene_id != slot.world_scene_id) {
    return false;
  }
  ScenarioRuntime* const runtime{m_manager->world_runtime(slot.world_scene_id)};
  const Character::RuntimeCharacter* const character{
      runtime == nullptr ? nullptr : runtime->character_runtime().find(current->character_id)};
  // TEMPORARY: See the contact overload above. Fresh contacts must remain gated
  // until OpenNomad has a Runtime-style current-character spatial trigger proxy.
  if (character == nullptr || !character->active || !character->area_present ||
      !character->controller_enabled) {
    return false;
  }
  return zone_contains_runtime_xz(active_zone.zone, character->transform.translation) &&
         active_zone.zone.accepts_orientation(character->serialized_orientation_units);
}

std::expected<void, std::string> ScenarioStartupController::create_zone_contact(
    const ActiveZoneRef& active_zone) {
  const bool exists{std::ranges::any_of(
      m_zone_contacts, [&active_zone](const std::unique_ptr<ZoneContactContext>& existing) {
        return existing != nullptr && existing->resident_slot == active_zone.resident_slot &&
               existing->source == active_zone.source && existing->area_id == active_zone.area_id &&
               existing->scene_id == active_zone.scene_id &&
               existing->zone.zone_id == active_zone.zone.zone_id &&
               existing->zone.event_offsets == active_zone.zone.event_offsets;
      })};
  if (exists) {
    return {};
  }
  if (m_zone_contacts.size() >= 16U) {
    App::Log::warn(LogCategory::Scenario,
        "zone {} contact ignored — recovered spatial-contact capacity 16 reached",
        active_zone.zone.zone_id);
    return {};
  }
  if (active_zone.resident_slot >= m_area_slots.size()) {
    return std::expected<void, std::string>{std::unexpect, "zone resident slot is out of range"};
  }
  const RuntimeAreaSlot& slot{m_area_slots.at(active_zone.resident_slot)};
  std::span<const std::byte> record_bytes;
  if (active_zone.source == ActiveZoneSource::k_area) {
    if (slot.primary.has_value()) {
      record_bytes = slot.primary->record_bytes();
    }
  } else if (slot.scene.has_value()) {
    record_bytes = slot.scene->record_bytes();
  }
  if (record_bytes.empty()) {
    return std::expected<void, std::string>{
        std::unexpect, "zone owner record is no longer resident"};
  }

  auto contact{std::make_unique<ZoneContactContext>()};
  contact->resident_slot = active_zone.resident_slot;
  contact->source = active_zone.source;
  contact->area_id = active_zone.area_id;
  contact->scene_id = active_zone.scene_id;
  contact->zone = active_zone.zone;
  contact->script = std::make_unique<Script::AreaScriptRuntime>(record_bytes);
  const auto entry_or_missing = [](const std::uint32_t offset) -> std::optional<std::size_t> {
    return offset == 0U ? std::nullopt : std::optional<std::size_t>{offset};
  };
  if (auto entries{contact->script->set_event_entries(Script::AreaScriptEventEntries{
          .event1 = entry_or_missing(active_zone.zone.event_offsets.at(0)),
          .event2 = entry_or_missing(active_zone.zone.event_offsets.at(1)),
          .event3 = entry_or_missing(active_zone.zone.event_offsets.at(2))})};
      !entries) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("zone {} event entries: {}", active_zone.zone.zone_id, entries.error())};
  }
  bind_scene_compact_services(
      *contact->script, active_zone.resident_slot, active_zone.source == ActiveZoneSource::k_scene);
  if (contact->script->event_entries().event1.has_value()) {
    contact->script->queue_event(1);
  }
  contact->script->activate();
  App::Log::info(LogCategory::Scenario,
      "zone {} first contact / event 1 — source={} slot={}",
      active_zone.zone.zone_id,
      active_zone.source == ActiveZoneSource::k_area ? "AREA" : "SCENE",
      active_zone.resident_slot);
  m_zone_contacts.push_back(std::move(contact));
  return {};
}

std::expected<void, std::string> ScenarioStartupController::service_zone_contacts(
    const float delta_seconds) {
  std::erase_if(m_zone_contacts, [this](const std::unique_ptr<ZoneContactContext>& contact) {
    if (contact != nullptr && zone_contact_backing_resident(*contact)) {
      return false;
    }
    if (contact != nullptr) {
      App::Log::debug(LogCategory::Scenario,
          "zone {} contact removed — backing record is no longer resident",
          contact->zone.zone_id);
    }
    return true;
  });

  for (const ActiveZoneRef& active_zone : m_active_zones) {
    const bool already_reported{std::ranges::any_of(
        m_zone_contacts, [&active_zone](const std::unique_ptr<ZoneContactContext>& existing) {
          return existing != nullptr && existing->resident_slot == active_zone.resident_slot &&
                 existing->source == active_zone.source &&
                 existing->area_id == active_zone.area_id &&
                 existing->scene_id == active_zone.scene_id &&
                 existing->zone.zone_id == active_zone.zone.zone_id &&
                 existing->zone.event_offsets == active_zone.zone.event_offsets;
        })};
    const bool qualifies{zone_contact_reporting_enabled(active_zone)};
    const std::uint64_t diagnostic_identity{
      (static_cast<std::uint64_t>(active_zone.resident_slot) << 48U) |
      (static_cast<std::uint64_t>(active_zone.source) << 40U) |
      static_cast<std::uint16_t>(active_zone.zone.zone_id)};
    auto diagnostic{std::ranges::find_if(m_zone_qualification_diagnostics,
      [diagnostic_identity](const ZoneQualificationDiagnostic& candidate) {
        return candidate.identity == diagnostic_identity;
      })};
    if (diagnostic == m_zone_qualification_diagnostics.end() ||
      diagnostic->qualifies != qualifies) {
      if (diagnostic == m_zone_qualification_diagnostics.end()) {
      m_zone_qualification_diagnostics.push_back(
        ZoneQualificationDiagnostic{.identity = diagnostic_identity, .qualifies = qualifies});
      } else {
      diagnostic->qualifies = qualifies;
      }
      const RuntimeAreaSlot& slot{m_area_slots.at(active_zone.resident_slot)};
      const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
      const std::int16_t character_id{
        current.has_value() ? current->character_id : std::int16_t{-1}};
      const ScenarioRuntime* const runtime{
        current.has_value() && current->world_scene_id == slot.world_scene_id
          ? m_manager->world_runtime(slot.world_scene_id)
          : nullptr};
      const Character::RuntimeCharacter* const character{
        runtime == nullptr ? nullptr : runtime->character_runtime().find(character_id)};
      const bool spatial{character != nullptr &&
               zone_contains_runtime_xz(active_zone.zone, character->transform.translation)};
      const bool orientation{
        character != nullptr &&
        active_zone.zone.accepts_orientation(character->serialized_orientation_units)};
      App::Log::info(LogCategory::Scenario,
        "ZoneQualification — source={} ownerSlot={} area={} scene={} zone={} qualifies={} "
        "current={} active={} areaPresent={} controller={} serialized=({},{},{}) "
        "runtime=({:.3f},{:.3f},{:.3f}) spatial={} orientation={}",
        active_zone.source == ActiveZoneSource::k_area ? "AREA" : "SCENE",
        active_zone.resident_slot,
        active_zone.area_id,
        active_zone.scene_id,
        active_zone.zone.zone_id,
        qualifies,
        character_id,
        character != nullptr && character->active,
        character != nullptr && character->area_present,
        character != nullptr && character->controller_enabled,
        character == nullptr ? 0 : character->serialized_area_position.at(0),
        character == nullptr ? 0 : character->serialized_area_position.at(1),
        character == nullptr ? 0 : character->serialized_area_position.at(2),
        character == nullptr ? 0.0F : character->transform.translation.x,
        character == nullptr ? 0.0F : character->transform.translation.y,
        character == nullptr ? 0.0F : character->transform.translation.z,
        spatial,
        orientation);
    }
    if (!already_reported && qualifies) {
      if (auto created{create_zone_contact(active_zone)}; !created) {
        return created;
      }
    }
  }

  std::erase_if(
      m_zone_contacts, [this, delta_seconds](const std::unique_ptr<ZoneContactContext>& contact) {
        if (contact == nullptr || contact->script == nullptr) {
          return true;
        }
        Script::AreaScriptRuntime& script{*contact->script};
        const bool spatial_match{zone_contact_spatially_matches(*contact)};
        const bool active_zone{std::ranges::any_of(
            m_active_zones, [contact_ptr = contact.get()](const ActiveZoneRef& active) {
              return active.resident_slot == contact_ptr->resident_slot &&
                     active.source == contact_ptr->source &&
                     active.area_id == contact_ptr->area_id &&
                     active.scene_id == contact_ptr->scene_id &&
                     active.zone.zone_id == contact_ptr->zone.zone_id &&
                     active.zone.event_offsets == contact_ptr->zone.event_offsets;
            })};
        if (!spatial_match && !contact->departure_queued &&
            script.event_entries().event3.has_value()) {
          script.queue_event(3);
          contact->departure_queued = true;
        }
        if (auto serviced{service_scx_script_wait(script, contact->resident_slot)}; !serviced) {
          App::Log::warn(LogCategory::Script,
              "zone {} SCX-script wait failed: {}",
              contact->zone.zone_id,
              serviced.error());
        }
        if (auto serviced{service_character_script_wait(script, contact->resident_slot)};
            !serviced) {
          App::Log::warn(LogCategory::Script,
              "zone {} character-script wait failed: {}",
              contact->zone.zone_id,
              serviced.error());
        }
              const std::optional<std::uint16_t> previous_event{script.active_event()};
        const Script::AreaScriptState state{script.run(delta_seconds)};
              if (previous_event.has_value() && !script.active_event().has_value()) {
                App::Log::info(LogCategory::Script,
                "CompactEventEnded — source=ZONE ownerSlot={} area={} scene={} zone={} event={} "
                "ip=+{:#x}",
                contact->resident_slot,
                contact->area_id,
                contact->scene_id,
                contact->zone.zone_id,
                previous_event.value(),
                script.instruction_pointer());
              }
        if (state == Script::AreaScriptState::k_paused_unsupported ||
            state == Script::AreaScriptState::k_failed) {
          App::Log::warn(LogCategory::Script,
              "zone {} compact VM {}: {}",
              contact->zone.zone_id,
              state == Script::AreaScriptState::k_failed ? "failed" : "paused",
              script.pause_info().reason_text);
        }
        const bool idle{script.state() == Script::AreaScriptState::k_ready};
        if ((!spatial_match || !active_zone || !zone_contact_backing_resident(*contact)) && idle) {
          App::Log::debug(LogCategory::Scenario, "zone {} event completed", contact->zone.zone_id);
          return true;
        }
        return false;
      });
  return {};
}

std::expected<void, std::string> ScenarioStartupController::tick(const float delta_seconds) {
  APP_PROFILE_FUNCTION();

  if (!m_initialized) {
    m_last_error = "startup not initialized";
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }

  m_ticked = true;
  if (auto cameras{service_camera_completions()}; !cameras) {
    m_last_error = cameras.error();
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }

  // 0x3D is a session-global scheduling takeover. Suppress all resident AREA,
  // SCENE and contact compact contexts while it owns the dispatcher; structured
  // world runtimes continue to advance in ScenarioEngine.
  bool resumed_after_dialog{false};
  if (m_dialog_takeover_active) {
    if (m_manager == nullptr) {
      m_last_error = "dialog takeover has no scenario manager";
      return std::expected<void, std::string>{std::unexpect, m_last_error};
    }

    Dialog::DialogRuntime& dialog{m_manager->dialog_runtime()};
    if (dialog.state() == Dialog::DialogState::k_failed) {
      m_last_error = fmt::format("dialog takeover failed: {}", dialog.last_error());
      return std::expected<void, std::string>{std::unexpect, m_last_error};
    }
    if (dialog.completed()) {
      if (!dialog.take_completion()) {
        m_last_error = "dialog takeover completion could not be consumed";
        return std::expected<void, std::string>{std::unexpect, m_last_error};
      }
      const std::int16_t completed_id{m_dialog_takeover_id.value_or(-1)};
      m_dialog_takeover_active = false;
      m_dialog_takeover_id.reset();
      resumed_after_dialog = true;
      record("DialogTakeover.Completed", fmt::format("id={}", completed_id));
      App::Log::info(
          LogCategory::Scenario, "Dialog {} completed — restoring AREA scheduling", completed_id);
    } else if (dialog.active()) {
      return {};
    } else {
      m_last_error = "dialog takeover lost its active dialog without completion";
      return std::expected<void, std::string>{std::unexpect, m_last_error};
    }
  }

  if (m_area_transition.has_value()) {
    if (auto transitioned{service_area_transition()}; !transitioned) {
      return transitioned;
    }
  }

  if (resumed_after_dialog) {
    if (const Script::AreaScriptRuntime* const active{area_script()}; active != nullptr) {
      if (m_area_script_sequences.at(m_active_area_slot) == 1U) {
        record("AreaScript.ResumedAfterDialog",
            fmt::format("ip={:#x}", active->instruction_pointer()));
      } else {
        record("AreaScript.ResumedAfterDialog",
            fmt::format("area={} slot={} ip={:#x}",
                active_area_id(),
                m_active_area_slot,
                active->instruction_pointer()));
      }
      App::Log::info(LogCategory::Script,
          "AREA {} resumed after dialog — slot={} ip={:#x}",
          active_area_id(),
          m_active_area_slot,
          active->instruction_pointer());
    }
  }

  if (auto areas{service_area_scripts(delta_seconds)}; !areas) {
    m_last_error = areas.error();
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }

  // Attached SCENE contexts are separate registrations created after their
  // owning AREA context. Service them after resident primary AREA contexts.
  service_scene_scripts(delta_seconds);

  // Input -> enabled CTL controller -> accepted position -> zone contacts.
  // Phase 4.2 inserts physics/collision resolution between the CTL candidate
  // and the accepted position without changing this ordering.
  service_ctl_controller(delta_seconds);

  if (auto zones{service_zone_contacts(delta_seconds)}; !zones) {
    m_last_error = zones.error();
    return std::expected<void, std::string>{std::unexpect, m_last_error};
  }
  return {};
}

std::expected<void, std::string> ScenarioStartupController::complete_interface(
    const InterfaceCompletion& completion) {
  APP_PROFILE_FUNCTION();

  for (std::size_t index{0}; index < m_area_slots.size(); ++index) {
    std::optional<Script::AreaScriptRuntime>& primary_script{m_area_scripts.at(index)};
    if (primary_script.has_value()) {
      if (auto completed{primary_script->complete_interface_wait(completion)}; completed) {
        return completed;
      }
    }
    RuntimeAreaSlot& slot{m_area_slots.at(index)};
    if (slot.scene_script.has_value()) {
      if (auto completed{slot.scene_script->complete_interface_wait(completion)}; completed) {
        return completed;
      }
    }
  }
  for (const std::unique_ptr<ZoneContactContext>& contact : m_zone_contacts) {
    if (contact == nullptr || contact->script == nullptr) {
      continue;
    }
    if (auto completed{contact->script->complete_interface_wait(completion)}; completed) {
      return completed;
    }
  }
  return std::expected<void, std::string>{
      std::unexpect, "interface completion does not match an active compact IAM context"};
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
  const RuntimeAreaSlot& slot{m_area_slots.at(m_active_area_slot)};
  return slot.primary.has_value() ? &*slot.primary : nullptr;
}

std::int32_t ScenarioStartupController::active_area_id() const {
  return m_area_slots.at(m_active_area_slot).primary_area_id;
}

const RuntimeAreaSlot* ScenarioStartupController::runtime_area_slot(const std::size_t index) const {
  return index < m_area_slots.size() ? &m_area_slots.at(index) : nullptr;
}

std::optional<std::int32_t> ScenarioStartupController::area_mapping(
    const std::int32_t area_id) const {
  if (m_manager == nullptr || m_manager->game_state() == nullptr) {
    return std::nullopt;
  }
  const auto value{m_manager->game_state()->area_mapping(area_id)};
  return value.has_value() ? std::optional<std::int32_t>{value.value()} : std::nullopt;
}

std::span<const std::int16_t> ScenarioStartupController::area_mapping_entries() const {
  return m_manager != nullptr && m_manager->game_state() != nullptr
             ? m_manager->game_state()->area_mappings()
             : std::span<const std::int16_t>{};
}

const Script::AreaScriptRuntime* ScenarioStartupController::area_script() const {
  return area_script(m_active_area_slot);
}

const Script::AreaScriptRuntime* ScenarioStartupController::area_script(
    const std::size_t resident_slot) const {
  if (resident_slot >= m_area_scripts.size()) {
    return nullptr;
  }
  const std::optional<Script::AreaScriptRuntime>& script{m_area_scripts.at(resident_slot)};
  if (!script.has_value()) {
    return nullptr;
  }
  return &script.value();
}

std::optional<std::int16_t> ScenarioStartupController::current_controlled_character() const {
  if (m_manager == nullptr) {
    return std::nullopt;
  }
  const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
  return current.has_value() ? std::optional<std::int16_t>{current->character_id} : std::nullopt;
}

}  // namespace App

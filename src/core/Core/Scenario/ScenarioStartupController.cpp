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
#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/GameState.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Omikron/IamStart.hpp"
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
constexpr std::string_view K_IAM_AREA_PATH{"IAM/AREA"};
constexpr std::string_view K_IAM_SCENE_PATH{"IAM/SCENE"};
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
    return std::expected<PreparedAreaWorld, std::string>{std::unexpect,
        fmt::format("IAM/AREA record {} has no scenario SCX name", area_id)};
  }

  const std::string model_name{area_record.model3do_name()};
  std::optional<std::string> decor_path;
  if (!model_name.empty()) {
    decor_path = dependency_path(K_DECOR_DIRECTORY, model_name, K_3DO_EXTENSION);
  }
  const std::string scenario_path{
      dependency_path(K_SCPTDATA_DIRECTORY, scx_name, K_SCX_EXTENSION)};

  auto world{manager.load_world_context(world_scene_id, decor_path, scenario_path)};
  if (!world) {
    return std::expected<PreparedAreaWorld, std::string>{std::unexpect,
        fmt::format("world scenario load for AREA {}: {}", area_id, world.error())};
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
  m_area_archive.reset();
  m_scene_archive.reset();
  for (std::size_t index{0}; index < m_area_slots.size(); ++index) {
    RuntimeAreaSlot& slot{m_area_slots.at(index)};
    slot.scene_script.reset();
    slot.scene.reset();
    slot.primary.reset();
    slot.primary_area_id = -1;
    slot.secondary_area_id = -1;
    slot.scene_id = -1;
    slot.world_scene_id = static_cast<std::uint32_t>(index);
  }
  m_active_area_slot = 0;
  m_area_script_owner_slot = 0;
  m_area_transition.reset();
  m_next_area_transition_generation = 1;
  m_area_script.reset();
  m_start_bytes.clear();
  m_area_archive_bytes.clear();
  m_scene_archive_bytes.clear();
  m_area_mapping.clear();
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
  RuntimeAreaSlot& area_slot{m_area_slots.at(m_active_area_slot)};
  area_slot.primary.emplace(std::move(parsed_record).value());
  area_slot.primary_area_id = m_initial_area_id;
  area_slot.secondary_area_id = m_linked_area_id;
  const Omikron::IamAreaRecord& area_record_view{*area_slot.primary};
  const std::size_t record_size{area_record_view.record_size()};
  const std::uint32_t script_offset{area_record_view.script_offset()};
  record("IAM_AREA.RecordLoaded", fmt::format("id={} size={:#x}", area_id, record_size));
  record("IAM_AREA.Parsed", fmt::format("scriptOffset={:#x}", script_offset));

  // 3. Dependencies in the original loader's order. The names are supplied
  // by the active IAM/AREA record; GRID is merely the value used by area 118,
  // not a special world-scenario role.

  // Decor CPU ownership lives in the world context. Startup retains only the
  // initial dependency path/state for historical diagnostics.
  auto prepared{prepare_area_world(
      manager, area_slot.world_scene_id, m_initial_area_id, area_record_view)};
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

  // 4. Area script context: create, queue event/state 1, activate. The
  // first interpreter tick runs in tick().
  m_area_script.emplace(area_record_view.script_bytes());
  Script::AreaScriptRuntime& area_script{*m_area_script};
  // This main AREA context continues to own its initial resident slot even
  // after a later compact handoff changes presentation to the alternate slot.
  const std::size_t area_script_owner_slot{m_active_area_slot};
  m_area_script_owner_slot = area_script_owner_slot;

  area_script.set_area_transition_sink(
      [this](const Script::AreaTransitionRequest& request)
          -> std::expected<Script::AreaTransitionHandle, std::string> {
        if (m_manager == nullptr || !m_area_archive.has_value()) {
          return std::expected<Script::AreaTransitionHandle, std::string>{
              std::unexpect, "AREA transition coordinator is not initialized"};
        }
        if (m_area_transition.has_value()) {
          return std::expected<Script::AreaTransitionHandle, std::string>{
              std::unexpect, "AREA transition coordinator is busy"};
        }
        if (request.target_area_id < 0) {
          return std::expected<Script::AreaTransitionHandle, std::string>{std::unexpect,
              fmt::format("target AREA ID {} is negative", request.target_area_id)};
        }
        if (!m_area_slots.at(m_active_area_slot).primary.has_value()) {
          return std::expected<Script::AreaTransitionHandle, std::string>{
              std::unexpect, "active resident AREA slot is empty"};
        }

        const std::size_t destination_slot{m_active_area_slot == 0U ? 1U : 0U};
        const Script::AreaTransitionHandle handle{
            .generation = m_next_area_transition_generation++};
        m_area_transition.emplace(PendingAreaTransition{.handle = handle,
            .request = request,
            .source_slot = m_active_area_slot,
            .destination_slot = destination_slot,
            .error = {}});
        record("AreaTransition.Accepted",
            fmt::format("generation={} sourceSlot={} destinationSlot={} target={}",
                handle.generation,
                m_active_area_slot,
                destination_slot,
                request.target_area_id));
        App::Log::info(LogCategory::Scenario,
            "AREA opcode 0x2F — accepted transition to AREA {} as generation {}",
            request.target_area_id,
            handle.generation);
        return handle;
      });

  area_script.set_area_release_sink(
      [this](const Script::AreaReleaseRequest& request) { return release_area(request); });
  area_script.set_area_scene_attach_sink(
      [this](const Script::AreaSceneAttachRequest& request) { return attach_area_scene(request); });
  area_script.set_area_address_placement_sink(
      [this](const Script::AreaAddressPlacementRequest& request) {
        return place_current_character_at_address(request);
      });
  area_script.set_address_flag_sink(
      [this](const Script::AreaAddressFlagRequest& request) { return set_address_flag(request); });
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
        if (m_manager == nullptr) {
          return std::expected<void, std::string>{
              std::unexpect, "scenario manager is not available"};
        }

        if (auto started{m_manager->start_dialog(static_cast<std::uint16_t>(request.dialog_id))};
            !started) {
          return std::expected<void, std::string>{std::unexpect, started.error()};
        }

        m_dialog_takeover_active = true;
        m_dialog_takeover_id = request.dialog_id;
        record("AreaScript.DialogStarted", fmt::format("id={}", request.dialog_id));
        record("DialogTakeover.Entered", fmt::format("id={}", request.dialog_id));
        App::Log::info(
            LogCategory::Script, "AREA opcode 0x3D — started dialog {}", request.dialog_id);
        return {};
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
          "AREA generic SCX launch — started world script {} '{}' from '{}' as instance {}",
          request.script_id,
          scripts.at(index).name,
          context->scenario_path,
          created.value());
      return created;
    }
    return std::expected<std::size_t, std::string>{std::unexpect,
        fmt::format("SCX script ID {} not found in active world", request.script_id)};
  });

  area_script.set_character_script_sink(
      [this, area_script_owner_slot](const Script::AreaCharacterScriptRequest& request) {
        return launch_character_script(area_script_owner_slot, request);
      });

  area_script.set_character_activation_sink(
      [this, area_script_owner_slot](const Script::AreaCharacterActivationRequest& request)
          -> std::expected<void, std::string> {
        if (request.character_id == -1) {
          return set_current_character_presentation(true);
        }
        const RuntimeAreaSlot& owner_slot{m_area_slots.at(area_script_owner_slot)};
        if (m_manager == nullptr || !owner_slot.primary.has_value()) {
          return std::expected<void, std::string>{
              std::unexpect, "main AREA owner has no character runtime"};
        }
        ScenarioRuntime* const scenario_runtime{m_manager->world_runtime(owner_slot.world_scene_id)};
        if (scenario_runtime == nullptr) {
          return std::expected<void, std::string>{
              std::unexpect, "main AREA owner has no loaded world character runtime"};
        }

        std::expected<void, std::string> activated;
        if (owner_slot.scene.has_value() &&
            owner_slot.scene->character_by_id(request.character_id).has_value()) {
          activated = scenario_runtime->character_runtime().materialize_scene_characters(
              owner_slot.primary_area_id, owner_slot.scene_id, *owner_slot.scene);
        } else {
          activated = scenario_runtime->activate_character(
              owner_slot.primary_area_id, *owner_slot.primary, request);
        }
        if (!activated) {
          return activated;
        }

        const Character::RuntimeCharacter* character{
            scenario_runtime->character_runtime().find(request.character_id)};
        if (character != nullptr) {
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
      });

  area_script.set_presentation_sink([this](const Script::AreaPresentationRequest& request) {
    if (m_manager == nullptr) {
      App::Log::warn(LogCategory::Scenario,
          "AREA presentation mode {} requested without a scenario manager",
          request.mode);
      return;
    }

    const WorldSceneContext* context{m_manager->active_world_context()};
    if (context == nullptr) {
      App::Log::warn(LogCategory::Scenario,
          "AREA presentation mode {} requested without an active world context",
          request.mode);
      return;
    }

    m_manager->world_presentation().enqueue_fade(WorldFadeCommand{.scene_id = context->scene_id,
        .scene_generation = context->generation,
        .mode = request.mode,
        .color = request.color,
        .duration_units = request.operand_b,
        .operand_c = request.operand_c});

    record("AreaScript.PresentationRequested",
        fmt::format("mode={} color={:#010x} duration={} arg={}",
            request.mode,
            request.color,
            request.operand_b,
            request.operand_c));
  });

  area_script.set_cinematic_letterbox_sink(
      [this](const Script::AreaCinematicLetterboxRequest& request) {
        if (m_manager == nullptr) {
          App::Log::warn(LogCategory::Scenario,
              "AREA cinematic letterbox requested without a scenario manager");
          return;
        }

        const WorldSceneContext* context{m_manager->active_world_context()};
        if (context == nullptr) {
          App::Log::warn(LogCategory::Scenario,
              "AREA cinematic letterbox requested without an active world context");
          return;
        }

        m_manager->world_presentation().enqueue_letterbox(
            WorldLetterboxCommand{.scene_id = context->scene_id,
                .scene_generation = context->generation,
                .enabled = request.enabled});
        App::Log::debug(LogCategory::Scenario,
            "cinematic letterbox requested — enabled={}",
            request.enabled);
      });

  area_script.set_camera_sink([this](const Script::AreaCameraRequest& request) {
    const RuntimeAreaSlot& active_slot{m_area_slots.at(m_active_area_slot)};
    if (m_manager == nullptr || !active_slot.primary.has_value()) {
      App::Log::warn(LogCategory::Scenario,
          "AREA camera {} requested without an active AREA/world owner",
          request.camera_id);
      return;
    }

    const auto camera{active_resident_camera(static_cast<std::int16_t>(request.camera_id))};
    if (!camera.has_value()) {
      App::Log::warn(
          LogCategory::Scenario, "AREA camera {} not found in table 6", request.camera_id);
      return;
    }

    const WorldSceneContext* context{m_manager->active_world_context()};
    if (context == nullptr) {
      App::Log::warn(LogCategory::Scenario,
          "AREA camera {} requested without an active world context",
          request.camera_id);
      return;
    }

    m_manager->world_presentation().enqueue_camera(WorldCameraCommand{.scene_id = context->scene_id,
        .scene_generation = context->generation,
        .camera_id = request.camera_id,
        .serialized_eye = camera->serialized_eye,
        .serialized_target = camera->serialized_target,
        .runtime_eye = Runtime::area_position_to_inches(camera->serialized_eye),
        .runtime_target = Runtime::area_position_to_inches(camera->serialized_target),
        .duration_units = request.duration_units,
        .flags = request.flags,
        .wait_for_completion = request.wait_for_completion,
        .camera_type = camera->camera_type,
        .roll_units = camera->roll_units,
        .horizontal_fov_units = camera->horizontal_fov_units,
        .roll_degrees = Runtime::area_angle_to_degrees(camera->roll_units),
        .horizontal_fov_degrees = Runtime::area_angle_to_degrees(camera->horizontal_fov_units),
        .field_20 = camera->field_20,
        .field_22 = camera->field_22,
        .tail_fields = camera->tail_fields});

    record("AreaScript.CameraRequested",
        fmt::format("id={} duration={} flags={} type={} hFov={}deg",
            request.camera_id,
            request.duration_units,
            request.flags,
            camera->camera_type,
            Runtime::area_angle_to_degrees(camera->horizontal_fov_units)));
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

std::expected<void, std::string> ScenarioStartupController::service_area_transition() {
  if (!m_area_transition.has_value()) {
    return {};
  }

  PendingAreaTransition& transition{m_area_transition.value()};
  if (!transition.error.empty()) {
    return std::expected<void, std::string>{std::unexpect, transition.error};
  }

  const auto fail_transition = [this, &transition](std::string error)
      -> std::expected<void, std::string> {
    transition.error = fmt::format(
        "AREA transition to {} failed: {}", transition.request.target_area_id, error);
    m_last_error = transition.error;
    record("AreaTransition.Failed", transition.error);
    App::Log::error(LogCategory::Scenario, "{}", transition.error);
    return std::expected<void, std::string>{std::unexpect, transition.error};
  };

  if (m_manager == nullptr || !m_area_archive.has_value() || !m_area_script.has_value()) {
    return fail_transition("coordinator ownership is unavailable");
  }
  if (transition.source_slot != m_active_area_slot) {
    return fail_transition("active resident AREA slot changed before commit");
  }

  Script::AreaScriptRuntime& area_script{m_area_script.value()};
  if (area_script.state() != Script::AreaScriptState::k_waiting ||
      area_script.wait_info().kind != Script::AreaWaitKind::k_area_transition ||
      !area_script.wait_info().area_transition_handle.has_value() ||
      area_script.wait_info().area_transition_handle.value() != transition.handle) {
    return fail_transition("requesting AREA context is not waiting on the accepted generation");
  }

  auto record_span{m_area_archive->read_record(
      static_cast<std::uint32_t>(transition.request.target_area_id))};
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
      "AREA {} prepared in resident slot {} — world remains inactive, resume ip={:#x}",
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

std::optional<Omikron::IamCameraRecord> ScenarioStartupController::active_resident_camera(
    const std::int16_t camera_id) const {
  const RuntimeAreaSlot& slot{m_area_slots.at(m_active_area_slot)};
  if (!slot.primary.has_value()) {
    return std::nullopt;
  }
  if (const auto area_camera{slot.primary->camera_by_id(camera_id)}; area_camera.has_value()) {
    return area_camera;
  }
  return slot.scene.has_value() ? slot.scene->camera_by_id(camera_id) : std::nullopt;
}

void ScenarioStartupController::bind_scene_compact_services(
    Script::AreaScriptRuntime& runtime, const std::size_t owner_slot) {
  runtime.set_area_release_sink(
      [this](const Script::AreaReleaseRequest& request) { return release_area(request); });
  runtime.set_area_scene_attach_sink(
      [this](const Script::AreaSceneAttachRequest& request) { return attach_area_scene(request); });
  runtime.set_area_address_placement_sink(
      [this](const Script::AreaAddressPlacementRequest& request) {
        return place_current_character_at_address(request);
      });
  runtime.set_address_flag_sink(
      [this](const Script::AreaAddressFlagRequest& request) { return set_address_flag(request); });
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
        if (m_manager == nullptr) {
          return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
        }
        return m_manager->start_dialog(static_cast<std::uint16_t>(request.dialog_id));
      });
  runtime.set_music_sink([this](const Audio::MusicTrackRequest& request) {
    if (m_audio != nullptr) {
      if (auto result{m_audio->play_music_track(request)}; !result) {
        App::Log::warn(LogCategory::Music, "SCENE music {} failed: {}", request.track_id, result.error());
      }
    }
  });
  runtime.set_camera_sink([this](const Script::AreaCameraRequest& request) {
    if (m_manager == nullptr) {
      return;
    }
    const auto camera{active_resident_camera(static_cast<std::int16_t>(request.camera_id))};
    const WorldSceneContext* context{m_manager->active_world_context()};
    if (!camera.has_value() || context == nullptr) {
      App::Log::warn(LogCategory::Scenario,
          "SCENE camera {} has no active resident camera/world owner",
          request.camera_id);
      return;
    }
    m_manager->world_presentation().enqueue_camera(WorldCameraCommand{.scene_id = context->scene_id,
        .scene_generation = context->generation,
        .camera_id = request.camera_id,
        .serialized_eye = camera->serialized_eye,
        .serialized_target = camera->serialized_target,
        .runtime_eye = Runtime::area_position_to_inches(camera->serialized_eye),
        .runtime_target = Runtime::area_position_to_inches(camera->serialized_target),
        .duration_units = request.duration_units,
        .flags = request.flags,
        .wait_for_completion = request.wait_for_completion,
        .camera_type = camera->camera_type,
        .roll_units = camera->roll_units,
        .horizontal_fov_units = camera->horizontal_fov_units,
        .roll_degrees = Runtime::area_angle_to_degrees(camera->roll_units),
        .horizontal_fov_degrees = Runtime::area_angle_to_degrees(camera->horizontal_fov_units),
        .field_20 = camera->field_20,
        .field_22 = camera->field_22,
        .tail_fields = camera->tail_fields});
  });
  runtime.set_presentation_sink([this](const Script::AreaPresentationRequest& request) {
    if (m_manager == nullptr) {
      return;
    }
    const WorldSceneContext* context{m_manager->active_world_context()};
    if (context != nullptr) {
      m_manager->world_presentation().enqueue_fade(WorldFadeCommand{.scene_id = context->scene_id,
          .scene_generation = context->generation,
          .mode = request.mode,
          .color = request.color,
          .duration_units = request.operand_b,
          .operand_c = request.operand_c});
    }
  });
  runtime.set_cinematic_letterbox_sink(
      [this](const Script::AreaCinematicLetterboxRequest& request) {
        if (m_manager == nullptr) {
          return;
        }
        const WorldSceneContext* context{m_manager->active_world_context()};
        if (context != nullptr) {
          m_manager->world_presentation().enqueue_letterbox(
              WorldLetterboxCommand{.scene_id = context->scene_id,
                  .scene_generation = context->generation,
                  .enabled = request.enabled});
        }
      });
  runtime.set_scx_script_sink(
      [this, owner_slot](const Script::AreaScxScriptRequest& request)
          -> std::expected<std::size_t, std::string> {
        if (m_manager == nullptr) {
          return std::expected<std::size_t, std::string>{std::unexpect, "scenario manager is not available"};
        }
        const RuntimeAreaSlot& slot{m_area_slots.at(owner_slot)};
        const Omikron::ScxData* scx{m_manager->world_context_scx(slot.world_scene_id)};
        ScenarioRuntime* scenario_runtime{m_manager->world_runtime(slot.world_scene_id)};
        if (scx == nullptr || scenario_runtime == nullptr) {
          return std::expected<std::size_t, std::string>{std::unexpect,
              "SCENE owner has no loaded SCX runtime"};
        }
        for (std::size_t index{0}; index < scx->scripts.size(); ++index) {
          if (scx->scripts.at(index).script_id == request.script_id) {
            return scenario_runtime->spawn_script_instance(index);
          }
        }
        return std::expected<std::size_t, std::string>{std::unexpect,
            fmt::format("SCENE owner SCX script ID {} was not found", request.script_id)};
      });
  runtime.set_character_script_sink(
      [this, owner_slot](const Script::AreaCharacterScriptRequest& request)
          -> std::expected<std::size_t, std::string> {
        return launch_character_script(owner_slot, request);
      });
  runtime.set_character_activation_sink(
      [this, owner_slot](const Script::AreaCharacterActivationRequest& request)
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
      return std::expected<void, std::string>{std::unexpect,
          "SCENE owner has no AREA character runtime"};
    }
    if (slot.scene.has_value() && slot.scene->character_by_id(request.character_id).has_value()) {
      return scenario_runtime->character_runtime().materialize_scene_characters(
          slot.primary_area_id, slot.scene_id, *slot.scene);
    }
    return scenario_runtime->activate_character(slot.primary_area_id, *slot.primary, request);
  });
  runtime.set_interface_sink([this](const InterfaceOpenRequest& request)
                                 -> std::expected<InterfaceHandle, std::string> {
    return m_dispatcher.open(request);
  });
}

std::expected<void, std::string> ScenarioStartupController::attach_area_scene(
    const Script::AreaSceneAttachRequest& request) {
  if (m_manager == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  const auto slot_index{resident_area_slot(request.area_id)};
  if (!slot_index.has_value()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("AREA {} is not resident for SCENE attachment", request.area_id)};
  }
  if (request.scene_id < 0) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("SCENE ID {} is negative", request.scene_id)};
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
  if (!parsed_scene->object_placements().empty()) {
    return std::expected<void, std::string>{std::unexpect,
        "SCENE-local object materialization is not implemented for a nonempty table 1"};
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
  if (auto materialized{scenario_runtime->character_runtime().materialize_scene_characters(
          slot.primary_area_id, slot.scene_id, *slot.scene)};
      !materialized) {
    const std::string materialize_error{std::move(materialized).error()};
    if (transfer_current) {
      if (auto returned{
              m_manager->transfer_controlled_character(slot.world_scene_id, source_world_scene_id)};
          !returned) {
        return std::expected<void, std::string>{std::unexpect,
            fmt::format(
                "SCENE character materialization failed: {}; current-character rollback failed: {}",
                materialize_error,
                returned.error())};
      }
    }
    scenario_runtime->character_runtime().dematerialize_scene_characters(
        slot.primary_area_id, slot.scene_id);
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
      slot.scene.reset();
      slot.scene_id = -1;
      return std::expected<void, std::string>{std::unexpect, switch_error};
    }
    m_active_area_slot = slot_index.value();
  }
  m_area_mapping[request.area_id] = request.scene_id;
  const Omikron::IamAreaRecord& area{slot.primary.value()};
  const Omikron::IamSceneRecord& scene{slot.scene.value()};
  App::Log::debug(LogCategory::Scenario,
      "AREA {} / SCENE {} zone-context refresh boundary retained — area zones={} scene zones={}",
      request.area_id,
      request.scene_id,
      area.table_count(2),
      scene.table_count(2));
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
  m_area_mapping.erase(request.area_id);
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
  if (current.has_value() && current->character_id == request.character_id) {
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

  if (slot.primary->character_by_id(request.character_id).has_value()) {
    if (auto materialized{runtime->character_runtime().ensure_area_character(
            slot.primary_area_id, *slot.primary, request.character_id)};
        !materialized) {
      return materialized;
    }
  } else if (slot.scene.has_value() &&
             slot.scene->character_by_id(request.character_id).has_value()) {
    if (auto materialized{runtime->character_runtime().ensure_scene_character(
            slot.primary_area_id, slot.scene_id, *slot.scene, request.character_id)};
        !materialized) {
      return materialized;
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
  m_manager->set_controlled_character(ControlledCharacterRef{
      .character_id = request.character_id, .world_scene_id = slot.world_scene_id});
  record("AreaScript.CurrentCharacterSelected",
      fmt::format("character={} world={} model={}",
          character->character_id,
          slot.world_scene_id,
          character->model_resource_name));
  App::Log::info(LogCategory::Scenario,
      "current character selected — id={} world={} model={}",
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

  // The compact trailing Scalar16 belongs to Runtime's presentation path, not
  // ScriptLaunchContext. Retain it in the request but launch SCX with its
  // neutral metadata value until that presentation path is modeled.
  auto created{scenario_runtime->spawn_character_script_instance(
      source_script_index.value(), character_id, 0)};
  if (!created) {
    return created;
  }

  const bool tracked{request.mode == Script::AreaCharacterScriptLaunchMode::k_tracked};
  const Omikron::ScxScript& script{scx->scripts.at(source_script_index.value())};
  record("AreaScript.CharacterScriptStarted",
      fmt::format(
          "target={} character={} script={} name='{}' cameraDuration={} mode={} instance={}",
          target_name,
          character_id,
          request.script_id,
          script.name,
          request.camera_duration_units,
          tracked ? "tracked" : "fire-and-forget",
          created.value()));
  App::Log::debug(LogCategory::Script,
      "AREA character-script launch — target={} character={} script {} '{}' as instance {}, {}",
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

std::expected<void, std::string> ScenarioStartupController::service_character_script_wait(
    Script::AreaScriptRuntime& area_script, const std::size_t owner_slot) {
  if (area_script.state() != Script::AreaScriptState::k_waiting ||
      area_script.wait_info().kind != Script::AreaWaitKind::k_character_script) {
    return {};
  }
  if (m_manager == nullptr || !area_script.wait_info().character_script_instance.has_value()) {
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
  record("AreaScript.CharacterScriptCompleted", fmt::format("instance={}", instance_id));
  return {};
}

void ScenarioStartupController::service_scene_scripts(const float delta_seconds) {
  for (std::size_t index{0}; index < m_area_slots.size(); ++index) {
    RuntimeAreaSlot& slot{m_area_slots.at(index)};
    if (!slot.scene_script.has_value()) {
      continue;
    }
    Script::AreaScriptRuntime& script{slot.scene_script.value()};
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
    const Script::AreaScriptState state{script.run(delta_seconds)};
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

  // AREA opcode 0x3D leaves its context running at the advanced IP. Runtime's
  // dialog mode is a global scheduling takeover, so suppress only normal AREA
  // servicing here; ScenarioEngine continues gameplay/world runtime ticks.
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

  // Tracked opcode 0x3A yields the AREA VM until its exact concrete
  // ScriptRuntime instance completes. Fire-and-forget 0x39 never enters here.
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

  // Opcodes 0x2E and 0x3C wait on the exact character-bound child returned
  // by their launch bridge. Poll their compact-context owner rather than the
  // current presentation world. An unsupported child opcode is a debugger
  // breakpoint, so it intentionally leaves the AREA context in state 4.
  if (auto serviced{service_character_script_wait(area_script, m_area_script_owner_slot)};
      !serviced) {
    m_last_error = serviced.error();
    return std::expected<void, std::string>{std::unexpect, m_last_error};
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

  if (resumed_after_dialog) {
    record("AreaScript.ResumedAfterDialog",
        fmt::format("ip={:#x}", area_script.instruction_pointer()));
    App::Log::info(LogCategory::Script,
        "AREA resumed after dialog — ip={:#x}",
        area_script.instruction_pointer());
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
  // Attached SCENE contexts are independent compact VM owners. They are
  // serviced after the parent AREA dispatch returns, never recursively from
  // opcode 0x47; a paused SCENE therefore cannot freeze its AREA parent.
  service_scene_scripts(delta_seconds);
  return {};
}

std::expected<void, std::string> ScenarioStartupController::complete_interface(
    const InterfaceCompletion& completion) {
  APP_PROFILE_FUNCTION();

  if (m_area_script.has_value()) {
    if (auto completed{m_area_script->complete_interface_wait(completion)}; completed) {
      return completed;
    }
  }
  for (RuntimeAreaSlot& slot : m_area_slots) {
    if (!slot.scene_script.has_value()) {
      continue;
    }
    if (auto completed{slot.scene_script->complete_interface_wait(completion)}; completed) {
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

std::optional<std::int16_t> ScenarioStartupController::current_controlled_character() const {
  if (m_manager == nullptr) {
    return std::nullopt;
  }
  const std::optional<ControlledCharacterRef> current{m_manager->controlled_character()};
  return current.has_value() ? std::optional<std::int16_t>{current->character_id} : std::nullopt;
}

}  // namespace App

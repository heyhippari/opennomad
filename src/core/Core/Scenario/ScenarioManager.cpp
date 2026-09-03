#include "ScenarioManager.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/GameState.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/IamCamera.hpp"
#include "Core/Omikron/IamDialog.hpp"
#include "Core/Omikron/IamStart.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Omikron/SFX.hpp"
#include "Core/Resources.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Sfx/SfxRuntime.hpp"
#include "Core/WorldPresentation.hpp"

namespace App {

namespace {

/// Runtime's dialog-camera pair controller submits the second camera with
// an authored transition duration of exactly 160 camera/scenario units.
constexpr std::int16_t K_DIALOG_CAMERA_TRANSITION_UNITS{160};

/// "SCPTDATA/Hall27.SCX" -> "Hall27": the canonical game path without
/// the archive directory and extension, for readable lifecycle messages.
std::string scenario_basename(const std::string_view path) {
  return std::filesystem::path{std::string{path}}.stem().string();
}

}  // namespace

ScenarioManager::ScenarioManager() = default;

ScenarioManager::~ScenarioManager() = default;

std::expected<void, std::string> ScenarioManager::reset_for_new_session() {
  APP_PROFILE_FUNCTION();

  m_controlled_character.reset();
  m_current_world_scene_id.reset();
  m_dialog_camera_generation.reset();
  m_game_state.reset();
  // Tear down both world contexts directly rather than through the
  // scene-id-based public helpers: a Free context's default scene_id (0)
  // collides with a resident context, so lookups are ambiguous here.
  for (WorldSceneContext& context : m_world_contexts) {
    if (context.residency != WorldSceneResidencyState::Free) {
      teardown_world_context(context);
      context.residency = WorldSceneResidencyState::Free;
      ++context.generation;
    }
  }
  m_world_presentation.clear();
  m_dialog_performance.reset();
  m_dialog_runtime.reset();
  return {};
}

std::expected<void, std::string> ScenarioManager::initialize_game_state(
    const Omikron::IamStart& start) {
  auto state{GameState::from_start(start)};
  if (!state) {
    return std::expected<void, std::string>{std::unexpect, state.error()};
  }
  m_game_state.emplace(std::move(state).value());
  return {};
}

GameState* ScenarioManager::game_state() {
  return m_game_state.has_value() ? &m_game_state.value() : nullptr;
}

const GameState* ScenarioManager::game_state() const {
  return m_game_state.has_value() ? &m_game_state.value() : nullptr;
}

std::expected<void, std::string> ScenarioManager::start_dialog(const std::uint16_t dialog_id) {
  APP_PROFILE_FUNCTION();

  if (m_dialog_archive.empty()) {
    auto loaded{load_game_file("IAM/DIALOG")};
    if (!loaded) {
      m_dialog_runtime.reset();
      return std::expected<void, std::string>{
          std::unexpect, fmt::format("Cannot load IAM/DIALOG: {}", loaded.error())};
    }
    m_dialog_archive = std::move(loaded->bytes);
  }

  auto record{Omikron::IamDialogRecord::load_from_archive(m_dialog_archive, dialog_id)};
  if (!record) {
    m_dialog_runtime.reset();
    return std::expected<void, std::string>{std::unexpect, record.error()};
  }
  auto started{m_dialog_runtime.start(std::move(record).value())};
  if (!started) {
    return started;
  }

  m_dialog_camera_generation.reset();

  App::Log::info(LogCategory::Scenario, "Dialog {} started", dialog_id);
  return {};
}

GameplayMode ScenarioManager::current_gameplay_mode() const {
  return m_gameplay_mode_slot.current_mode;
}

ScenarioIdentity ScenarioManager::gameplay_identity() const {
  return ScenarioIdentity{
      .role = ScenarioRole::GameplayMode, .slot = 0, .generation = m_gameplay_mode_slot.generation};
}

std::string_view ScenarioManager::gameplay_scenario_path() const {
  return m_gameplay_mode_slot.scenario_path;
}

std::string_view ScenarioManager::gameplay_resolved_scenario_path() const {
  return m_gameplay_mode_slot.resolved_path;
}

std::expected<void, std::string> ScenarioManager::set_gameplay_mode(const GameplayMode mode) {
  APP_PROFILE_FUNCTION();

  const std::string_view path_view{gameplay_mode_scenario_path(mode)};
  if (path_view.empty()) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("Unknown gameplay mode {}", static_cast<int>(mode))};
  }
  const std::string scenario_path{path_view};

  App::Log::debug(LogCategory::Scenario,
      "load begin: role=gameplay_mode mode={} path={}",
      gameplay_mode_name(mode),
      scenario_path);

  auto loaded{load_scenario(scenario_path)};
  if (!loaded) {
    m_gameplay_mode_slot.last_error = loaded.error();
    App::Log::error(LogCategory::Scenario,
        "load failed: role=gameplay_mode mode={}: {}",
        gameplay_mode_name(mode),
        loaded.error());
    return std::expected<void, std::string>{std::unexpect, std::move(loaded).error()};
  }

  // Build the replacement runtime from the freshly parsed package BEFORE
  // touching the resident slot, so a runtime-construction failure preserves
  // the old mode slot (and its runtime) intact.
  auto runtime{prepare_runtime(scenario_path, loaded.value())};
  if (!runtime) {
    m_gameplay_mode_slot.last_error = runtime.error();
    App::Log::error(LogCategory::Scenario,
        "load failed: role=gameplay_mode mode={}: {}",
        gameplay_mode_name(mode),
        runtime.error());
    return std::expected<void, std::string>{std::unexpect, std::move(runtime).error()};
  }

  // Stop old mode-owned executions/instances/voices, then install the
  // replacement and its runtime atomically.
  teardown_gameplay_mode_slot();
  install_gameplay_mode(mode, std::move(loaded).value(), std::move(runtime).value());

  App::Log::info(LogCategory::Scenario,
      "gameplay mode \"{}\" loaded — generation={}",
      gameplay_mode_name(mode),
      m_gameplay_mode_slot.generation);
  App::Log::debug(LogCategory::Scenario,
      "native activation semantics remain unsupported (no scripts activated)");
  return {};
}

std::expected<WorldSceneContext*, std::string> ScenarioManager::load_world_context(
    const std::uint32_t scene_id,
    std::optional<std::string> decor_path,
    std::optional<std::string> scenario_path) {
  APP_PROFILE_FUNCTION();

  WorldSceneContext* target{allocate_world_context_slot()};
  if (target == nullptr) {
    return std::expected<WorldSceneContext*, std::string>{std::unexpect,
        fmt::format("Cannot allocate world context slot for scene {}: no free or recyclable "
                    "entry (both are ResidentAttached)",
            scene_id)};
  }

  const std::size_t cache_index{static_cast<std::size_t>(target - m_world_contexts.data())};
  const WorldSceneResidencyState previous{target->residency};
  App::Log::debug(LogCategory::Scenario,
      "world context allocate: cache_index={} scene_id={} previous={}",
      cache_index,
      scene_id,
      previous == WorldSceneResidencyState::Free ? "free" : "loaded_inactive");
  App::Log::debug(LogCategory::Scenario,
      "load begin: role=world_scene cache_index={} scene_id={} path={}",
      cache_index,
      scene_id,
      scenario_path.value_or("<world-only>"));

  // Optional decor (level) model first: the recovered AREA dependency loader
  // parses the decor before the scenario SCX. Best-effort and non-fatal — a
  // failure leaves the decor empty and is logged without failing the world load.
  const std::string requested_decor{decor_path.value_or(std::string{})};
  std::string resolved_decor_path;
  std::optional<Omikron::Model3DOData> decor_model;
  if (!requested_decor.empty()) {
    if (auto decor_file{load_game_file(normalize_asset_path(requested_decor))}) {
      if (auto parsed{Omikron::Model3DO::load(std::span<const std::byte>{decor_file->bytes})}) {
        resolved_decor_path = decor_file->resolved.string();
        decor_model.emplace(std::move(parsed).value());
      } else {
        App::Log::warn(
            LogCategory::Scenario, "World decor parse failed (non-fatal): {}", parsed.error());
      }
    } else {
      App::Log::warn(
          LogCategory::Scenario, "World decor unavailable (non-fatal): {}", decor_file.error());
    }
  }

  // Load and parse into temporary ownership BEFORE mutating the target so a
  // failed load leaves every currently resident context intact.
  std::optional<LoadedScenario> loaded;
  std::unique_ptr<ScenarioRuntime> runtime;
  if (scenario_path.has_value()) {
    auto scenario{load_scenario(*scenario_path)};
    if (!scenario) {
      target->last_error = scenario.error();
      return std::expected<WorldSceneContext*, std::string>{
          std::unexpect, std::move(scenario).error()};
    }
    loaded.emplace(std::move(scenario).value());
    auto prepared{prepare_runtime(*scenario_path, *loaded)};
    if (!prepared) {
      target->last_error = prepared.error();
      return std::expected<WorldSceneContext*, std::string>{
          std::unexpect, std::move(prepared).error()};
    }
    runtime = std::move(prepared).value();
  } else {
    runtime = std::make_unique<ScenarioRuntime>();
    runtime->set_body_locator([this](const Character::BodyIdentity body_identity) {
      const auto located{find_character_body(body_identity)};
      return located.has_value() ? located->character : nullptr;
    });
    runtime->initialize_world_only(fmt::format("world-{}", scene_id), m_audio_system);
  }

  // The replacement is ready: tear down the recycled entry (if any) and
  // atomically install the new paired model/SCX/runtime state.
  const std::size_t script_count{loaded ? loaded->scx_data.scripts.size() : 0U};
  const std::size_t sound_count{loaded ? loaded->scx_data.sounds.size() : 0U};
  const std::size_t sprite_count{loaded ? loaded->scx_data.sprites.size() : 0U};
  const std::size_t model_count{loaded ? loaded->scx_data.models.size() : 0U};
  const std::string scenario_name{
      scenario_path ? scenario_basename(*scenario_path) : "<world-only>"};

  if (previous != WorldSceneResidencyState::Free) {
    teardown_world_context(*target);
  }
  install_world_context(*target,
      scene_id,
      std::move(decor_path),
      std::move(resolved_decor_path),
      std::move(decor_model),
      scenario_path,
      std::move(loaded),
      std::move(runtime),
      WorldSceneResidencyState::ResidentDetached);

  App::Log::debug(LogCategory::Scenario,
      "world context {} \"{}\" loaded — scripts={} sounds={} sprites={} models={}",
      scene_id,
      scenario_name,
      script_count,
      sound_count,
      sprite_count,
      model_count);

  return target;
}

std::expected<void, std::string> ScenarioManager::activate_world_context(
    const std::uint32_t scene_id) {
  WorldSceneContext* context{find_world_context(scene_id)};
  if (context == nullptr || context->residency == WorldSceneResidencyState::Free) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("Cannot activate world context {}: not found or free", scene_id)};
  }

  if (context->residency == WorldSceneResidencyState::ResidentAttached) {
    return {};
  }

  context->residency = WorldSceneResidencyState::ResidentAttached;
  if (!m_current_world_scene_id.has_value()) {
    m_current_world_scene_id = scene_id;
  }

  App::Log::info(LogCategory::Scenario,
      "world context {} \"{}\" active — generation={}",
      scene_id,
      scenario_basename(context->scenario_path),
      context->generation);
  return {};
}

std::expected<void, std::string> ScenarioManager::deactivate_world_context(
    const std::uint32_t scene_id) {
  WorldSceneContext* context{find_world_context(scene_id)};
  if (context == nullptr || context->residency == WorldSceneResidencyState::Free) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("Cannot deactivate world context {}: not found or free", scene_id)};
  }

  if (context->residency == WorldSceneResidencyState::ResidentDetached) {
    return {};
  }
  context->residency = WorldSceneResidencyState::ResidentDetached;

  App::Log::info(LogCategory::Scenario,
      "world context {} \"{}\" deactivated — generation={}",
      scene_id,
      scenario_basename(context->scenario_path),
      context->generation);
  return {};
}

std::expected<void, std::string> ScenarioManager::switch_active_world_context(
    const std::uint32_t source_scene_id, const std::uint32_t target_scene_id) {
  if (source_scene_id == target_scene_id) {
    return std::expected<void, std::string>{
        std::unexpect, "Cannot switch world residency to the same scene identity"};
  }

  const WorldSceneContext* source{find_world_context(source_scene_id)};
  const WorldSceneContext* target{find_world_context(target_scene_id)};
  if (source == nullptr || source->residency == WorldSceneResidencyState::Free ||
      m_current_world_scene_id != source_scene_id) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format(
            "Cannot switch current world: source context {} is not current", source_scene_id)};
  }
  if (target == nullptr || target->residency != WorldSceneResidencyState::ResidentAttached) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format(
            "Cannot switch current world: target context {} is not attached", target_scene_id)};
  }

  m_current_world_scene_id = target_scene_id;

  App::Log::info(LogCategory::Scenario,
      "current world switched — source={} '{}', target={} '{}'",
      source_scene_id,
      scenario_basename(source->scenario_path),
      target_scene_id,
      scenario_basename(target->scenario_path));
  return {};
}

std::expected<void, std::string> ScenarioManager::set_current_world_context(
    const std::uint32_t scene_id) {
  const WorldSceneContext* target{find_world_context(scene_id)};
  if (target == nullptr || target->residency != WorldSceneResidencyState::ResidentAttached) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("Cannot select current world context {}: not resident and attached", scene_id)};
  }
  m_current_world_scene_id = scene_id;
  return {};
}

std::expected<void, std::string> ScenarioManager::unload_world_context(
    const std::uint32_t scene_id) {
  WorldSceneContext* context{find_world_context(scene_id)};
  if (context == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("Cannot unload world context {}: not found", scene_id)};
  }

  if (context->residency == WorldSceneResidencyState::ResidentAttached) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format(
            "Cannot unload world context {}: still ResidentAttached (deactivate first)", scene_id)};
  }
  if (m_current_world_scene_id == scene_id) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("Cannot unload current world context {}", scene_id)};
  }

  if (context->residency == WorldSceneResidencyState::Free) {
    return {};  // Already free.
  }
  if (m_controlled_character.has_value() && m_controlled_character->world_scene_id == scene_id) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("Cannot unload world context {}: it owns current controlled character {}",
            scene_id,
            m_controlled_character->character_id)};
  }

  App::Log::debug(LogCategory::Scenario,
      "world context {} \"{}\" unloading — generation={}",
      scene_id,
      scenario_basename(context->scenario_path),
      context->generation);

  teardown_world_context(*context);
  context->residency = WorldSceneResidencyState::Free;
  ++context->generation;

  return {};
}

std::optional<ControlledCharacterRef> ScenarioManager::controlled_character() const {
  return m_controlled_character;
}

void ScenarioManager::set_controlled_character(const ControlledCharacterRef character) {
  m_controlled_character = character;
}

void ScenarioManager::clear_controlled_character() {
  m_controlled_character.reset();
}

std::optional<LocatedCharacterBody> ScenarioManager::find_character_body(
    const Character::BodyIdentity body_identity) {
  for (WorldSceneContext& context : m_world_contexts) {
    if (context.residency == WorldSceneResidencyState::Free || context.runtime == nullptr) {
      continue;
    }
    Character::Runtime& characters{context.runtime->character_runtime()};
    if (Character::RuntimeCharacter* const character{characters.find_body(body_identity)};
        character != nullptr) {
      return LocatedCharacterBody{.world_scene_id = context.scene_id,
          .runtime = context.runtime.get(),
          .character = character};
    }
  }
  return std::nullopt;
}

std::optional<LocatedConstCharacterBody> ScenarioManager::find_character_body(
    const Character::BodyIdentity body_identity) const {
  for (const WorldSceneContext& context : m_world_contexts) {
    if (context.residency == WorldSceneResidencyState::Free || context.runtime == nullptr) {
      continue;
    }
    const Character::Runtime& characters{context.runtime->character_runtime()};
    if (const Character::RuntimeCharacter* const character{characters.find_body(body_identity)};
        character != nullptr) {
      return LocatedConstCharacterBody{.world_scene_id = context.scene_id,
          .runtime = context.runtime.get(),
          .character = character};
    }
  }
  return std::nullopt;
}

std::expected<void, std::string> ScenarioManager::transfer_controlled_character(
    const std::uint32_t source_scene_id, const std::uint32_t target_scene_id) {
  if (!m_controlled_character.has_value() ||
      m_controlled_character->world_scene_id != source_scene_id) {
    return {};
  }
  if (source_scene_id == target_scene_id) {
    return {};
  }
  ScenarioRuntime* const source{world_runtime(source_scene_id)};
  ScenarioRuntime* const target{world_runtime(target_scene_id)};
  if (source == nullptr || target == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("Cannot transfer current controlled character {} from world {} to {}: "
                    "source or target runtime is unavailable",
            m_controlled_character->character_id,
            source_scene_id,
            target_scene_id)};
  }
  if (auto transferred{source->character_runtime().transfer_body_to(
          target->character_runtime(), m_controlled_character->body_identity)};
      !transferred) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("Cannot transfer current controlled character {} from world {} to {}: {}",
            m_controlled_character->character_id,
            source_scene_id,
            target_scene_id,
            transferred.error())};
  }
  m_controlled_character->world_scene_id = target_scene_id;
  return {};
}

WorldSceneContext* ScenarioManager::find_world_context(const std::uint32_t scene_id) {
  for (auto& ctx : m_world_contexts) {
    if (ctx.residency != WorldSceneResidencyState::Free && ctx.scene_id == scene_id) {
      return &ctx;
    }
  }
  return nullptr;
}

const WorldSceneContext* ScenarioManager::find_world_context(const std::uint32_t scene_id) const {
  for (const auto& ctx : m_world_contexts) {
    if (ctx.residency != WorldSceneResidencyState::Free && ctx.scene_id == scene_id) {
      return &ctx;
    }
  }
  return nullptr;
}

std::span<const WorldSceneContext, 2> ScenarioManager::world_contexts() const {
  return std::span<const WorldSceneContext, 2>{m_world_contexts};
}

const Omikron::ScxData* ScenarioManager::gameplay_mode_scx() const {
  if (m_gameplay_mode_slot.resolved_path.empty()) {
    return nullptr;  // Not yet loaded.
  }
  return &m_gameplay_mode_slot.scx_data;
}

std::span<const std::byte> ScenarioManager::gameplay_mode_scx_bytes() const {
  return std::span<const std::byte>{m_gameplay_mode_slot.file_buffer};
}

ScenarioRuntime* ScenarioManager::gameplay_runtime() const {
  return m_gameplay_mode_slot.runtime.get();
}

ScenarioRuntime* ScenarioManager::world_runtime(const std::uint32_t scene_id) const {
  const WorldSceneContext* context{find_world_context(scene_id)};
  return context == nullptr ? nullptr : context->runtime.get();
}

std::vector<ScenarioRuntime*> ScenarioManager::resident_world_runtimes() const {
  std::vector<ScenarioRuntime*> runtimes;
  for (const WorldSceneContext& context : m_world_contexts) {
    if (context.residency != WorldSceneResidencyState::Free && context.runtime != nullptr) {
      runtimes.push_back(context.runtime.get());
    }
  }
  return runtimes;
}

WorldSceneContext* ScenarioManager::active_world_context() {
  return current_world_context();
}

const WorldSceneContext* ScenarioManager::active_world_context() const {
  return current_world_context();
}

WorldSceneContext* ScenarioManager::current_world_context() {
  return m_current_world_scene_id.has_value() ? find_world_context(*m_current_world_scene_id)
                                              : nullptr;
}

const WorldSceneContext* ScenarioManager::current_world_context() const {
  return m_current_world_scene_id.has_value() ? find_world_context(*m_current_world_scene_id)
                                              : nullptr;
}

std::vector<WorldSceneContext*> ScenarioManager::attached_world_contexts() {
  std::vector<WorldSceneContext*> contexts;
  for (WorldSceneContext& context : m_world_contexts) {
    if (context.residency == WorldSceneResidencyState::ResidentAttached) {
      contexts.push_back(&context);
    }
  }
  return contexts;
}

std::vector<const WorldSceneContext*> ScenarioManager::attached_world_contexts() const {
  std::vector<const WorldSceneContext*> contexts;
  for (const WorldSceneContext& context : m_world_contexts) {
    if (context.residency == WorldSceneResidencyState::ResidentAttached) {
      contexts.push_back(&context);
    }
  }
  return contexts;
}

const Omikron::ScxData* ScenarioManager::world_context_scx(const std::uint32_t scene_id) const {
  const WorldSceneContext* context{find_world_context(scene_id)};
  if (context != nullptr && context->scx_data) {
    return &*context->scx_data;
  }
  return nullptr;
}

std::vector<LoadedScenarioView> ScenarioManager::scenario_inventory() const {
  std::vector<LoadedScenarioView> results;

  // Gameplay-mode slot.
  {
    LoadedScenarioView view;
    view.identity.role = ScenarioRole::GameplayMode;
    view.identity.slot = 0;
    view.identity.generation = m_gameplay_mode_slot.generation;
    view.scenario_path = m_gameplay_mode_slot.scenario_path;
    view.resolved_path = m_gameplay_mode_slot.resolved_path;
    view.file_size = m_gameplay_mode_slot.file_size_bytes;
    view.file_version = m_gameplay_mode_slot.scx_data.header.version;
    view.script_count = m_gameplay_mode_slot.scx_data.scripts.size();
    view.sound_count = m_gameplay_mode_slot.scx_data.sounds.size();
    view.sprite_count = m_gameplay_mode_slot.scx_data.sprites.size();
    view.model_count = m_gameplay_mode_slot.scx_data.models.size();
    view.shared_value_count = m_gameplay_mode_slot.scx_data.shared_values.size();
    view.loaded = !m_gameplay_mode_slot.resolved_path.empty();
    if (m_gameplay_mode_slot.runtime != nullptr &&
        m_gameplay_mode_slot.runtime->script_runtime() != nullptr) {
      view.active_script_instances =
          m_gameplay_mode_slot.runtime->script_runtime()->instances().size();
      view.render_instances = m_gameplay_mode_slot.runtime->sprite_pool().attached_count();
    }
    const Sfx::Diagnostics sfx{m_gameplay_mode_slot.runtime == nullptr
                                   ? Sfx::Diagnostics{}
                                   : m_gameplay_mode_slot.runtime->sfx_diagnostics()};
    view.sfx_loaded = sfx.loaded;
    view.sfx_definition_count = sfx.definition_count;
    view.sfx_node_count = sfx.node_count;
    view.sfx_track_count = sfx.track_count;
    view.active_sfx_nodes = sfx.active_node_count;
    view.queued_sfx_requests = sfx.queued_request_count;
    view.active_sfx_particles = sfx.active_particle_count;
    view.sfx_attached_sprites = sfx.attached_sprite_count;
    view.last_error = m_gameplay_mode_slot.last_error;
    results.push_back(std::move(view));
  }

  // World contexts.
  for (std::size_t i = 0; i < m_world_contexts.size(); ++i) {
    const WorldSceneContext& ctx{m_world_contexts.at(i)};
    LoadedScenarioView view;
    view.identity.role = ScenarioRole::WorldScene;
    view.identity.slot = static_cast<std::uint32_t>(i);
    view.identity.generation = ctx.generation;
    view.residency = ctx.residency;
    view.scene_id = ctx.scene_id;
    view.scenario_path = ctx.scenario_path;
    view.resolved_path = ctx.resolved_scenario_path;
    view.decor_path = ctx.decor_path.value_or(std::string{});
    view.resolved_decor_path = ctx.resolved_decor_path;
    view.file_size = ctx.file_size_bytes;
    view.loaded = (ctx.residency != WorldSceneResidencyState::Free);
    view.decor_attached = ctx.residency == WorldSceneResidencyState::ResidentAttached;
    view.current = m_current_world_scene_id == ctx.scene_id;
    view.scx_present = ctx.scx_data.has_value();
    view.structured_runtime_present =
        ctx.runtime != nullptr && ctx.runtime->script_runtime() != nullptr;
    view.controlled_body_owner = m_controlled_character.has_value() &&
                                 m_controlled_character->world_scene_id == ctx.scene_id;
    view.last_error = ctx.last_error;
    if (ctx.scx_data) {
      view.file_version = ctx.scx_data->header.version;
      view.script_count = ctx.scx_data->scripts.size();
      view.sound_count = ctx.scx_data->sounds.size();
      view.sprite_count = ctx.scx_data->sprites.size();
      view.model_count = ctx.scx_data->models.size();
      view.shared_value_count = ctx.scx_data->shared_values.size();
    }
    if (ctx.runtime != nullptr && ctx.runtime->script_runtime() != nullptr) {
      view.active_script_instances = ctx.runtime->script_runtime()->instances().size();
      view.render_instances = ctx.runtime->sprite_pool().attached_count();
    }
    const Sfx::Diagnostics sfx{
        ctx.runtime == nullptr ? Sfx::Diagnostics{} : ctx.runtime->sfx_diagnostics()};
    view.sfx_loaded = sfx.loaded;
    view.sfx_definition_count = sfx.definition_count;
    view.sfx_node_count = sfx.node_count;
    view.sfx_track_count = sfx.track_count;
    view.active_sfx_nodes = sfx.active_node_count;
    view.queued_sfx_requests = sfx.queued_request_count;
    view.active_sfx_particles = sfx.active_particle_count;
    view.sfx_attached_sprites = sfx.attached_sprite_count;
    results.push_back(std::move(view));
  }

  return results;
}

std::size_t ScenarioManager::loaded_scenario_count() const {
  std::size_t count{0};

  if (!m_gameplay_mode_slot.resolved_path.empty()) {
    ++count;
  }

  for (const WorldSceneContext& ctx : m_world_contexts) {
    if (ctx.residency != WorldSceneResidencyState::Free) {
      ++count;
    }
  }

  return count;
}

std::size_t ScenarioManager::active_script_instances_total() const {
  std::size_t total{0};
  const auto count_runtime = [&total](const ScenarioRuntime* runtime) {
    if (runtime != nullptr && runtime->script_runtime() != nullptr) {
      total += runtime->script_runtime()->instances().size();
    }
  };

  count_runtime(m_gameplay_mode_slot.runtime.get());
  for (const WorldSceneContext& context : m_world_contexts) {
    count_runtime(context.runtime.get());
  }
  return total;
}

void ScenarioManager::set_audio_system(Audio::AudioSystem* audio_system) {
  m_audio_system = audio_system;
  if (m_gameplay_mode_slot.runtime != nullptr) {
    m_gameplay_mode_slot.runtime->set_audio_system(audio_system);
  }
  for (WorldSceneContext& context : m_world_contexts) {
    if (context.runtime != nullptr) {
      context.runtime->set_audio_system(audio_system);
    }
  }
}

void ScenarioManager::service_dialog_camera() {
  const std::uint64_t generation{m_dialog_runtime.generation()};

  // A camera pair is a presentation-generation operation, not something that
  // should be continuously resubmitted while a subtitle remains visible.
  if (m_dialog_camera_generation.has_value() && m_dialog_camera_generation.value() == generation) {
    return;
  }

  const std::optional<Dialog::DialogPresentation> presentation{m_dialog_runtime.presentation()};

  if (!presentation.has_value()) {
    // Remember inactive/completed generations too. A later dialog/node/state
    // change increments DialogRuntime's generation and will be seen normally.
    m_dialog_camera_generation = generation;
    return;
  }

  const Dialog::DialogCameraPair* pair{nullptr};

  switch (presentation->state) {
    case Dialog::DialogState::k_presenting_line:
      // Runtime's main/NPC line path requests node +0x3C/+0x3E.
      pair = &presentation->line_cameras;
      break;

    case Dialog::DialogState::k_presenting_automatic_player_line:
    case Dialog::DialogState::k_waiting_for_choice:
      // Runtime's response-side presentation path requests node +0x38/+0x3A.
      pair = &presentation->response_cameras;
      break;

    default:
      m_dialog_camera_generation = generation;
      return;
  }

  // Runtime's dialog camera-pair routine returns immediately when camera A is
  // -1. Camera B is not independently applied in that case.
  if (pair->authored_ids.at(0) < 0) {
    m_dialog_camera_generation = generation;
    return;
  }

  const std::optional<Omikron::IamCameraRecord> immediate_camera{pair->cameras.at(0)};
  if (!immediate_camera.has_value()) {
    App::Log::warn(LogCategory::Scenario,
        "Dialog camera {} is authored but unresolved",
        pair->authored_ids.at(0));
    m_dialog_camera_generation = generation;
    return;
  }

  const WorldSceneContext* context{active_world_context()};
  if (context == nullptr) {
    // Do not consume the generation: if presentation temporarily has no
    // active world owner, retry once a world exists.
    return;
  }

  const std::optional<ControlledCharacterRef> controlled{controlled_character()};
  const std::int16_t participant_a_character_id{
      static_cast<std::int16_t>(controlled.has_value() ? controlled->character_id : -1)};
  const WorldCameraAttachmentParticipants participants{
      .participant_a_character_id = participant_a_character_id,
      .participant_b_character_id = presentation->character_id};

  const auto enqueue = [this, context, participants](const Omikron::IamCameraRecord& camera,
                           const std::int16_t duration_units) {
    m_world_presentation.enqueue_camera(WorldCameraCommand{.scene_id = context->scene_id,
        .scene_generation = context->generation,
        .camera_id = static_cast<std::uint16_t>(camera.camera_id),
        .attachment_participants = participants,

        .serialized_eye = camera.serialized_eye,
        .serialized_target = camera.serialized_target,
        .runtime_eye = Runtime::iam_camera_vector_to_runtime(camera.serialized_eye),
        .runtime_target = Runtime::iam_camera_vector_to_runtime(camera.serialized_target),

        .duration_units = duration_units,
        .flags = 0,
        .wait_for_completion = false,

        .camera_type = camera.camera_type,
        .roll_units = camera.roll_units,
        .horizontal_fov_units = camera.horizontal_fov_units,
        .roll_degrees = Runtime::area_angle_to_degrees(camera.roll_units),
        .horizontal_fov_degrees = Runtime::area_angle_to_degrees(camera.horizontal_fov_units),

        .target_attachment_selector = camera.target_attachment_selector,
        .eye_attachment_selector = camera.eye_attachment_selector,
        .tail_fields = camera.tail_fields});
  };

  // Native dialog pair:
  //
  //   camera A -> immediate placement
  //
  // Runtime uses -1.0 in its native camera command for this first operation.
  // OpenNomad's presentation abstraction represents the same snap with zero
  // duration.
  enqueue(immediate_camera.value(), 0);

  // Then travel to camera B over Runtime's fixed 160-unit interval.
  if (pair->authored_ids.at(1) >= 0) {
    const std::optional<Omikron::IamCameraRecord> transition_camera{pair->cameras.at(1)};
    if (!transition_camera.has_value()) {
      App::Log::warn(LogCategory::Scenario,
          "Dialog camera {} is authored but unresolved",
          pair->authored_ids.at(1));
    } else {
      enqueue(transition_camera.value(), K_DIALOG_CAMERA_TRANSITION_UNITS);
    }
  }

  App::Log::debug(LogCategory::Scenario,
      "Dialog camera pair — generation={} state={} {} -> {} participants=({}, {})",
      generation,
      static_cast<int>(presentation->state),
      pair->authored_ids.at(0),
      pair->authored_ids.at(1),
      participants.participant_a_character_id,
      participants.participant_b_character_id);

  m_dialog_camera_generation = generation;
}

void ScenarioManager::service_dialog_performance(const float real_delta_seconds) {
  // Runtime dialogue presentation owns authored camera changes independently
  // of the synchronized MORPH/3DM media stream. Submit them before servicing
  // the visual performance so both become visible on the same presentation
  // frame.
  service_dialog_camera();

  const WorldSceneContext* context{active_world_context()};
  ScenarioRuntime* runtime{context == nullptr ? nullptr : context->runtime.get()};
  Character::Runtime* characters{runtime == nullptr ? nullptr : &runtime->character_runtime()};
  const std::uint64_t identity{
      context == nullptr
          ? 0U
          : (static_cast<std::uint64_t>(context->generation) << 32U) | context->scene_id};
  m_dialog_performance.tick(
      real_delta_seconds, m_dialog_runtime, characters, identity, m_audio_system);
}

std::string ScenarioManager::normalize_asset_path(std::string path) {
  for (char& character : path) {
    if (character == '\\') {
      character = '/';
    }
  }
  return path;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static) — scenario-role diagnostics
std::expected<ScenarioManager::LoadedScenario, std::string> ScenarioManager::load_scenario(
    const std::string& scenario_path) {
  APP_PROFILE_FUNCTION();

  const std::string normalized{normalize_asset_path(scenario_path)};
  auto loaded_file{load_game_file(normalized)};
  if (!loaded_file) {
    return std::expected<LoadedScenario, std::string>{std::unexpect,
        fmt::format("Failed to open scenario file: requested='{}': {}",
            scenario_path,
            loaded_file.error())};
  }

  const std::filesystem::path resolved{loaded_file->resolved};
  auto scx{Omikron::SCX::load(std::span<const std::byte>{loaded_file->bytes})};
  if (!scx) {
    return std::expected<LoadedScenario, std::string>{std::unexpect,
        fmt::format("Failed to parse scenario: requested='{}' resolved='{}' error='{}'",
            scenario_path,
            resolved.string(),
            scx.error())};
  }

  App::Log::trace(LogCategory::Scenario,
      "scenario parsed — path='{}' scripts={} sounds={} sprites={} models={}",
      resolved.string(),
      scx->scripts.size(),
      scx->sounds.size(),
      scx->sprites.size(),
      scx->models.size());

  std::filesystem::path companion_relative{normalized};
  companion_relative.replace_extension(".SFX");
  const std::filesystem::path companion_root{Resources::game_data_path(companion_relative)};
  const std::filesystem::path companion_resolved{
      Resources::resolve_case_insensitive(companion_root)};
  std::error_code exists_error;
  const bool companion_exists{std::filesystem::exists(companion_resolved, exists_error)};
  if (exists_error) {
    return std::expected<LoadedScenario, std::string>{std::unexpect,
        fmt::format("Failed to inspect optional SFX companion: requested='{}' resolved='{}': {}",
            companion_relative.string(),
            companion_resolved.string(),
            exists_error.message())};
  }

  std::string resolved_sfx_path;
  std::vector<std::byte> sfx_file_buffer;
  std::optional<Omikron::SfxData> sfx_data;
  if (companion_exists) {
    auto sfx_file{load_game_file(companion_relative)};
    if (!sfx_file) {
      return std::expected<LoadedScenario, std::string>{std::unexpect,
          fmt::format("Failed to open SFX companion: requested='{}': {}",
              companion_relative.string(),
              sfx_file.error())};
    }
    auto parsed_sfx{Omikron::SFX::load(std::span<const std::byte>{sfx_file->bytes})};
    if (!parsed_sfx) {
      return std::expected<LoadedScenario, std::string>{std::unexpect,
          fmt::format("Failed to parse SFX companion: requested='{}' resolved='{}' error='{}'",
              companion_relative.string(),
              sfx_file->resolved.string(),
              parsed_sfx.error())};
    }
    resolved_sfx_path = sfx_file->resolved.string();
    sfx_file_buffer = std::move(sfx_file->bytes);
    sfx_data = std::move(parsed_sfx).value();
    App::Log::debug(LogCategory::Scenario,
        "loaded {}: definitions={} nodes={} tracks={}",
        companion_relative.filename().string(),
        sfx_data->definitions.size(),
        sfx_data->nodes.size(),
        sfx_data->tracks.size());
  }

  return LoadedScenario{.resolved_path = resolved.string(),
      .file_buffer = std::move(loaded_file->bytes),
      .scx_data = std::move(scx).value(),
      .resolved_sfx_path = std::move(resolved_sfx_path),
      .sfx_file_buffer = std::move(sfx_file_buffer),
      .sfx_data = std::move(sfx_data)};
}

std::expected<std::unique_ptr<ScenarioRuntime>, std::string> ScenarioManager::prepare_runtime(
    const std::string& scenario_name, const LoadedScenario& loaded) {
  auto runtime{std::make_unique<ScenarioRuntime>()};
  runtime->set_body_locator([this](const Character::BodyIdentity body_identity) {
    const auto located{find_character_body(body_identity)};
    return located.has_value() ? located->character : nullptr;
  });
  if (auto result{runtime->initialize(loaded.scx_data,
          std::span<const std::byte>{loaded.file_buffer},
          scenario_name,
          m_audio_system,
          /*activate_startup_scripts=*/false,
          loaded.sfx_data ? &*loaded.sfx_data : nullptr)};
      !result) {
    return std::expected<std::unique_ptr<ScenarioRuntime>, std::string>{
        std::unexpect, fmt::format("Scenario runtime init failed: {}", result.error())};
  }
  return runtime;
}

void ScenarioManager::install_gameplay_mode(
    const GameplayMode mode, LoadedScenario loaded, std::unique_ptr<ScenarioRuntime> runtime) {
  m_gameplay_mode_slot.current_mode = mode;
  m_gameplay_mode_slot.scenario_path = std::string{gameplay_mode_scenario_path(mode)};
  m_gameplay_mode_slot.resolved_path = std::move(loaded.resolved_path);
  m_gameplay_mode_slot.file_buffer = std::move(loaded.file_buffer);
  m_gameplay_mode_slot.scx_data = std::move(loaded.scx_data);
  m_gameplay_mode_slot.resolved_sfx_path = std::move(loaded.resolved_sfx_path);
  m_gameplay_mode_slot.sfx_file_buffer = std::move(loaded.sfx_file_buffer);
  m_gameplay_mode_slot.sfx_data = std::move(loaded.sfx_data);
  m_gameplay_mode_slot.runtime = std::move(runtime);
  m_gameplay_mode_slot.file_size_bytes = m_gameplay_mode_slot.file_buffer.size();
  m_gameplay_mode_slot.last_error.clear();
  ++m_gameplay_mode_slot.generation;
}

void ScenarioManager::teardown_gameplay_mode_slot() {
  // Destroy the slot-owned runtime before releasing its SCX backing bytes.
  m_gameplay_mode_slot.runtime.reset();
  m_gameplay_mode_slot.scx_data = Omikron::ScxData{};
  m_gameplay_mode_slot.file_buffer.clear();
  m_gameplay_mode_slot.sfx_data.reset();
  m_gameplay_mode_slot.sfx_file_buffer.clear();
  m_gameplay_mode_slot.resolved_sfx_path.clear();
  m_gameplay_mode_slot.scenario_path.clear();
  m_gameplay_mode_slot.resolved_path.clear();
  m_gameplay_mode_slot.file_size_bytes = 0;
  m_gameplay_mode_slot.last_error.clear();
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static) — slot-teardown responsibilities
void ScenarioManager::install_world_context(WorldSceneContext& context,
    const std::uint32_t scene_id,
    std::optional<std::string> decor_path,
    std::string resolved_decor_path,
    std::optional<Omikron::Model3DOData> decor_model,
    const std::optional<std::string>& scenario_path,
    std::optional<LoadedScenario> loaded,
    std::unique_ptr<ScenarioRuntime> runtime,
    const WorldSceneResidencyState residency) {
  context.scene_id = scene_id;
  context.decor_path = std::move(decor_path);
  context.resolved_decor_path = std::move(resolved_decor_path);
  context.decor_model = std::move(decor_model);
  context.scenario_path = scenario_path.value_or(std::string{});
  if (loaded.has_value()) {
    context.resolved_scenario_path = std::move(loaded->resolved_path);
    context.scx_file_buffer = std::move(loaded->file_buffer);
    context.scx_data = std::move(loaded->scx_data);
    context.resolved_sfx_path = std::move(loaded->resolved_sfx_path);
    context.sfx_file_buffer = std::move(loaded->sfx_file_buffer);
    context.sfx_data = std::move(loaded->sfx_data);
  } else {
    context.resolved_scenario_path.clear();
    context.scx_file_buffer.clear();
    context.scx_data.reset();
    context.resolved_sfx_path.clear();
    context.sfx_file_buffer.clear();
    context.sfx_data.reset();
  }
  context.runtime = std::move(runtime);
  if (context.runtime != nullptr) {
    context.runtime->bind_decor_model(
        context.decor_model.has_value() ? &context.decor_model.value() : nullptr);
  }
  context.file_size_bytes = context.scx_file_buffer.size();
  context.residency = residency;
  context.last_error.clear();
  ++context.generation;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static) — slot-teardown responsibilities
void ScenarioManager::teardown_world_context(WorldSceneContext& context) {
  m_dialog_performance.stop_for_world_change();
  // Destroy the context-owned runtime and decor before releasing its SCX
  // backing bytes, so no runtime object outlives its slot's data.
  context.runtime.reset();
  context.decor_model.reset();
  context.decor_path.reset();
  context.resolved_decor_path.clear();
  context.scx_data.reset();
  context.scx_file_buffer.clear();
  context.sfx_data.reset();
  context.sfx_file_buffer.clear();
  context.resolved_sfx_path.clear();
  context.scene_id = 0;
  context.scenario_path.clear();
  context.resolved_scenario_path.clear();
  context.file_size_bytes = 0;
  context.last_error.clear();
}

WorldSceneContext* ScenarioManager::allocate_world_context_slot() {
  // Prefer the first Free entry.
  for (WorldSceneContext& ctx : m_world_contexts) {
    if (ctx.residency == WorldSceneResidencyState::Free) {
      return &ctx;
    }
  }

  // Otherwise, prefer the first ResidentDetached entry.
  for (WorldSceneContext& ctx : m_world_contexts) {
    if (ctx.residency == WorldSceneResidencyState::ResidentDetached &&
        m_current_world_scene_id != ctx.scene_id &&
        (!m_controlled_character.has_value() ||
            m_controlled_character->world_scene_id != ctx.scene_id)) {
      return &ctx;
    }
  }

  // Neither resident slot is safely recyclable.
  return nullptr;
}

}  // namespace App

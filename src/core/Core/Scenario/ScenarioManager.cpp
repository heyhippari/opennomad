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
#include <utility>
#include <vector>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"

namespace App {

namespace {

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
  return {};
}

GameplayMode ScenarioManager::current_gameplay_mode() const {
  return m_gameplay_mode_slot.current_mode;
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
    const std::string& scenario_path) {
  APP_PROFILE_FUNCTION();

  WorldSceneContext* target{allocate_world_context_slot()};
  if (target == nullptr) {
    return std::expected<WorldSceneContext*, std::string>{
        std::unexpect,
        fmt::format("Cannot allocate world context slot for scene {}: no free or recyclable "
                    "entry (both are LoadedActive)",
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
      scenario_path);

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
        App::Log::warn(LogCategory::Scenario, "World decor parse failed (non-fatal): {}", parsed.error());
      }
    } else {
      App::Log::warn(LogCategory::Scenario, "World decor unavailable (non-fatal): {}", decor_file.error());
    }
  }

  // Load and parse into temporary ownership BEFORE mutating the target so a
  // failed load leaves every currently resident context intact.
  auto loaded{load_scenario(scenario_path)};
  if (!loaded) {
    target->last_error = loaded.error();
    App::Log::error(LogCategory::Scenario,
        "load failed: role=world_scene cache_index={} scene_id={}: {}",
        cache_index,
        scene_id,
        loaded.error());
    return std::expected<WorldSceneContext*, std::string>{
        std::unexpect, std::move(loaded).error()};
  }

  // Build the replacement runtime from the freshly parsed package before
  // touching the target so a runtime-construction failure leaves every
  // resident context intact.
  auto runtime{prepare_runtime(scenario_path, loaded.value())};
  if (!runtime) {
    target->last_error = runtime.error();
    App::Log::error(LogCategory::Scenario,
        "load failed: role=world_scene cache_index={} scene_id={}: {}",
        cache_index,
        scene_id,
        runtime.error());
    return std::expected<WorldSceneContext*, std::string>{
        std::unexpect, std::move(runtime).error()};
  }

  // The replacement is ready: tear down the recycled entry (if any) and
  // atomically install the new paired model/SCX/runtime state.
  LoadedScenario package{std::move(loaded).value()};
  const std::size_t script_count{package.scx_data.scripts.size()};
  const std::size_t sound_count{package.scx_data.sounds.size()};
  const std::size_t sprite_count{package.scx_data.sprites.size()};
  const std::size_t model_count{package.scx_data.models.size()};

  if (previous != WorldSceneResidencyState::Free) {
    teardown_world_context(*target);
  }
  install_world_context(*target,
      scene_id,
      std::move(decor_path),
      std::move(resolved_decor_path),
      std::move(decor_model),
      scenario_path,
      std::move(package),
      std::move(runtime).value(),
      WorldSceneResidencyState::LoadedInactive);

  App::Log::debug(LogCategory::Scenario,
      "world context {} \"{}\" loaded — scripts={} sounds={} sprites={} models={}",
      scene_id,
      scenario_basename(scenario_path),
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
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("Cannot activate world context {}: not found or free", scene_id)};
  }

  if (context->residency == WorldSceneResidencyState::LoadedActive) {
    return {};  // Already active.
  }

  context->residency = WorldSceneResidencyState::LoadedActive;

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
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("Cannot deactivate world context {}: not found or free", scene_id)};
  }

  if (context->residency == WorldSceneResidencyState::LoadedInactive) {
    return {};  // Already inactive.
  }

  context->residency = WorldSceneResidencyState::LoadedInactive;

  App::Log::info(LogCategory::Scenario,
      "world context {} \"{}\" deactivated — generation={}",
      scene_id,
      scenario_basename(context->scenario_path),
      context->generation);
  return {};
}

std::expected<void, std::string> ScenarioManager::unload_world_context(
    const std::uint32_t scene_id) {
  WorldSceneContext* context{find_world_context(scene_id)};
  if (context == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("Cannot unload world context {}: not found", scene_id)};
  }

  if (context->residency == WorldSceneResidencyState::LoadedActive) {
    return std::expected<void, std::string>{
        std::unexpect,
        fmt::format("Cannot unload world context {}: still LoadedActive (deactivate first)",
            scene_id)};
  }

  if (context->residency == WorldSceneResidencyState::Free) {
    return {};  // Already free.
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

std::vector<ScenarioRuntime*> ScenarioManager::active_world_runtimes() const {
  std::vector<ScenarioRuntime*> runtimes;
  for (const WorldSceneContext& context : m_world_contexts) {
    if (context.residency == WorldSceneResidencyState::LoadedActive && context.runtime != nullptr) {
      runtimes.push_back(context.runtime.get());
    }
  }
  return runtimes;
}

WorldSceneContext* ScenarioManager::active_world_context() {
  for (WorldSceneContext& context : m_world_contexts) {
    if (context.residency == WorldSceneResidencyState::LoadedActive) {
      return &context;
    }
  }
  return nullptr;
}

const WorldSceneContext* ScenarioManager::active_world_context() const {
  for (const WorldSceneContext& context : m_world_contexts) {
    if (context.residency == WorldSceneResidencyState::LoadedActive) {
      return &context;
    }
  }
  return nullptr;
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

  return LoadedScenario{.resolved_path = resolved.string(),
      .file_buffer = std::move(loaded_file->bytes),
      .scx_data = std::move(scx).value()};
}

std::expected<std::unique_ptr<ScenarioRuntime>, std::string> ScenarioManager::prepare_runtime(
    const std::string& scenario_name, const LoadedScenario& loaded) {
  auto runtime{std::make_unique<ScenarioRuntime>()};
  if (auto result{runtime->initialize(loaded.scx_data,
          std::span<const std::byte>{loaded.file_buffer},
          scenario_name,
          m_audio_system,
          /*activate_startup_scripts=*/false)};
      !result) {
    return std::expected<std::unique_ptr<ScenarioRuntime>, std::string>{
        std::unexpect, fmt::format("Scenario runtime init failed: {}", result.error())};
  }
  return runtime;
}

void ScenarioManager::install_gameplay_mode(const GameplayMode mode,
    LoadedScenario loaded,
    std::unique_ptr<ScenarioRuntime> runtime) {
  m_gameplay_mode_slot.current_mode = mode;
  m_gameplay_mode_slot.scenario_path = std::string{gameplay_mode_scenario_path(mode)};
  m_gameplay_mode_slot.resolved_path = std::move(loaded.resolved_path);
  m_gameplay_mode_slot.file_buffer = std::move(loaded.file_buffer);
  m_gameplay_mode_slot.scx_data = std::move(loaded.scx_data);
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
    const std::string& scenario_path,
    LoadedScenario loaded,
    std::unique_ptr<ScenarioRuntime> runtime,
    const WorldSceneResidencyState residency) {
  context.scene_id = scene_id;
  context.decor_path = std::move(decor_path);
  context.resolved_decor_path = std::move(resolved_decor_path);
  context.decor_model = std::move(decor_model);
  context.scenario_path = scenario_path;
  context.resolved_scenario_path = std::move(loaded.resolved_path);
  context.scx_file_buffer = std::move(loaded.file_buffer);
  context.scx_data = std::move(loaded.scx_data);
  context.runtime = std::move(runtime);
  context.file_size_bytes = context.scx_file_buffer.size();
  context.residency = residency;
  context.last_error.clear();
  ++context.generation;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static) — slot-teardown responsibilities
void ScenarioManager::teardown_world_context(WorldSceneContext& context) {
  // Destroy the context-owned runtime and decor before releasing its SCX
  // backing bytes, so no runtime object outlives its slot's data.
  context.runtime.reset();
  context.decor_model.reset();
  context.decor_path.reset();
  context.resolved_decor_path.clear();
  context.scx_data.reset();
  context.scx_file_buffer.clear();
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

  // Otherwise, prefer the first LoadedInactive entry.
  for (WorldSceneContext& ctx : m_world_contexts) {
    if (ctx.residency == WorldSceneResidencyState::LoadedInactive) {
      return &ctx;
    }
  }

  // Both are LoadedActive; allocation fails.
  return nullptr;
}

}  // namespace App

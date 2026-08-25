#include "Core/Object/ObjectPlacementRuntime.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/GameDataLoader.hpp"
#include "Core/GameState.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamObjectPlacement.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/Texture3DT.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::ObjectPlacement {

namespace {

constexpr std::string_view K_OBJECT_MODEL_DIRECTORY{"MESHES/OBJETS"};
constexpr float K_DEGREES_TO_RADIANS{std::numbers::pi_v<float> / 180.0F};

std::string object_resource_path(
    const std::string_view resource_name, const std::string_view extension) {
  std::filesystem::path path{K_OBJECT_MODEL_DIRECTORY};
  path /= resource_name;
  path.replace_extension(extension);
  return path.generic_string();
}

App::Runtime::Transform placement_transform(const Omikron::IamObjectPlacementRecord& placement) {
  // Runtime 0x00409FC0 widens the three placement dwords directly to float
  // before 0x0041CF50 installs them. Unlike character/address coordinates,
  // these object-placement XYZ values do not pass through the AREA cm->inch
  // conversion. The three signed words are likewise literal degrees;
  // 0x0041CF50 multiplies them directly by pi/180 before Runtime's Euler builder.
  const App::Runtime::Vec3 translation{.x = static_cast<float>(placement.serialized_position.at(0)),
      .y = static_cast<float>(placement.serialized_position.at(1)),
      .z = static_cast<float>(placement.serialized_position.at(2))};
  const float rotation_x{
      static_cast<float>(placement.orientation_units.at(0)) * K_DEGREES_TO_RADIANS};
  const float rotation_y{
      static_cast<float>(placement.orientation_units.at(1)) * K_DEGREES_TO_RADIANS};
  const float rotation_z{
      static_cast<float>(placement.orientation_units.at(2)) * K_DEGREES_TO_RADIANS};
  return App::Runtime::Transform{
      .matrix = App::Runtime::euler_rotation(rotation_x, rotation_y, rotation_z),
      .translation = translation,
      .scale = App::Runtime::Vec3{.x = 1.0F, .y = 1.0F, .z = 1.0F}};
}

std::expected<void, std::string> validate_pair(const Omikron::IamObjectPlacementRecord& placement,
    const Omikron::IamObjectDefinitionRecord& definition,
    const std::size_t index,
    const std::string_view source) {
  if (placement.object_id != definition.object_id) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("{} object pair {} disagrees: table1 id={} table3 id={}",
            source,
            index,
            placement.object_id,
            definition.object_id)};
  }
  if (placement.persistent_state_index < 0) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("{} object {} has negative packed-state index {}",
            source,
            placement.object_id,
            placement.persistent_state_index)};
  }
  if (definition.model_resource.empty()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format(
            "{} object {} has an empty MESHES/OBJETS resource name", source, placement.object_id)};
  }
  return {};
}

}  // namespace

Runtime::Runtime() : Runtime{load_model_resource} {}

Runtime::Runtime(ModelLoader loader) : m_model_loader{std::move(loader)} {}

void Runtime::set_model_loader(ModelLoader loader) {
  m_model_loader = std::move(loader);
  m_model_resources.clear();
}

std::expected<std::shared_ptr<const ModelResource>, std::string> Runtime::load_model_resource(
    const std::string_view resource_name) {
  if (resource_name.empty()) {
    return std::expected<std::shared_ptr<const ModelResource>, std::string>{
        std::unexpect, "IAM object placement has an empty model resource"};
  }

  const std::string model_path{object_resource_path(resource_name, ".3DO")};
  auto model_file{load_game_file(model_path)};
  if (!model_file) {
    return std::expected<std::shared_ptr<const ModelResource>, std::string>{
        std::unexpect, fmt::format("object model '{}': {}", model_path, model_file.error())};
  }
  auto model{Omikron::Model3DO::load(std::span<const std::byte>{model_file->bytes})};
  if (!model) {
    return std::expected<std::shared_ptr<const ModelResource>, std::string>{std::unexpect,
        fmt::format("object model '{}': {}", model_file->resolved.string(), model.error())};
  }
  auto groups{Omikron::Model3DO::build_static_geometry(model.value())};
  if (!groups) {
    return std::expected<std::shared_ptr<const ModelResource>, std::string>{std::unexpect,
        fmt::format(
            "object model '{}' geometry: {}", model_file->resolved.string(), groups.error())};
  }

  std::vector<Omikron::Texture3DTImage> images;
  std::string resolved_texture_path;
  if (!model->materials.empty()) {
    const std::string texture_path{object_resource_path(resource_name, ".3DT")};
    auto texture_file{load_game_file(texture_path)};
    if (!texture_file) {
      return std::expected<std::shared_ptr<const ModelResource>, std::string>{std::unexpect,
          fmt::format("object texture '{}': {}", texture_path, texture_file.error())};
    }
    auto textures{Omikron::Texture3DT::load(
        std::span<const std::byte>{texture_file->bytes}, model->materials)};
    if (!textures) {
      return std::expected<std::shared_ptr<const ModelResource>, std::string>{std::unexpect,
          fmt::format(
              "object texture '{}': {}", texture_file->resolved.string(), textures.error())};
    }
    images = std::move(textures).value();
    resolved_texture_path = texture_file->resolved.string();
  }

  auto resource{std::make_shared<ModelResource>(ModelResource{.name = std::string{resource_name},
      .resolved_model_path = model_file->resolved.string(),
      .resolved_texture_path = std::move(resolved_texture_path),
      .model = std::move(model).value(),
      .groups = std::move(groups).value(),
      .images = std::move(images)})};
  return std::shared_ptr<const ModelResource>{std::move(resource)};
}

std::expected<void, std::string> Runtime::materialize_area_objects(
    const std::int32_t area_id, const Omikron::IamAreaRecord& area, const GameState& game_state) {
  const auto placements{area.object_placements()};
  const auto definitions{area.object_definitions()};
  if (placements.size() != definitions.size()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("AREA {} object tables disagree: {} placements, {} definitions",
            area_id,
            placements.size(),
            definitions.size())};
  }

  for (std::size_t index{0}; index < placements.size(); ++index) {
    const auto& placement{placements.at(index)};
    const auto& definition{definitions.at(index)};
    if (auto valid{validate_pair(placement, definition, index, "AREA")}; !valid) {
      return valid;
    }
    auto state{game_state.packed_state(static_cast<std::size_t>(placement.persistent_state_index))};
    if (!state) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("AREA {} object {} persistent state: {}",
              area_id,
              placement.object_id,
              state.error())};
    }
    if ((state.value() & 0x01U) == 0U) {
      continue;
    }
    if (auto created{materialize(area_id, std::nullopt, placement, definition, state.value())};
        !created) {
      return created;
    }
  }
  return {};
}

std::expected<void, std::string> Runtime::materialize_scene_objects(const std::int32_t area_id,
    const std::int32_t scene_id,
    const Omikron::IamSceneRecord& scene,
    const GameState& game_state) {
  const auto placements{scene.object_placements()};
  const auto definitions{scene.object_definitions()};
  if (placements.size() != definitions.size()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("SCENE {} object tables disagree: {} placements, {} definitions",
            scene_id,
            placements.size(),
            definitions.size())};
  }

  for (std::size_t index{0}; index < placements.size(); ++index) {
    const auto& placement{placements.at(index)};
    const auto& definition{definitions.at(index)};
    if (auto valid{validate_pair(placement, definition, index, "SCENE")}; !valid) {
      return valid;
    }
    auto state{game_state.packed_state(static_cast<std::size_t>(placement.persistent_state_index))};
    if (!state) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("SCENE {} object {} persistent state: {}",
              scene_id,
              placement.object_id,
              state.error())};
    }
    if ((state.value() & 0x01U) == 0U) {
      continue;
    }
    if (auto created{materialize(area_id, scene_id, placement, definition, state.value())};
        !created) {
      dematerialize_scene_objects(area_id, scene_id);
      return created;
    }
  }
  return {};
}

std::expected<void, std::string> Runtime::materialize(const std::int32_t area_id,
    const std::optional<std::int32_t> scene_id,
    const Omikron::IamObjectPlacementRecord& placement,
    const Omikron::IamObjectDefinitionRecord& definition,
    const std::uint8_t persistent_state) {
  std::shared_ptr<const ModelResource> resource;
  const auto cached{m_model_resources.find(definition.model_resource)};
  if (cached != m_model_resources.end()) {
    resource = cached->second;
  } else {
    if (!m_model_loader) {
      return std::expected<void, std::string>{
          std::unexpect, "object-placement model loader is not wired"};
    }
    auto loaded{m_model_loader(definition.model_resource)};
    if (!loaded) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("object {} model '{}': {}",
              placement.object_id,
              definition.model_resource,
              loaded.error())};
    }
    resource = loaded.value();
    m_model_resources.emplace(definition.model_resource, resource);
  }

  m_placements.push_back(RuntimePlacement{.instance_id = m_next_instance_id++,
      .area_id = area_id,
      .scene_id = scene_id,
      .runtime_object_slot_seed = placement.runtime_object_slot_seed,
      .object_id = placement.object_id,
      .persistent_state_index = placement.persistent_state_index,
      .type_or_flags = definition.type_or_flags,
      .enabled = (persistent_state & 0x02U) != 0U,
      .transform = placement_transform(placement),
      .model_resource_name = definition.model_resource,
      .model_resource = std::move(resource)});
  return {};
}

void Runtime::dematerialize_scene_objects(const std::int32_t area_id, const std::int32_t scene_id) {
  std::erase_if(m_placements, [area_id, scene_id](const RuntimePlacement& placement) {
    return placement.area_id == area_id && placement.scene_id == scene_id;
  });
}

RuntimePlacement* Runtime::find(const std::int32_t area_id,
    const std::optional<std::int32_t> scene_id,
    const std::int16_t object_id) {
  const auto found{std::ranges::find_if(
      m_placements, [area_id, scene_id, object_id](const RuntimePlacement& placement) {
        return placement.area_id == area_id && placement.scene_id == scene_id &&
               placement.object_id == object_id;
      })};
  return found == m_placements.end() ? nullptr : &*found;
}

const RuntimePlacement* Runtime::find(const std::int32_t area_id,
    const std::optional<std::int32_t> scene_id,
    const std::int16_t object_id) const {
  const auto found{std::ranges::find_if(
      m_placements, [area_id, scene_id, object_id](const RuntimePlacement& placement) {
        return placement.area_id == area_id && placement.scene_id == scene_id &&
               placement.object_id == object_id;
      })};
  return found == m_placements.end() ? nullptr : &*found;
}

std::expected<void, std::string> Runtime::set_enabled(const std::int32_t area_id,
    const std::optional<std::int32_t> scene_id,
    const std::int16_t object_id,
    const bool enabled) {
  RuntimePlacement* const placement{find(area_id, scene_id, object_id)};
  if (placement == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("{} object {} is not materialized in AREA {}{}",
            scene_id.has_value() ? "SCENE" : "AREA",
            object_id,
            area_id,
            scene_id.has_value() ? fmt::format(" SCENE {}", scene_id.value()) : std::string{})};
  }
  placement->enabled = enabled;
  return {};
}

}  // namespace App::ObjectPlacement
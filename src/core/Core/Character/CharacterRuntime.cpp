#include "Core/Character/CharacterRuntime.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/Texture3DT.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "Core/Vertex.hpp"

namespace App::Character {

namespace {

constexpr std::string_view K_CHARACTER_MODEL_DIRECTORY{"MESHES/PERSOS"};

std::filesystem::path character_resource_path(
    const std::string_view resource, const std::string_view extension) {
  std::filesystem::path path{K_CHARACTER_MODEL_DIRECTORY};
  path /= std::string{resource} + std::string{extension};
  return path;
}

void resolve_bounds(ModelResource& resource) {
  std::array<float, 3> minimum{std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max()};
  std::array<float, 3> maximum{std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest()};
  bool has_vertices{false};
  for (const Omikron::MaterialGroup& group : resource.groups) {
    for (const Vertex& vertex : group.vertices) {
      has_vertices = true;
      for (std::size_t axis{0}; axis < 3U; ++axis) {
        minimum.at(axis) = std::min(minimum.at(axis), vertex.position.at(axis));
        maximum.at(axis) = std::max(maximum.at(axis), vertex.position.at(axis));
      }
    }
  }
  if (!has_vertices) {
    return;
  }

  resource.bounds_center = App::Runtime::Vec3{.x = (minimum.at(0) + maximum.at(0)) * 0.5F,
      .y = (minimum.at(1) + maximum.at(1)) * 0.5F,
      .z = (minimum.at(2) + maximum.at(2)) * 0.5F};
  const float extent_x{maximum.at(0) - minimum.at(0)};
  const float extent_y{maximum.at(1) - minimum.at(1)};
  const float extent_z{maximum.at(2) - minimum.at(2)};
  resource.bounds_radius =
      0.5F * std::sqrt((extent_x * extent_x) + (extent_y * extent_y) + (extent_z * extent_z));
}

}  // namespace

Runtime::Runtime() : Runtime{load_model_resource} {}

Runtime::Runtime(ModelLoader model_loader) : m_model_loader{std::move(model_loader)} {}

void Runtime::set_model_loader(ModelLoader model_loader) {
  m_model_loader = std::move(model_loader);
}

std::expected<std::shared_ptr<const ModelResource>, std::string> Runtime::load_model_resource(
    const std::string_view model_resource) {
  APP_PROFILE_FUNCTION();

  if (model_resource.empty()) {
    return std::expected<std::shared_ptr<const ModelResource>, std::string>{
        std::unexpect, "character definition has an empty model resource"};
  }

  const std::filesystem::path model_path{character_resource_path(model_resource, ".3DO")};
  auto model_file{load_game_file(model_path)};
  if (!model_file) {
    return std::expected<std::shared_ptr<const ModelResource>, std::string>{
        std::unexpect, fmt::format("character model '{}': {}", model_resource, model_file.error())};
  }
  auto model{Omikron::Model3DO::load(std::span<const std::byte>{model_file->bytes})};
  if (!model) {
    return std::expected<std::shared_ptr<const ModelResource>, std::string>{
        std::unexpect, fmt::format("character model '{}': {}", model_resource, model.error())};
  }
  auto groups{Omikron::Model3DO::build_static_geometry(model.value())};
  if (!groups) {
    return std::expected<std::shared_ptr<const ModelResource>, std::string>{std::unexpect,
        fmt::format("character model '{}' geometry: {}", model_resource, groups.error())};
  }

  const std::filesystem::path texture_path{character_resource_path(model_resource, ".3DT")};
  auto texture_file{load_game_file(texture_path)};
  if (!texture_file) {
    return std::expected<std::shared_ptr<const ModelResource>, std::string>{std::unexpect,
        fmt::format("character textures '{}': {}", model_resource, texture_file.error())};
  }
  auto images{
      Omikron::Texture3DT::load(std::span<const std::byte>{texture_file->bytes}, model->materials)};
  if (!images) {
    return std::expected<std::shared_ptr<const ModelResource>, std::string>{
        std::unexpect, fmt::format("character textures '{}': {}", model_resource, images.error())};
  }

  auto resource{std::make_shared<ModelResource>()};
  resource->name = model_resource;
  resource->resolved_model_path = model_file->resolved.string();
  resource->resolved_texture_path = texture_file->resolved.string();
  resource->model = std::move(model).value();
  resource->groups = std::move(groups).value();
  resource->images = std::move(images).value();
  resolve_bounds(*resource);
  return std::shared_ptr<const ModelResource>{std::move(resource)};
}

std::expected<void, std::string> Runtime::activate(const std::int32_t area_id,
    const Omikron::IamAreaRecord& area,
    const Script::AreaCharacterActivationRequest& request) {
  APP_PROFILE_FUNCTION();

  if (request.character_id == -1) {
    return std::expected<void, std::string>{
        std::unexpect, "character -1 current-character model-flag path is not materialized yet"};
  }
  if (request.character_id < 0) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("invalid negative character ID {}", request.character_id)};
  }

  const auto placement{area.character_by_id(request.character_id)};
  if (!placement.has_value()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("character ID {} not found in active AREA table 0", request.character_id)};
  }
  const auto definition{
      area.character_definition_by_character_id(request.character_id)};
  if (!definition.has_value()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format(
            "character ID {} has no matching authored AREA table-4 record",
            request.character_id)};
  }
  if (definition->model_resource.empty()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("character definition {} has no model resource", definition->character_id)};
  }

  std::shared_ptr<const ModelResource> resource;
  const auto cached{m_model_resources.find(definition->model_resource)};
  if (cached != m_model_resources.end()) {
    resource = cached->second;
  } else {
    auto loaded{m_model_loader(definition->model_resource)};
    if (!loaded) {
      return std::expected<void, std::string>{std::unexpect, std::move(loaded).error()};
    }
    resource = std::move(loaded).value();
    m_model_resources.emplace(definition->model_resource, resource);
  }

  RuntimeCharacter* character{find(request.character_id)};
  if (character == nullptr) {
    m_characters.push_back(RuntimeCharacter{.instance_id = m_characters.size(),
        .character_id = request.character_id,
        .area_id = area_id,
        .active = true,
        .area_present = true,
        .serialized_area_position = placement->serialized_position,
        .serialized_orientation_units = placement->orientation_units,
        .transform = {},
        .runtime_orientation_degrees = 0,
        .definition_name = definition->name,
        .model_resource_name = definition->model_resource,
        .model_resource = std::move(resource)});
    character = &m_characters.back();
  } else {
    character->active = true;
    character->area_present = true;
    character->area_id = area_id;
    character->serialized_area_position = placement->serialized_position;
    character->serialized_orientation_units = placement->orientation_units;
    character->definition_name = definition->name;
    character->model_resource_name = definition->model_resource;
    character->model_resource = std::move(resource);
  }

  if (request.apply_area_transform) {
    constexpr float k_degrees_to_radians{std::numbers::pi_v<float> / 180.0F};
    character->runtime_orientation_degrees =
        App::Runtime::area_angle_to_degrees(placement->orientation_units);
    character->transform.translation =
        App::Runtime::area_position_to_inches(placement->serialized_position);
    character->transform.matrix = App::Runtime::rotation_y(
        static_cast<float>(character->runtime_orientation_degrees) * k_degrees_to_radians);
    character->transform.scale = App::Runtime::Vec3{.x = 1.0F, .y = 1.0F, .z = 1.0F};
  }
  return {};
}

RuntimeCharacter* Runtime::find(const std::int16_t character_id) {
  const auto found{std::ranges::find(m_characters, character_id, &RuntimeCharacter::character_id)};
  return found == m_characters.end() ? nullptr : &(*found);
}

const RuntimeCharacter* Runtime::find(const std::int16_t character_id) const {
  const auto found{std::ranges::find(m_characters, character_id, &RuntimeCharacter::character_id)};
  return found == m_characters.end() ? nullptr : &(*found);
}

std::span<const RuntimeCharacter> Runtime::characters() const {
  return m_characters;
}

std::size_t Runtime::model_resource_count() const {
  return m_model_resources.size();
}

}  // namespace App::Character

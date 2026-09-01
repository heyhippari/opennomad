#include "Core/Character/CharacterRuntime.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/CtlControlSet.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/Texture3DT.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "Core/Vertex.hpp"

namespace App::Character {

std::optional<App::Runtime::Transform> RuntimeCharacter::object_model_transform(
    const std::size_t object_index) const {
  if (object_index >= runtime_objects.size()) {
    return std::nullopt;
  }
  const Omikron::Model3DOData::RuntimeObjectState& object{runtime_objects.at(object_index)};
  return App::Runtime::Transform{.matrix = object.world_matrix,
      .translation = object.world_translation,
      .scale = object.scale};
}

std::optional<App::Runtime::Transform> RuntimeCharacter::object_world_transform(
    const std::size_t object_index) const {
  const std::optional<App::Runtime::Transform> model_transform{
      object_model_transform(object_index)};
  if (!model_transform.has_value()) {
    return std::nullopt;
  }
  return App::Runtime::compose(model_transform.value(), presentation_transform());
}

namespace {

constexpr std::string_view K_CHARACTER_MODEL_DIRECTORY{"MESHES/PERSOS"};
/// Runtime generically loads character control sets from ANIMS/<name>.
constexpr std::string_view K_CONTROL_SET_DIRECTORY{"ANIMS"};

[[nodiscard]] BodyIdentity allocate_body_identity() {
  // Identity only needs uniqueness/monotonicity; it carries no synchronization
  // semantics of its own.
  static std::atomic_uint64_t next_identity{1U};
  return next_identity.fetch_add(1U, std::memory_order_relaxed);
}

/// Cache key for shared CTL banks: the authored control-set name uppercased
/// (original data uses inconsistent casing).
[[nodiscard]] std::string normalized_control_set_name(const std::string_view control_set_name) {
  std::string normalized{control_set_name};
  std::ranges::transform(normalized, normalized.begin(), [](const char character) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
  });
  return normalized;
}

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

void resolve_actor_object(ModelResource& resource) {
  resource.actor_object_index = actor_object_index(resource.model);
}

}  // namespace

std::optional<std::size_t> actor_object_index(const Omikron::Model3DOData& model) {
  std::optional<std::size_t> selected;
  std::uint64_t greatest_polygon_count{0};
  for (std::size_t index{0}; index < model.meshes.size(); ++index) {
    const Omikron::MeshDescriptor& mesh{model.meshes.at(index)};
    const std::uint64_t polygon_count{static_cast<std::uint64_t>(mesh.triangle_count) +
                                      static_cast<std::uint64_t>(mesh.rectangle_count)};
    if (!selected.has_value() || polygon_count > greatest_polygon_count) {
      selected = index;
      greatest_polygon_count = polygon_count;
    }
  }
  return selected;
}

Runtime::Runtime() : Runtime{load_model_resource} {}

Runtime::Runtime(ModelLoader model_loader)
    : m_model_loader{std::move(model_loader)},
      m_ctl_bank_loader{load_ctl_bank} {}

void Runtime::set_model_loader(ModelLoader model_loader) {
  m_model_loader = std::move(model_loader);
}

void Runtime::set_ctl_bank_loader(CtlBankLoader ctl_bank_loader) {
  m_ctl_bank_loader = std::move(ctl_bank_loader);
}

std::expected<std::shared_ptr<const Omikron::CtlControlSet>, std::string> Runtime::load_ctl_bank(
    const std::string_view control_set_name) {
  APP_PROFILE_FUNCTION();

  if (control_set_name.empty()) {
    return std::expected<std::shared_ptr<const Omikron::CtlControlSet>, std::string>{
        std::unexpect, "character definition has an empty adventure control set"};
  }

  const std::filesystem::path control_set_path{
      std::filesystem::path{K_CONTROL_SET_DIRECTORY} /
      (std::string{control_set_name} + std::string{".CTL"})};
  auto control_set_file{load_game_file(control_set_path)};
  if (!control_set_file) {
    return std::expected<std::shared_ptr<const Omikron::CtlControlSet>, std::string>{std::unexpect,
        fmt::format("character control set '{}': {}", control_set_name, control_set_file.error())};
  }
  auto control_set{
      Omikron::CtlControlSet::load(std::span<const std::byte>{control_set_file->bytes})};
  if (!control_set) {
    return std::expected<std::shared_ptr<const Omikron::CtlControlSet>, std::string>{std::unexpect,
        fmt::format("character control set '{}': {}", control_set_name, control_set.error())};
  }
  return std::make_shared<const Omikron::CtlControlSet>(std::move(control_set).value());
}

std::expected<void, std::string> Runtime::ensure_adventure_controller(
    const std::int16_t character_id, const std::string_view adventure_control_set) {
  if (adventure_control_set.empty()) {
    return {};
  }
  RuntimeCharacter* character{find(character_id)};
  if (character == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("character {} is not materialized for CTL controller setup", character_id)};
  }

  const std::string normalized_name{normalized_control_set_name(adventure_control_set)};
  if (character->ctl_controller.has_value() &&
      character->ctl_controller->resource_name() == normalized_name) {
    // Same immutable bank: the live controller state is preserved.
    return {};
  }

  std::shared_ptr<const Omikron::CtlControlSet> bank;
  if (const auto cached{m_ctl_banks.find(normalized_name)}; cached != m_ctl_banks.end()) {
    bank = cached->second;
  } else {
    auto loaded{m_ctl_bank_loader(adventure_control_set)};
    if (!loaded) {
      return std::expected<void, std::string>{std::unexpect, std::move(loaded).error()};
    }
    bank = std::move(loaded).value();
    m_ctl_banks.emplace(normalized_name, bank);
  }

  auto controller{CtlController::create(std::move(bank), normalized_name)};
  if (!controller) {
    return std::expected<void, std::string>{std::unexpect, std::move(controller).error()};
  }
  character->ctl_controller = std::move(controller).value();
  return {};
}

std::expected<void, std::string> Runtime::ensure_adventure_controller(
    const BodyIdentity body_identity, const std::string_view adventure_control_set) {
  const RuntimeCharacter* const character{find_body(body_identity)};
  if (character == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("body {} is not materialized", body_identity)};
  }
  return ensure_adventure_controller(character->character_id, adventure_control_set);
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
  resolve_actor_object(*resource);
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
  const auto definition{area.character_definition_by_character_id(request.character_id)};
  if (!definition.has_value()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format(
            "character ID {} has no matching authored AREA table-4 record", request.character_id)};
  }
  return materialize_character(area_id,
      std::nullopt,
      request.character_id,
      placement->serialized_position,
      placement->orientation_units,
      definition->name,
      definition->model_resource,
      request.apply_area_transform,
      true);
}

std::expected<void, std::string> Runtime::ensure_area_character(const std::int32_t area_id,
    const Omikron::IamAreaRecord& area,
    const std::int16_t character_id) {
  if (character_id < 0) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("invalid negative character ID {}", character_id)};
  }
  const auto placement{area.character_by_id(character_id)};
  if (!placement.has_value()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("character ID {} not found in owner AREA table 0", character_id)};
  }
  const auto definition{area.character_definition_by_character_id(character_id)};
  if (!definition.has_value()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("character ID {} has no matching authored AREA table-4 record", character_id)};
  }

  if (RuntimeCharacter* const existing{find(character_id)}; existing != nullptr) {
    existing->active = true;
    existing->area_present = true;
    existing->area_id = area_id;
    existing->scene_id.reset();
    return {};
  }
  return materialize_character(area_id,
      std::nullopt,
      character_id,
      placement->serialized_position,
      placement->orientation_units,
      definition->name,
      definition->model_resource,
      true,
      true);
}

std::expected<void, std::string> Runtime::ensure_scene_character(const std::int32_t area_id,
    const std::int32_t scene_id,
    const Omikron::IamSceneRecord& scene,
    const std::int16_t character_id) {
  if (character_id < 0) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("invalid negative character ID {}", character_id)};
  }
  const auto placement{scene.character_by_id(character_id)};
  if (!placement.has_value()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("character ID {} not found in owner SCENE table 0", character_id)};
  }
  const auto definition{scene.character_definition_by_character_id(character_id)};
  if (!definition.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("SCENE character {} has no matching definition", character_id)};
  }

  if (RuntimeCharacter* const existing{find(character_id)}; existing != nullptr) {
    existing->active = true;
    existing->area_present = true;
    existing->area_id = area_id;
    existing->scene_id = scene_id;
    return {};
  }
  return materialize_character(area_id,
      scene_id,
      character_id,
      placement->serialized_position,
      placement->orientation_units,
      definition->name,
      definition->model_resource,
      true,
      true);
}

std::expected<void, std::string> Runtime::preload_scene_characters(
    const std::int32_t area_id, const std::int32_t scene_id, const Omikron::IamSceneRecord& scene) {
  const std::vector<Omikron::IamSceneCharacterRecord> placements{scene.character_placements()};
  for (const Omikron::IamSceneCharacterRecord& placement : placements) {
    RuntimeCharacter* const existing{find(placement.character_id)};
    if (existing != nullptr && existing->active && existing->area_present) {
      // A transferred/current body is already live. SCENE attachment may take
      // ownership of it, but must not teleport, hide or reset that live body.
      existing->area_id = area_id;
      existing->scene_id = scene_id;
      continue;
    }

    const auto definition{scene.character_definition_by_character_id(placement.character_id)};
    if (!definition.has_value()) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("SCENE character {} has no matching definition", placement.character_id)};
    }
    if (auto result{materialize_character(area_id,
            scene_id,
            placement.character_id,
            placement.serialized_position,
            placement.orientation_units,
            definition->name,
            definition->model_resource,
            true,
            true)};
        !result) {
      return result;
    }
  }
  return {};
}

void Runtime::dematerialize_scene_characters(const std::int32_t area_id,
    const std::int32_t scene_id,
    const std::optional<std::int16_t> preserved_character_id) {
  for (RuntimeCharacter& character : m_characters) {
    if (character.area_id == area_id && character.scene_id == scene_id) {
      if (preserved_character_id.has_value() &&
          character.character_id == preserved_character_id.value()) {
        character.scene_id.reset();
        continue;
      }
      character.active = false;
      character.area_present = false;
      character.scene_id.reset();
      character.pose_revision += 1U;
    }
  }
}

std::expected<void, std::string> Runtime::set_presentation_enabled(
    const std::int16_t character_id, const bool enabled) {
  RuntimeCharacter* const character{find(character_id)};
  if (character == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("current controlled character {} is not materialized", character_id)};
  }
  character->presentation_enabled = enabled;
  return {};
}

std::expected<void, std::string> Runtime::set_body_presentation_enabled(
    const BodyIdentity body_identity, const bool enabled) {
  RuntimeCharacter* const character{find_body(body_identity)};
  if (character == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("body {} is not materialized", body_identity)};
  }
  character->presentation_enabled = enabled;
  return {};
}

std::expected<void, std::string> Runtime::deactivate_character(const std::int16_t character_id) {
  RuntimeCharacter* const character{find(character_id)};
  if (character == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("character {} is not materialized", character_id)};
  }
  character->active = false;
  character->area_present = false;
  character->pose_revision += 1U;
  return {};
}

std::expected<RuntimeCharacter, std::string> Runtime::extract_character(
    const std::int16_t character_id) {
  const auto found{std::ranges::find(m_characters, character_id, &RuntimeCharacter::character_id)};
  if (found == m_characters.end()) {
    return std::expected<RuntimeCharacter, std::string>{
        std::unexpect, fmt::format("character {} is not materialized", character_id)};
  }
  RuntimeCharacter extracted{std::move(*found)};
  m_characters.erase(found);
  // instance_id is deliberately Runtime-local and dense; body_identity is not.
  for (std::size_t index{0}; index < m_characters.size(); ++index) {
    m_characters.at(index).instance_id = index;
  }
  return extracted;
}

std::expected<RuntimeCharacter, std::string> Runtime::extract_body(
    const BodyIdentity body_identity) {
  const auto found{
      std::ranges::find(m_characters, body_identity, &RuntimeCharacter::body_identity)};
  if (found == m_characters.end()) {
    return std::expected<RuntimeCharacter, std::string>{
        std::unexpect, fmt::format("body {} is not materialized", body_identity)};
  }
  RuntimeCharacter extracted{std::move(*found)};
  m_characters.erase(found);
  for (std::size_t index{0}; index < m_characters.size(); ++index) {
    m_characters.at(index).instance_id = index;
  }
  return extracted;
}

std::expected<void, std::string> Runtime::adopt_character(RuntimeCharacter character) {
  if (find(character.character_id) != nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("character {} already belongs to target world", character.character_id)};
  }
  if (character.model_resource != nullptr && !character.model_resource_name.empty()) {
    m_model_resources.try_emplace(character.model_resource_name, character.model_resource);
  }
  // Preserve `body_identity`: this is the same logical Runtime actor/body.
  character.instance_id = m_characters.size();
  m_characters.push_back(std::move(character));
  return {};
}

std::expected<void, std::string> Runtime::transfer_character_to(
    Runtime& target, const std::int16_t character_id) {
  if (target.find(character_id) != nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("character {} already belongs to target world", character_id)};
  }
  auto extracted{extract_character(character_id)};
  if (!extracted) {
    return std::expected<void, std::string>{std::unexpect, extracted.error()};
  }
  return target.adopt_character(std::move(extracted).value());
}

std::expected<void, std::string> Runtime::transfer_body_to(
    Runtime& target, const BodyIdentity body_identity) {
  if (target.find_body(body_identity) != nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("body {} already belongs to target world", body_identity)};
  }
  auto extracted{extract_body(body_identity)};
  if (!extracted) {
    return std::expected<void, std::string>{std::unexpect, extracted.error()};
  }
  return target.adopt_character(std::move(extracted).value());
}

std::expected<void, std::string> Runtime::place_character_at_address(
    const std::int16_t character_id, const Omikron::IamAreaAddressRecord& address) {
  RuntimeCharacter* character{find(character_id)};
  if (character == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("current controlled character {} is not materialized", character_id)};
  }
  character->serialized_area_position = address.serialized_position;
  character->serialized_orientation_units = address.orientation_units;
  character->runtime_orientation_degrees =
      App::Runtime::area_angle_to_degrees(address.orientation_units);
  character->transform.translation =
      App::Runtime::area_position_to_inches(address.serialized_position);
  character->set_principal_orientation(App::Runtime::Vec3{
      .x = 0.0F, .y = static_cast<float>(character->runtime_orientation_degrees), .z = 0.0F});
  character->transform.scale = App::Runtime::Vec3{.x = 1.0F, .y = 1.0F, .z = 1.0F};
  character->pose_revision += 1U;
  return {};
}

std::expected<void, std::string> Runtime::place_body_at_address(
    const BodyIdentity body_identity, const Omikron::IamAreaAddressRecord& address) {
  RuntimeCharacter* character{find_body(body_identity)};
  if (character == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("body {} is not materialized", body_identity)};
  }
  character->serialized_area_position = address.serialized_position;
  character->serialized_orientation_units = address.orientation_units;
  character->runtime_orientation_degrees =
      App::Runtime::area_angle_to_degrees(address.orientation_units);
  character->transform.translation =
      App::Runtime::area_position_to_inches(address.serialized_position);
  character->set_principal_orientation(App::Runtime::Vec3{
      .x = 0.0F, .y = static_cast<float>(character->runtime_orientation_degrees), .z = 0.0F});
  character->transform.scale = App::Runtime::Vec3{.x = 1.0F, .y = 1.0F, .z = 1.0F};
  character->pose_revision += 1U;
  return {};
}

std::expected<void, std::string> Runtime::materialize_character(const std::int32_t area_id,
    const std::optional<std::int32_t> scene_id,
    const std::int16_t character_id,
    const std::array<std::int32_t, 3>& serialized_position,
    const std::int16_t orientation_units,
    const std::string_view definition_name,
    const std::string_view model_resource,
    const bool apply_transform,
    const bool activate) {
  if (model_resource.empty()) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("character definition {} has no model resource", character_id)};
  }

  std::shared_ptr<const ModelResource> resource;
  const auto cached{m_model_resources.find(std::string{model_resource})};
  if (cached != m_model_resources.end()) {
    resource = cached->second;
  } else {
    auto loaded{m_model_loader(model_resource)};
    if (!loaded) {
      return std::expected<void, std::string>{std::unexpect, std::move(loaded).error()};
    }
    resource = std::move(loaded).value();
    m_model_resources.emplace(std::string{model_resource}, resource);
  }

  RuntimeCharacter* character{find(character_id)};
  if (character == nullptr) {
    m_characters.push_back(RuntimeCharacter{.body_identity = allocate_body_identity(),
        .instance_id = m_characters.size(),
        .character_id = character_id,
        .area_id = area_id,
        .scene_id = scene_id,
        .active = activate,
        .area_present = activate,
        .serialized_area_position = serialized_position,
        .serialized_orientation_units = orientation_units,
        .transform = {},
        .runtime_orientation_degrees = 0,
        .ctl_controller = std::nullopt,
        .controller_enabled = false,
        .pose_owner = PoseOwner::k_model_defaults,
        .adventure_mode = 0,
        .suppress_automatic_movement_heading = false,
        .definition_name = std::string{definition_name},
        .model_resource_name = std::string{model_resource},
        .model_resource = std::move(resource),
        .runtime_objects = {},
        .object_poses = {},
        .posed_groups = {},
        .pose_revision = 0,
        .body_animation = {},
        .dialog_performance = {}});
    character = &m_characters.back();
  } else {
    character->active = activate;
    character->area_present = activate;
    character->area_id = area_id;
    character->scene_id = scene_id;
    character->serialized_area_position = serialized_position;
    character->serialized_orientation_units = orientation_units;
    character->definition_name = definition_name;
    character->model_resource_name = model_resource;
    character->model_resource = std::move(resource);
  }

  if (apply_transform) {
    character->runtime_orientation_degrees = App::Runtime::area_angle_to_degrees(orientation_units);
    character->transform.translation = App::Runtime::area_position_to_inches(serialized_position);
    character->set_principal_orientation(App::Runtime::Vec3{
        .x = 0.0F, .y = static_cast<float>(character->runtime_orientation_degrees), .z = 0.0F});
    character->transform.scale = App::Runtime::Vec3{.x = 1.0F, .y = 1.0F, .z = 1.0F};
  }
  character->runtime_objects = character->model_resource->model.runtime_objects;
  character->object_poses.assign(character->runtime_objects.size(), BodyAnimationObjectPose{});
  character->posed_groups = character->model_resource->groups;
  character->body_animation = BodyAnimationPlayback{};
  character->dialog_performance.reset();
  character->pose_owner = PoseOwner::k_model_defaults;
  character->pose_revision += 1U;
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

RuntimeCharacter* Runtime::find_body(const BodyIdentity body_identity) {
  const auto found{
      std::ranges::find(m_characters, body_identity, &RuntimeCharacter::body_identity)};
  return found == m_characters.end() ? nullptr : &(*found);
}

const RuntimeCharacter* Runtime::find_body(const BodyIdentity body_identity) const {
  const auto found{
      std::ranges::find(m_characters, body_identity, &RuntimeCharacter::body_identity)};
  return found == m_characters.end() ? nullptr : &(*found);
}

std::span<const RuntimeCharacter> Runtime::characters() const {
  return m_characters;
}

std::size_t Runtime::model_resource_count() const {
  return m_model_resources.size();
}

void Runtime::reset_pose(const std::int16_t character_id) {
  RuntimeCharacter* character{find(character_id)};
  if (character == nullptr || character->model_resource == nullptr) {
    return;
  }
  character->runtime_objects = character->model_resource->model.runtime_objects;
  character->object_poses.assign(character->runtime_objects.size(), BodyAnimationObjectPose{});
  character->posed_groups = character->model_resource->groups;
  character->body_animation = BodyAnimationPlayback{};
  character->dialog_performance.reset();
  character->pose_owner = PoseOwner::k_model_defaults;
  character->pose_revision += 1U;
}

void Runtime::reset_pose(const BodyIdentity body_identity) {
  RuntimeCharacter* character{find_body(body_identity)};
  if (character == nullptr || character->model_resource == nullptr) {
    return;
  }
  character->runtime_objects = character->model_resource->model.runtime_objects;
  character->object_poses.assign(character->runtime_objects.size(), BodyAnimationObjectPose{});
  character->posed_groups = character->model_resource->groups;
  character->body_animation = BodyAnimationPlayback{};
  character->dialog_performance.reset();
  character->pose_owner = PoseOwner::k_model_defaults;
  character->pose_revision += 1U;
}

std::expected<void, std::string> Runtime::apply_dialog_performance(
    const std::int16_t character_id, DialogPerformanceOverlay overlay) {
  RuntimeCharacter* character{find(character_id)};
  if (character == nullptr || character->model_resource == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, "dialogue performance character is not loaded"};
  }
  const Omikron::Model3DOData& model{character->model_resource->model};
  if (overlay.object_rotations.size() != model.meshes.size() ||
      overlay.root_object_index >= model.meshes.size()) {
    return std::expected<void, std::string>{
        std::unexpect, "dialogue object overlay does not match character model"};
  }

  std::vector<Omikron::Model3DOData::RuntimeObjectState> composed{character->runtime_objects};
  for (std::size_t index{0}; index < overlay.object_rotations.size(); ++index) {
    if (overlay.object_rotations.at(index).has_value()) {
      composed.at(index).animation_matrix =
          App::Runtime::quaternion_matrix(overlay.object_rotations.at(index).value_or({}));
    }
  }
  Omikron::Model3DOData::RuntimeObjectState& root{composed.at(overlay.root_object_index)};
  root.local_offset.x += overlay.root_translation_delta.x;
  root.local_offset.y += overlay.root_translation_delta.y;
  root.local_offset.z += overlay.root_translation_delta.z;
  if (auto resolved{Omikron::Model3DO::resolve_runtime_transforms(model, std::span{composed})};
      !resolved) {
    return resolved;
  }

  std::vector<Omikron::RawVertex> vertices{model.vertices};
  if (overlay.face_mesh_index.has_value()) {
    const std::size_t face_index{overlay.face_mesh_index.value_or(0U)};
    if (face_index >= model.meshes.size()) {
      return std::expected<void, std::string>{
          std::unexpect, "dialogue face mesh index is out of range"};
    }
    const Omikron::MeshDescriptor& face{model.meshes.at(face_index)};
    if (face.vertex_count != overlay.face_vertices.size()) {
      return std::expected<void, std::string>{
          std::unexpect, "dialogue face vertex count does not match face mesh"};
    }
    if (face.vertex_base > vertices.size() ||
        face.vertex_count > vertices.size() - face.vertex_base) {
      return std::expected<void, std::string>{
          std::unexpect, "dialogue face source vertex range is out of bounds"};
    }
    for (std::size_t index{0}; index < overlay.face_vertices.size(); ++index) {
      Omikron::RawVertex& vertex{vertices.at(face.vertex_base + index)};
      vertex.position = overlay.face_vertices.at(index).position;
      vertex.normal = overlay.face_vertices.at(index).normal;
    }
  }
  auto groups{Omikron::Model3DO::build_posed_geometry(model,
      std::span<const Omikron::Model3DOData::RuntimeObjectState>{composed},
      std::span<const Omikron::RawVertex>{vertices})};
  if (!groups) {
    return std::expected<void, std::string>{std::unexpect, std::move(groups).error()};
  }
  character->posed_groups = std::move(groups).value();
  character->dialog_performance = std::move(overlay);
  character->pose_revision += 1U;
  return {};
}

void Runtime::clear_dialog_performance(const std::int16_t character_id) {
  RuntimeCharacter* character{find(character_id)};
  if (character == nullptr || character->model_resource == nullptr ||
      !character->dialog_performance.has_value()) {
    return;
  }
  character->dialog_performance.reset();
  auto groups{Omikron::Model3DO::build_posed_geometry(character->model_resource->model,
      std::span<const Omikron::Model3DOData::RuntimeObjectState>{character->runtime_objects})};
  if (!groups) {
    return;
  }
  character->posed_groups = std::move(groups).value();
  character->pose_revision += 1U;
}

}  // namespace App::Character

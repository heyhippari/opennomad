#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamObjectPlacement.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/Texture3DT.hpp"
#include "Core/RuntimeMath.hpp"

namespace App {

class GameState;

namespace ObjectPlacement {

/// Immutable decoded MESHES/OBJETS resource shared by every live placement of
/// the same authored model stem.
struct ModelResource {
  std::string name;
  std::string resolved_model_path;
  std::string resolved_texture_path;
  Omikron::Model3DOData model;
  std::vector<Omikron::MaterialGroup> groups;
  std::vector<Omikron::Texture3DTImage> images;
};

/// One materialized IAM table-1 placement.
///
/// Presence in this container corresponds to persistent state bit 0. `enabled`
/// corresponds to bit 1 and controls whether presentation submits the object.
struct RuntimePlacement {
  std::size_t instance_id{0};
  std::int32_t area_id{-1};
  std::optional<std::int32_t> scene_id;
  std::int16_t runtime_object_slot_seed{-1};
  std::int16_t object_id{-1};
  std::int16_t persistent_state_index{-1};
  std::uint16_t type_or_flags{0};
  bool enabled{false};
  App::Runtime::Transform transform{};
  std::string model_resource_name;
  std::shared_ptr<const ModelResource> model_resource;

  [[nodiscard]] bool renderable() const {
    return enabled && model_resource != nullptr && !model_resource->groups.empty();
  }
};

/// CPU-side owner of resident AREA/SCENE table-1 object placements.
///
/// AREA residency owns AREA placements for the lifetime of the world runtime.
/// Attached SCENEs add/remove their own placement subset independently.
class Runtime {
 public:
  using ModelLoader = std::function<
      std::expected<std::shared_ptr<const ModelResource>, std::string>(std::string_view)>;

  Runtime();
  explicit Runtime(ModelLoader loader);

  void set_model_loader(ModelLoader loader);

  [[nodiscard]] std::expected<void, std::string> materialize_area_objects(
      std::int32_t area_id, const Omikron::IamAreaRecord& area, const GameState& game_state);
  [[nodiscard]] std::expected<void, std::string> materialize_scene_objects(std::int32_t area_id,
      std::int32_t scene_id,
      const Omikron::IamSceneRecord& scene,
      const GameState& game_state);

  void dematerialize_scene_objects(std::int32_t area_id, std::int32_t scene_id);

  [[nodiscard]] std::expected<void, std::string> set_enabled(std::int32_t area_id,
      std::optional<std::int32_t> scene_id,
      std::int16_t object_id,
      bool enabled);

  [[nodiscard]] RuntimePlacement* find(std::int32_t area_id,
      std::optional<std::int32_t> scene_id,
      std::int16_t object_id);
  [[nodiscard]] const RuntimePlacement* find(std::int32_t area_id,
      std::optional<std::int32_t> scene_id,
      std::int16_t object_id) const;

  [[nodiscard]] std::span<const RuntimePlacement> placements() const {
    return m_placements;
  }

 private:
  [[nodiscard]] static std::expected<std::shared_ptr<const ModelResource>, std::string>
  load_model_resource(std::string_view resource_name);

  [[nodiscard]] std::expected<void, std::string> materialize(std::int32_t area_id,
      std::optional<std::int32_t> scene_id,
      const Omikron::IamObjectPlacementRecord& placement,
      const Omikron::IamObjectDefinitionRecord& definition,
      std::uint8_t persistent_state);

  ModelLoader m_model_loader;
  std::vector<RuntimePlacement> m_placements;
  std::unordered_map<std::string, std::shared_ptr<const ModelResource>> m_model_resources;
  std::size_t m_next_instance_id{1};
};

}  // namespace ObjectPlacement
}  // namespace App
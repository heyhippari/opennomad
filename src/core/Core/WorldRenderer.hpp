#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <vector>

#include "Core/Mesh.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Shader.hpp"
#include "Core/Sprite/SpriteRenderer.hpp"
#include "Core/Texture.hpp"

namespace App {

class Camera;
class ScenarioRuntime;
struct WorldSceneContext;

struct WorldBounds {
  std::array<float, 3> center{};
  float radius{1.0F};
};

/// GL presentation object for one active WorldSceneContext.
///
/// ScenarioManager owns decoded CPU data and mutable ScenarioRuntime state;
/// this class owns only the GPU representation needed to draw that context.
/// Recreate it when the context generation changes.
class WorldRenderer {
 public:
  [[nodiscard]] static std::expected<std::unique_ptr<WorldRenderer>, std::string> create(
      WorldSceneContext& context);

  ~WorldRenderer() = default;
  WorldRenderer(const WorldRenderer&) = delete;
  WorldRenderer(WorldRenderer&&) = delete;
  WorldRenderer& operator=(const WorldRenderer&) = delete;
  WorldRenderer& operator=(WorldRenderer&&) = delete;

  void render(const Camera& camera, ScenarioRuntime* runtime);

  [[nodiscard]] const WorldBounds& bounds() const {
    return m_bounds;
  }

 private:
  WorldRenderer() = default;

  void draw_group(std::size_t index);

  std::unique_ptr<Shader> m_shader;
  std::deque<Mesh> m_meshes;
  std::vector<std::int32_t> m_group_material_ids;
  std::vector<std::uint32_t> m_group_flags;
  std::vector<Omikron::BlendMode> m_group_modes;
  std::vector<std::array<float, 3>> m_group_centers;
  std::vector<Texture2D> m_textures;

  Sprite::SpriteRenderer m_sprite_renderer;

  WorldBounds m_bounds{};
};

}  // namespace App
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Debug/SceneDebugView.hpp"
#include "Core/Object/ObjectPlacementRuntime.hpp"
#include "Core/Mesh.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Shader.hpp"
#include "Core/Sprite/SpriteRenderer.hpp"
#include "Core/Texture.hpp"

namespace App {

class Camera;
class ScenarioRuntime;
class WorldColorPipeline;
struct WorldSceneContext;

struct WorldBounds {
  std::array<float, 3> center{};  ///< Unscaled GL presentation-space center.
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

  void render(const Camera& camera,
      ScenarioRuntime* runtime,
      float uv_phase_u,
      float uv_phase_v,
      WorldColorPipeline& color_pipeline);
  /// Draws OpenNomad-native diagnostics after legacy color has been decoded.
  void render_debug_overlay(const Camera& camera, const ScenarioRuntime* runtime);

  [[nodiscard]] const WorldBounds& bounds() const {
    return m_bounds;
  }

  [[nodiscard]] std::size_t group_count() const {
    return m_meshes.size();
  }

  [[nodiscard]] std::size_t material_count() const {
    return m_textures.size();
  }

  [[nodiscard]] std::size_t mirror_group_count() const;
  [[nodiscard]] std::size_t uv_scroll_u_group_count() const;
  [[nodiscard]] std::size_t uv_scroll_v_group_count() const;
  [[nodiscard]] std::size_t environment_group_count() const;

  [[nodiscard]] Debug::SpriteRenderDebugState sprite_render_debug_state() const;
  void set_sprite_grayscale(bool enabled);
  [[nodiscard]] bool sprite_grayscale() const {
    return m_sprite_grayscale;
  }
  void set_geometry_wireframe(bool enabled) {
    m_geometry_wireframe = enabled;
  }
  [[nodiscard]] bool geometry_wireframe() const {
    return m_geometry_wireframe;
  }

 private:
  WorldRenderer() = default;

  void draw_group(std::size_t index, float uv_phase_u, float uv_phase_v, bool legacy_effect);
  void draw_character_group(const Character::RuntimeCharacter& character,
      const Camera& camera,
      std::size_t group_index,
      float uv_phase_u,
      float uv_phase_v,
      bool legacy_effect);
  void draw_object_group(const ObjectPlacement::RuntimePlacement& placement,
      const Camera& camera,
      std::size_t group_index,
      float uv_phase_u,
      float uv_phase_v,
      bool legacy_effect);
  void render_geometry_wireframe(const Camera& camera, const ScenarioRuntime* runtime);
  void sync_character_models(const ScenarioRuntime& runtime);
  void sync_object_models(const ScenarioRuntime& runtime);
  void sync_decor_model(const ScenarioRuntime& runtime);

  struct CharacterGpuModel {
    std::shared_ptr<const Character::ModelResource> resource;
    std::deque<Mesh> meshes;
    std::vector<std::int32_t> group_material_ids;
    std::vector<std::uint32_t> group_flags;
    std::vector<Omikron::BlendMode> group_modes;
    std::vector<GameColorTexture> textures;
    std::uint64_t pose_revision{0};
  };

  struct ObjectGpuModel {
    std::shared_ptr<const ObjectPlacement::ModelResource> resource;
    std::deque<Mesh> meshes;
    std::vector<std::int32_t> group_material_ids;
    std::vector<std::uint32_t> group_flags;
    std::vector<Omikron::BlendMode> group_modes;
    std::vector<GameColorTexture> textures;
  };

  std::unique_ptr<Shader> m_modern_shader;
  std::unique_ptr<Shader> m_legacy_shader;
  std::unique_ptr<Shader> m_wireframe_shader;
  std::deque<Mesh> m_meshes;
  std::vector<std::int32_t> m_group_material_ids;
  std::vector<std::uint32_t> m_group_flags;
  std::vector<Omikron::BlendMode> m_group_modes;
  std::vector<std::array<float, 3>> m_group_centers;
  std::vector<GameColorTexture> m_textures;

  Sprite::SpriteRenderer m_sprite_renderer;
  ScenarioRuntime* m_last_sprite_runtime{nullptr};
  bool m_sprite_grayscale{false};
  bool m_geometry_wireframe{false};
  std::unordered_map<std::size_t, std::unique_ptr<CharacterGpuModel>> m_character_models;
  std::vector<std::string> m_failed_character_models;
  std::unordered_map<std::size_t, std::unique_ptr<ObjectGpuModel>> m_object_models;
  std::vector<std::string> m_failed_object_models;
  std::uint64_t m_decor_pose_revision{0};
  /// Last accepted posed-decor bounds, retained only for mutation diagnostics.
  WorldBounds m_decor_pose_bounds{};
  std::size_t m_decor_pose_vertex_count{0};

  WorldBounds m_bounds{};
};

}  // namespace App

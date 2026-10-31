#pragma once

#include <glad/glad.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// NOLINTNEXTLINE(misc-include-cleaner) -- GLM umbrella header is canonical.
#include <glm/glm.hpp>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Debug/SceneDebugView.hpp"
#include "Core/DecorCulling.hpp"
#include "Core/Mesh.hpp"
#include "Core/Object/ObjectPlacementRuntime.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Shader.hpp"
#include "Core/Sprite/SpriteRenderer.hpp"
#include "Core/Texture.hpp"
#include "Core/Vertex.hpp"
#include "Core/ViewFrustum.hpp"
#include "Core/WorldFog.hpp"
#include "Core/WorldLighting.hpp"

namespace App {

class Camera;
class ScenarioRuntime;
class WorldColorPipeline;
class WorldShaderSet;
struct WorldSceneContext;

struct WorldBounds {
  std::array<float, 3> center{};  ///< Unscaled GL presentation-space center.
  float radius{1.0F};
};

/// GL presentation object for one active WorldSceneContext.
///
/// ScenarioManager owns decoded CPU data and mutable ScenarioRuntime state;
/// this class owns only the GPU representation needed to draw that context.
/// Recreate it when the context generation changes. Programs come from the
/// scene's WorldShaderSet, which must outlive the renderer.
class WorldRenderer {
 public:
  [[nodiscard]] static std::expected<std::unique_ptr<WorldRenderer>, std::string> create(
      WorldSceneContext& context, WorldShaderSet& shaders);

  ~WorldRenderer() = default;
  WorldRenderer(const WorldRenderer&) = delete;
  WorldRenderer(WorldRenderer&&) = delete;
  WorldRenderer& operator=(const WorldRenderer&) = delete;
  WorldRenderer& operator=(WorldRenderer&&) = delete;

  void render(const Camera& camera,
      ScenarioRuntime* runtime,
      float uv_phase_u,
      float uv_phase_v,
      const Fog::ShaderFog& fog,
      const Lighting::Ambient& ambient,
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

  /// Decor groups submitted versus frustum-culled by the most recent render().
  [[nodiscard]] const DecorCullStats& decor_cull_stats() const {
    return m_decor_cull_stats;
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
  /// Lit characters evaluate their lights per pixel (OpenNomad enhancement).
  /// Falls back to Runtime's vertex lighting if the variant cannot be built.
  void set_per_pixel_lighting(bool enabled) {
    m_per_pixel_lighting = enabled;
  }
  /// Opaque/cutout decor interpolates its baked colours with a cubic Bezier
  /// triangle (OpenNomad enhancement). The slope stream is built on first
  /// use; falls back to Runtime's linear colours if either part fails.
  void set_cubic_decor_colors(bool enabled) {
    m_cubic_decor_colors = enabled;
  }
  /// Detail Level preference (0..2, Runtime 0x0090E727) that biases each
  /// passerby's level of detail.
  void set_detail_level(std::uint8_t detail_level) {
    m_detail_level = detail_level;
  }
  /// Environment texture for mesh flag bit 26: Runtime copies the AREA sky's
  /// first texture page into it when the sky loads (0x0041D28A ->
  /// 0x004641E0). Null disables environment mapping.
  void set_environment_texture(const GameColorTexture* texture) {
    m_environment_texture = texture;
  }
  /// Passersby posed and submitted by the most recent render().
  [[nodiscard]] std::size_t crowd_visible_count() const {
    return m_crowd_visible_count;
  }

 private:
  WorldRenderer() = default;

  /// Per-instance transforms, computed once per character or placement per pass.
  struct InstanceMatrices {
    glm::mat4 model_view{1.0F};
    glm::mat4 mvp{1.0F};
    /// Inverse of the model rotation/scale; lit characters only.
    glm::mat3 to_model{1.0F};
  };

  /// Binds `shader` and writes the per-frame uniforms every world draw shares.
  /// Resets the redundant-state tracking, since other passes may have bound
  /// their own program and textures in between.
  void begin_world_pass(
      const Shader& shader, const glm::mat4& mvp, const glm::mat4& view, bool legacy_effect);
  void bind_world_shader(const Shader& shader);
  void bind_world_texture(const Texture2D& texture);
  /// Environment mapping is active for a bit-26 group only with an
  /// environment texture and Detail Level >= 1 (Runtime 0x00464610 also
  /// requires renderer mode 0, the Direct3D path OpenNomad reproduces).
  [[nodiscard]] bool environment_mapping_active(std::uint32_t flags) const;
  /// Group uniforms that vary per draw; unchanged values are skipped by Shader.
  /// Also binds the environment texture to unit 1 for bit-26 groups.
  void apply_group_uniforms(const Shader& shader,
      std::uint32_t flags,
      Omikron::BlendMode mode,
      float uv_phase_u,
      float uv_phase_v,
      bool legacy_effect);
  void draw_group(std::size_t index, float uv_phase_u, float uv_phase_v, bool legacy_effect);
  struct CharacterGpuModel;
  /// Lighting inputs of one lit (character-path) instance.
  struct LitInstance {
    std::span<const Omikron::MaterialGroup> groups;
    std::span<const Omikron::Model3DOData::RuntimeObjectState> objects;
    Runtime::Transform presentation{};
    Runtime::Vec3 actor_position{};
    float actor_radius{0.0F};
  };
  void draw_lit_group(const CharacterGpuModel& gpu,
      std::span<const GameColorTexture> textures,
      const LitInstance& lit,
      const InstanceMatrices& matrices,
      std::size_t group_index,
      float uv_phase_u,
      float uv_phase_v,
      bool legacy_effect);
  void draw_character_group(const Character::RuntimeCharacter& character,
      const InstanceMatrices& matrices,
      std::size_t group_index,
      float uv_phase_u,
      float uv_phase_v,
      bool legacy_effect);
  void draw_object_group(const ObjectPlacement::RuntimePlacement& placement,
      const InstanceMatrices& matrices,
      std::size_t group_index,
      float uv_phase_u,
      float uv_phase_v,
      bool legacy_effect);
  /// Rebuilds the material-sorted order of the opaque/cutout decor groups.
  void rebuild_opaque_draw_order();
  /// Attaches the VertexColorSlopes stream to every decor mesh once. False
  /// when it cannot (logged once until the decor meshes are rebuilt).
  [[nodiscard]] bool ensure_decor_color_slopes();
  /// Runtime baked-colour clamp for decor, placed objects and unlit nodes.
  void apply_baked_lighting(const Shader& shader) const;
  /// Runtime lit-instance path for one character node group.
  void apply_actor_lighting(const Shader& shader,
      const LitInstance& lit,
      std::size_t group_index,
      const glm::mat3& to_model) const;
  void render_geometry_wireframe(const Camera& camera, const ScenarioRuntime* runtime);
  void sync_character_models(ScenarioRuntime& runtime);
  /// Selects, poses and uploads every visible passerby's level of detail.
  void sync_crowd(const ScenarioRuntime& runtime,
      const glm::mat4& view,
      const glm::mat4& projection,
      const ViewFrustum& frustum);
  /// True when the character at `character_index` is renderable and may be
  /// inside this frame's view volume.
  [[nodiscard]] bool character_presented(std::size_t character_index) const;
  void sync_object_models(const ScenarioRuntime& runtime);
  void sync_decor_model(const ScenarioRuntime& runtime);
  [[nodiscard]] bool patch_decor_model(const ScenarioRuntime& runtime);
  void index_decor_groups(const Omikron::Model3DOData& model,
      std::span<const Omikron::MaterialGroup> groups,
      std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects);

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
    /// Presentation-space sphere around every vertex; nullopt when unbounded.
    std::optional<BoundingSphere> local_bounds;
  };

  [[nodiscard]] static bool update_character_meshes(
      CharacterGpuModel& gpu, std::span<const Omikron::MaterialGroup> groups);
  /// Replaces `gpu`'s meshes with `groups` when the layout changed.
  static void rebuild_character_meshes(
      CharacterGpuModel& gpu, std::span<const Omikron::MaterialGroup> groups);

  /// Textures shared by every passerby of one crowd prototype.
  struct CrowdPrototypeGpu {
    std::shared_ptr<const Character::ModelResource> resource;
    std::vector<GameColorTexture> textures;
  };
  /// One passerby's posed level of detail.
  struct CrowdInstanceGpu {
    bool visible{false};
    std::size_t prototype{0};
    std::vector<Omikron::Model3DOData::RuntimeObjectState> objects;
    std::vector<Omikron::MaterialGroup> groups;
    CharacterGpuModel model;
    LitInstance lit;
    InstanceMatrices matrices;
  };

  /// Fog of the frame being rendered; blended groups are never fogged.
  Fog::ShaderFog m_frame_fog;
  /// Context ambient (+0x1A0/+0x1A4) of the frame being rendered.
  Lighting::Ambient m_frame_ambient;
  /// Explicit decor lights, prepared once like Runtime's scene load.
  std::vector<Lighting::LightSource> m_lights;
  /// Shared world programs, owned by WorldScene.
  WorldShaderSet* m_shaders{nullptr};
  /// Opaque/cutout program for lit characters in the frame being rendered.
  const Shader* m_character_shader{nullptr};
  /// Opaque/cutout program for decor in the frame being rendered.
  const Shader* m_decor_shader{nullptr};
  std::deque<Mesh> m_meshes;
  std::vector<std::int32_t> m_group_material_ids;
  std::vector<std::uint32_t> m_group_flags;
  std::vector<Omikron::BlendMode> m_group_modes;
  std::vector<std::array<float, 3>> m_group_centers;
  std::vector<GameColorTexture> m_textures;
  /// Source 3DO object of each decor group, parallel to m_meshes.
  std::vector<std::size_t> m_group_mesh_index;
  /// Per-object frustum spheres that follow the posed decor transforms.
  DecorCulling m_decor_culling;
  DecorCullStats m_decor_cull_stats;
  /// Opaque/cutout decor groups sorted by material to minimise texture binds.
  std::vector<std::size_t> m_opaque_draw_order;
  /// Per-frame scratch, retained to avoid reallocating every frame.
  std::vector<std::uint8_t> m_group_visible;
  std::vector<std::size_t> m_blended_groups;
  std::vector<std::uint8_t> m_changed_objects;
  std::vector<std::uint8_t> m_changed_mesh_mask;
  /// Redundant-bind tracking inside the world passes; reset by begin_world_pass().
  const Shader* m_bound_shader{nullptr};
  GLuint m_bound_texture{0};

  Sprite::SpriteRenderer m_sprite_renderer;
  ScenarioRuntime* m_last_sprite_runtime{nullptr};
  bool m_sprite_grayscale{false};
  bool m_geometry_wireframe{false};
  bool m_per_pixel_lighting{false};
  bool m_cubic_decor_colors{false};
  /// Every decor mesh carries the slope stream of the current layout.
  bool m_decor_color_slopes_ready{false};
  bool m_decor_color_slopes_failed{false};
  std::unordered_map<std::size_t, std::unique_ptr<CharacterGpuModel>> m_character_models;
  /// Parallel to the rendered crowd's prototypes; rebuilt when they change.
  std::deque<CrowdPrototypeGpu> m_crowd_prototypes;
  bool m_crowd_prototypes_failed{false};
  /// Parallel to the rendered crowd's passersby.
  std::deque<CrowdInstanceGpu> m_crowd_instances;
  std::size_t m_crowd_visible_count{0};
  std::uint8_t m_detail_level{1};
  const GameColorTexture* m_environment_texture{nullptr};
  GLuint m_bound_environment_texture{0};
  std::vector<std::string> m_failed_character_models;
  /// Placement instance id to its GPU model. Placements of the same model
  /// resource share one ObjectGpuModel (textures and buffers).
  std::unordered_map<std::size_t, std::shared_ptr<ObjectGpuModel>> m_object_models;
  std::unordered_map<const ObjectPlacement::ModelResource*, std::weak_ptr<ObjectGpuModel>>
      m_object_models_by_resource;
  std::unordered_set<std::size_t> m_live_object_ids;
  /// Placements that passed the frustum test this frame, with their transforms.
  struct VisibleObject {
    const ObjectPlacement::RuntimePlacement* placement{nullptr};
    InstanceMatrices matrices;
  };
  std::vector<VisibleObject> m_visible_objects;
  std::vector<std::string> m_failed_object_models;
  std::uint64_t m_decor_pose_revision{0};
  /// Decor object transforms the GPU geometry was last posed from. Only meshes
  /// whose transform differs are re-posed when the decor revision changes.
  std::vector<Omikron::Model3DOData::RuntimeObjectState> m_decor_posed_objects;
  /// (mesh, material, flags) group key to its index in m_meshes.
  std::map<std::tuple<std::size_t, std::int32_t, std::uint32_t>, std::size_t> m_decor_group_index;
  /// Per-draw uniform handles of one world program, resolved on first use so
  /// character and decor draws skip the uniform name lookup.
  struct WorldUniforms {
    Shader::UniformRef mvp;
    Shader::UniformRef model_view;
    Shader::UniformRef lighting_mode;
    Shader::UniformRef ambient_floor;
    Shader::UniformRef ambient_ceiling;
    Shader::UniformRef light_count;
    Shader::UniformRef uv_offset;
    Shader::UniformRef alpha_test;
    Shader::UniformRef premultiply_alpha;
    Shader::UniformRef far_fade_depth_scale;
    Shader::UniformRef fog_depth_scale;
    Shader::UniformRef environment_mapped;
    Shader::UniformRef environment_phase;
    std::array<Shader::UniformRef, Lighting::K_MAX_NODE_LIGHTS> light_direction;
    std::array<Shader::UniformRef, Lighting::K_MAX_NODE_LIGHTS> light_scale;
    std::array<Shader::UniformRef, Lighting::K_MAX_NODE_LIGHTS> light_color;
  };
  [[nodiscard]] const WorldUniforms& world_uniforms(const Shader& shader) const;
  /// Programs are owned by WorldShaderSet, which outlives this renderer, so the
  /// handles stay valid. A deque keeps returned references stable.
  mutable std::deque<std::pair<const Shader*, WorldUniforms>> m_world_uniforms;
  /// Per character of the rendered runtime (characters() order): nonzero
  /// when it is renderable and passed the lossless frustum test this frame.
  std::vector<std::uint8_t> m_character_visible;
  /// Pose-independent decor layout used to re-pose changed meshes.
  Omikron::PosedGeometryLayout m_decor_layout;
  /// Model `m_decor_layout` was built from.
  const Omikron::Model3DOData* m_decor_layout_model{nullptr};
  std::vector<Vertex> m_decor_pose_scratch;
  /// Last accepted posed-decor bounds, retained only for mutation diagnostics.
  WorldBounds m_decor_pose_bounds{};
  std::size_t m_decor_pose_vertex_count{0};

  WorldBounds m_bounds{};
};

}  // namespace App

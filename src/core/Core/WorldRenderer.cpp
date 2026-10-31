#include "Core/WorldRenderer.hpp"

#include <glad/glad.h>

// NOLINTBEGIN(misc-include-cleaner) -- GLM uses umbrella headers by design.
// Keep this suppression active for the translation unit, matching
// ModelViewerScene.cpp: clang-tidy cannot associate GLM's umbrella headers
// with the declarations used below without requiring implementation-detail
// GLM headers.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "Core/Camera.hpp"
#include "Core/Crowd/CrowdRuntime.hpp"
#include "Core/CubicVertexColor.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/DecorCulling.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Mesh.hpp"
#include "Core/Object/ObjectPlacementRuntime.hpp"
#include "Core/Omikron/Animation3DA.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/Texture3DT.hpp"
#include "Core/Reflection.hpp"
#include "Core/RuntimePresentation.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Shader.hpp"
#include "Core/Sprite/SpriteRenderer.hpp"
#include "Core/Texture.hpp"
#include "Core/Vertex.hpp"
#include "Core/ViewFrustum.hpp"
#include "Core/WorldColorPipeline.hpp"
#include "Core/WorldFog.hpp"
#include "Core/WorldLighting.hpp"
#include "Core/WorldShaderSet.hpp"

namespace App {

namespace {

constexpr std::array<GLfloat, 4> K_WIREFRAME_COLOR{0.15F, 0.95F, 1.0F, 0.9F};

/// CPU-side integrity summary for one posed decor rebuild. Keep it beside the
/// renderer boundary so an invalid gameplay pose never reaches GL buffers.
struct DecorPoseGeometry {
  WorldBounds bounds;
  std::size_t vertex_count{0};
};

[[nodiscard]] bool same_world_transform(const Omikron::Model3DOData::RuntimeObjectState& lhs,
    const Omikron::Model3DOData::RuntimeObjectState& rhs) {
  return lhs.world_matrix.values == rhs.world_matrix.values &&
         lhs.world_translation.x == rhs.world_translation.x &&
         lhs.world_translation.y == rhs.world_translation.y &&
         lhs.world_translation.z == rhs.world_translation.z && lhs.scale.x == rhs.scale.x &&
         lhs.scale.y == rhs.scale.y && lhs.scale.z == rhs.scale.z;
}

[[nodiscard]] std::optional<DecorPoseGeometry> describe_decor_pose(
    const std::span<const Omikron::MaterialGroup> groups) {
  if (groups.empty()) {
    return std::nullopt;
  }
  std::array<float, 3> minimum{std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max()};
  std::array<float, 3> maximum{std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest()};
  std::size_t vertex_count{0};
  for (const Omikron::MaterialGroup& group : groups) {
    for (const Vertex& native_vertex : group.vertices) {
      const Vertex vertex{Runtime::Presentation::to_gl(native_vertex)};
      for (const float coordinate : vertex.position) {
        if (!std::isfinite(coordinate)) {
          return std::nullopt;
        }
      }
      for (std::size_t axis{0}; axis < minimum.size(); ++axis) {
        minimum.at(axis) = std::min(minimum.at(axis), vertex.position.at(axis));
        maximum.at(axis) = std::max(maximum.at(axis), vertex.position.at(axis));
      }
      ++vertex_count;
    }
  }
  if (vertex_count == 0U) {
    return std::nullopt;
  }
  WorldBounds bounds;
  for (std::size_t axis{0}; axis < bounds.center.size(); ++axis) {
    bounds.center.at(axis) = (minimum.at(axis) + maximum.at(axis)) * 0.5F;
  }
  const float extent_x{maximum.at(0) - minimum.at(0)};
  const float extent_y{maximum.at(1) - minimum.at(1)};
  const float extent_z{maximum.at(2) - minimum.at(2)};
  bounds.radius = std::max(
      0.5F * std::sqrt((extent_x * extent_x) + (extent_y * extent_y) + (extent_z * extent_z)),
      1.0F);
  return DecorPoseGeometry{.bounds = bounds, .vertex_count = vertex_count};
}

/// Bucket 0x0800 (3DO flag bit 16) doubles the Runtime fog range.
[[nodiscard]] float fog_depth_scale(const std::uint32_t flags) {
  return Omikron::has_flag(flags, Omikron::MeshFlags::k_doubled_fog_range)
             ? Fog::K_DOUBLED_RANGE_DEPTH_SCALE
             : 1.0F;
}

bool is_blended(const Omikron::BlendMode mode) {
  return mode == Omikron::BlendMode::k_alpha_blend || mode == Omikron::BlendMode::k_additive ||
         mode == Omikron::BlendMode::k_subtractive;
}

[[nodiscard]] LegacyBlendOperator legacy_operator(const Omikron::BlendMode mode) {
  switch (mode) {
    case Omikron::BlendMode::k_additive:
      return LegacyBlendOperator::k_additive;
    case Omikron::BlendMode::k_subtractive:
      return LegacyBlendOperator::k_subtractive;
    case Omikron::BlendMode::k_alpha_blend:
    default:
      return LegacyBlendOperator::k_alpha;
  }
}

[[nodiscard]] LegacyBlendOperator sprite_legacy_operator(const std::uint16_t bucket) {
  if ((bucket & 0x0300U) == 0x0100U) {
    return LegacyBlendOperator::k_additive;
  }
  if ((bucket & 0x0300U) == 0x0200U) {
    return LegacyBlendOperator::k_darken;
  }
  return LegacyBlendOperator::k_alpha;
}

/// Sphere around every presentation-space vertex of an object model.
[[nodiscard]] std::optional<BoundingSphere> model_bounds(
    const std::span<const Omikron::MaterialGroup> groups) {
  glm::vec3 minimum{std::numeric_limits<float>::max()};
  glm::vec3 maximum{std::numeric_limits<float>::lowest()};
  bool any{false};
  for (const Omikron::MaterialGroup& group : groups) {
    for (const Vertex& native_vertex : group.vertices) {
      const glm::vec3 position{
          glm::make_vec3(Runtime::Presentation::to_gl(native_vertex).position.data())};
      minimum = glm::min(minimum, position);
      maximum = glm::max(maximum, position);
      any = true;
    }
  }
  if (!any) {
    return std::nullopt;
  }
  const glm::vec3 center{(minimum + maximum) * 0.5F};
  float radius_squared{0.0F};
  for (const Omikron::MaterialGroup& group : groups) {
    for (const Vertex& native_vertex : group.vertices) {
      const glm::vec3 offset{
          glm::make_vec3(Runtime::Presentation::to_gl(native_vertex).position.data()) - center};
      radius_squared = std::max(radius_squared, glm::dot(offset, offset));
    }
  }
  const float radius{std::sqrt(radius_squared)};
  BoundingSphere sphere{.radius = radius};
  std::copy_n(glm::value_ptr(center), sphere.center.size(), sphere.center.begin());
  if (!std::isfinite(radius) || !std::ranges::all_of(sphere.center, [](const float value) {
        return std::isfinite(value);
      })) {
    return std::nullopt;
  }
  return sphere;
}

/// Lossless frustum test for one placed model; unbounded models are always drawn.
[[nodiscard]] bool instance_visible(const std::optional<BoundingSphere>& local_bounds,
    const glm::mat4& model,
    const ViewFrustum& frustum) {
  if (!local_bounds.has_value()) {
    return true;
  }
  const glm::vec4 center{model * glm::vec4{glm::make_vec3(local_bounds->center.data()), 1.0F}};
  const float scale{std::max({glm::length(glm::vec3{model[0]}),
      glm::length(glm::vec3{model[1]}),
      glm::length(glm::vec3{model[2]})})};
  BoundingSphere sphere{.radius = local_bounds->radius * scale};
  std::copy_n(glm::value_ptr(center), sphere.center.size(), sphere.center.begin());
  if (!std::isfinite(sphere.radius) || !std::ranges::all_of(sphere.center, [](const float value) {
        return std::isfinite(value);
      })) {
    return true;
  }
  return frustum.intersects(sphere);
}

/// Frobenius norm: an upper bound of how far a matrix can stretch a vector.
[[nodiscard]] float stretch_bound(const Runtime::Matrix3& matrix) {
  float sum{0.0F};
  for (const float value : matrix.values) {
    sum += value * value;
  }
  return std::sqrt(sum);
}

[[nodiscard]] float max_abs_scale(const Runtime::Vec3& scale) {
  return std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
}

/// Lossless frustum test for one character. Every posed corner lies within
/// reach * |object scale| * |object matrix| * |presentation| of its owning
/// object's presented origin, so a character is skipped only when all of
/// those spheres are outside the view volume. Dialogue face overrides replace
/// source vertices, so overlaid characters are never culled.
[[nodiscard]] bool character_visible(
    const Character::RuntimeCharacter& character, const ViewFrustum& frustum) {
  if (character.model_resource == nullptr || character.dialog_performance.has_value()) {
    return true;
  }
  const std::vector<float>& reach{character.model_resource->object_vertex_reach};
  if (reach.size() != character.runtime_objects.size()) {
    return true;
  }
  const Runtime::Transform presentation{character.presentation_transform()};
  const float presentation_stretch{
      stretch_bound(presentation.matrix) * max_abs_scale(presentation.scale)};
  for (std::size_t index{0}; index < reach.size(); ++index) {
    if (!(reach.at(index) > 0.0F)) {
      continue;
    }
    const Omikron::Model3DOData::RuntimeObjectState& object{character.runtime_objects.at(index)};
    const Runtime::Vec3 center{Runtime::Presentation::to_gl(
        Runtime::transform_point(object.world_translation, presentation))};
    const BoundingSphere sphere{.center = {center.x, center.y, center.z},
        .radius = reach.at(index) * max_abs_scale(object.scale) *
                  stretch_bound(object.world_matrix) * presentation_stretch};
    if (!std::isfinite(sphere.radius) || !std::ranges::all_of(sphere.center, [](const float value) {
          return std::isfinite(value);
        })) {
      return true;
    }
    if (frustum.intersects(sphere)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] GameColorTextureUsage texture_usage(const unsigned char bits) {
  if (bits == 2U) {
    return GameColorTextureUsage::k_legacy_effect;
  }
  if (bits == 3U) {
    return GameColorTextureUsage::k_both;
  }
  return GameColorTextureUsage::k_modern;
}

std::expected<std::vector<GameColorTexture>, std::string> make_white_textures(
    const std::vector<GameColorTextureUsage>& usages) {
  static constexpr std::array<std::uint8_t, 4> k_white_pixel{255, 255, 255, 255};
  std::vector<GameColorTexture> textures;
  textures.reserve(usages.size());
  for (const GameColorTextureUsage usage : usages) {
    auto texture{
        GameColorTexture::create(1, 1, std::span<const std::uint8_t>{k_white_pixel}, usage)};
    if (!texture) {
      return std::expected<std::vector<GameColorTexture>, std::string>{
          std::unexpect, std::move(texture).error()};
    }
    textures.push_back(std::move(texture).value());
  }
  return textures;
}

std::expected<std::vector<GameColorTexture>, std::string> load_decor_textures(
    const WorldSceneContext& context,
    const Omikron::Model3DOData& model,
    const std::vector<Omikron::MaterialGroup>& groups) {
  std::vector<unsigned char> usage_bits(model.materials.size(), 0U);
  for (const Omikron::MaterialGroup& group : groups) {
    if (group.material_id < 0 || static_cast<std::size_t>(group.material_id) >= usage_bits.size()) {
      continue;
    }
    const unsigned char bit{
        static_cast<unsigned char>(is_blended(Omikron::blend_mode(group.flags)) ? 2U : 1U)};
    usage_bits.at(static_cast<std::size_t>(group.material_id)) |= bit;
  }
  std::vector<GameColorTextureUsage> usages;
  usages.reserve(usage_bits.size());
  for (const unsigned char bits : usage_bits) {
    usages.push_back(texture_usage(bits));
  }
  if (!context.decor_path.has_value()) {
    App::Log::warn(LogCategory::Renderer,
        "World decor has no source path; rendering {} materials with white fallback textures",
        model.materials.size());
    return make_white_textures(usages);
  }

  std::filesystem::path texture_path{context.decor_path.value()};
  texture_path.replace_extension(".3dt");
  auto file{load_game_file(texture_path)};
  if (!file) {
    App::Log::warn(LogCategory::Renderer,
        "World decor texture '{}' unavailable: {}; using white fallbacks",
        texture_path.string(),
        file.error());
    return make_white_textures(usages);
  }

  auto images{Omikron::Texture3DT::load(std::span<const std::byte>{file->bytes}, model.materials)};
  if (!images) {
    App::Log::warn(LogCategory::Renderer,
        "World decor texture '{}' failed to decode: {}; using white fallbacks",
        texture_path.string(),
        images.error());
    return make_white_textures(usages);
  }

  std::vector<GameColorTexture> textures;
  textures.reserve(images->size());
  for (const Omikron::Texture3DTImage& image : images.value()) {
    if (image.width == 0U || image.height == 0U) {
      auto fallback{
          make_white_textures(std::vector<GameColorTextureUsage>{usages.at(textures.size())})};
      if (!fallback) {
        return fallback;
      }
      textures.push_back(std::move(fallback->front()));
      continue;
    }
    auto texture{GameColorTexture::create(static_cast<int>(image.width),
        static_cast<int>(image.height),
        std::span<const std::uint8_t>{image.rgba8},
        usages.at(textures.size()))};
    if (!texture) {
      return std::expected<std::vector<GameColorTexture>, std::string>{
          std::unexpect, std::move(texture).error()};
    }
    textures.push_back(std::move(texture).value());
  }
  return textures;
}

}  // namespace

std::expected<std::unique_ptr<WorldRenderer>, std::string> WorldRenderer::create(
    WorldSceneContext& context, WorldShaderSet& shaders) {
  APP_PROFILE_FUNCTION();

  if (!context.decor_model.has_value()) {
    return std::expected<std::unique_ptr<WorldRenderer>, std::string>{
        std::unexpect, "active world context has no decoded decor model"};
  }

  const Omikron::Model3DOData& model{context.decor_model.value()};
  // The layout is resolved once and kept for later decor pose patches.
  auto layout{Omikron::Model3DO::build_posed_layout(model)};
  if (!layout) {
    return std::expected<std::unique_ptr<WorldRenderer>, std::string>{
        std::unexpect, std::move(layout).error()};
  }
  std::expected<std::vector<Omikron::MaterialGroup>, std::string> groups{};
  if (auto posed{Omikron::Model3DO::apply_pose(model,
          layout.value(),
          context.runtime != nullptr && context.runtime->decor_model() == &model
              ? context.runtime->decor_runtime_objects()
              : std::span<const Omikron::Model3DOData::RuntimeObjectState>{model.runtime_objects},
          std::span<const Omikron::RawVertex>{model.vertices},
          groups.value())};
      !posed) {
    return std::expected<std::unique_ptr<WorldRenderer>, std::string>{
        std::unexpect, std::move(posed).error()};
  }

  auto textures{load_decor_textures(context, model, groups.value())};
  if (!textures) {
    return std::expected<std::unique_ptr<WorldRenderer>, std::string>{
        std::unexpect, std::move(textures).error()};
  }

  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) -- private constructor.
  auto renderer{std::unique_ptr<WorldRenderer>{new WorldRenderer()}};
  renderer->m_shaders = &shaders;
  renderer->m_textures = std::move(textures).value();
  renderer->m_lights = Lighting::prepare_all(model);
  renderer->m_decor_layout = std::move(layout).value();
  renderer->m_decor_layout_model = &model;
  renderer->m_decor_pose_revision =
      context.runtime != nullptr ? context.runtime->decor_pose_revision() : 0U;

  std::array<float, 3> bounds_min{std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max()};
  std::array<float, 3> bounds_max{std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest()};
  bool has_vertices{false};
  bool special_flags_seen{false};

  for (const Omikron::MaterialGroup& group : groups.value()) {
    std::vector<Vertex> presentation_vertices;
    presentation_vertices.reserve(group.vertices.size());
    for (const Vertex& vertex : group.vertices) {
      presentation_vertices.push_back(Runtime::Presentation::to_gl(vertex));
    }
    renderer->m_meshes.emplace_back(presentation_vertices, group.indices);
    renderer->m_group_material_ids.push_back(group.material_id);
    renderer->m_group_flags.push_back(group.flags);
    renderer->m_group_modes.push_back(Omikron::blend_mode(group.flags));

    std::array<float, 3> group_min{std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    std::array<float, 3> group_max{std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()};

    for (const Vertex& vertex : presentation_vertices) {
      has_vertices = true;
      for (std::size_t axis{0}; axis < 3U; ++axis) {
        group_min.at(axis) = std::min(group_min.at(axis), vertex.position.at(axis));
        group_max.at(axis) = std::max(group_max.at(axis), vertex.position.at(axis));
        bounds_min.at(axis) = std::min(bounds_min.at(axis), vertex.position.at(axis));
        bounds_max.at(axis) = std::max(bounds_max.at(axis), vertex.position.at(axis));
      }
    }
    std::array<float, 3> center{};
    if (!group.vertices.empty()) {
      for (std::size_t axis{0}; axis < 3U; ++axis) {
        center.at(axis) = (group_min.at(axis) + group_max.at(axis)) * 0.5F;
      }
    }
    renderer->m_group_centers.push_back(center);

    special_flags_seen =
        special_flags_seen || Omikron::has_flag(group.flags, Omikron::MeshFlags::k_mirror);
  }

  if (has_vertices) {
    for (std::size_t axis{0}; axis < 3U; ++axis) {
      renderer->m_bounds.center.at(axis) = (bounds_min.at(axis) + bounds_max.at(axis)) * 0.5F;
    }
    const float extent_x{bounds_max.at(0) - bounds_min.at(0)};
    const float extent_y{bounds_max.at(1) - bounds_min.at(1)};
    const float extent_z{bounds_max.at(2) - bounds_min.at(2)};
    renderer->m_bounds.radius = std::max(
        0.5F * std::sqrt((extent_x * extent_x) + (extent_y * extent_y) + (extent_z * extent_z)),
        1.0F);
  }
  renderer->rebuild_opaque_draw_order();
  renderer->index_decor_groups(model,
      groups.value(),
      context.runtime != nullptr && context.runtime->decor_model() == &model
          ? context.runtime->decor_runtime_objects()
          : std::span<const Omikron::Model3DOData::RuntimeObjectState>{model.runtime_objects});
  if (const auto pose_geometry{describe_decor_pose(groups.value())}; pose_geometry.has_value()) {
    renderer->m_decor_pose_bounds = pose_geometry->bounds;
    renderer->m_decor_pose_vertex_count = pose_geometry->vertex_count;
  } else {
    App::Log::warn(LogCategory::Renderer,
        "World decor initial geometry has no finite vertices; pose rebuilds will be rejected");
  }

  if (special_flags_seen) {
    App::Log::debug(LogCategory::Renderer,
        "WorldRenderer: mirror flags currently use the base textured pass; the specialized "
        "ModelViewer mirror pass remains to be extracted into the shared renderer");
  }

  if (context.runtime != nullptr) {
    // Scenario state remains Runtime-native; the bounds above are GL-facing.
    context.runtime->set_world_anchor(Runtime::Presentation::to_gl(renderer->m_bounds.center));
  }
  if (auto initialized{renderer->m_sprite_renderer.initialize()}; !initialized) {
    App::Log::warn(
        LogCategory::Renderer, "World sprite renderer unavailable: {}", initialized.error());
  }

  App::Log::info(LogCategory::Renderer,
      "World renderer ready — scene={} generation={} groups={} materials={}",
      context.scene_id,
      context.generation,
      renderer->m_meshes.size(),
      renderer->m_textures.size());
  return renderer;
}

std::size_t WorldRenderer::mirror_group_count() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(m_group_flags, [](const std::uint32_t flags) {
        return Omikron::has_flag(flags, Omikron::MeshFlags::k_mirror);
      }));
}

std::size_t WorldRenderer::uv_scroll_u_group_count() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(m_group_flags, [](const std::uint32_t flags) {
        return Omikron::has_flag(flags, Omikron::MeshFlags::k_uv_scroll_u);
      }));
}

std::size_t WorldRenderer::uv_scroll_v_group_count() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(m_group_flags, [](const std::uint32_t flags) {
        return Omikron::has_flag(flags, Omikron::MeshFlags::k_uv_scroll_v);
      }));
}

std::size_t WorldRenderer::environment_group_count() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(m_group_flags, [](const std::uint32_t flags) {
        return Omikron::has_flag(flags, Omikron::MeshFlags::k_environment_mapped);
      }));
}

Debug::SpriteRenderDebugState WorldRenderer::sprite_render_debug_state() const {
  return Debug::make_sprite_render_debug_state(
      m_last_sprite_runtime, m_sprite_renderer.queue_stats(), m_sprite_renderer.commands());
}

void WorldRenderer::set_sprite_grayscale(const bool enabled) {
  m_sprite_renderer.set_grayscale(enabled);
  m_sprite_grayscale = enabled;
}

void WorldRenderer::index_decor_groups(const Omikron::Model3DOData& model,
    const std::span<const Omikron::MaterialGroup> groups,
    const std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects) {
  m_decor_group_index.clear();
  m_group_mesh_index.clear();
  m_group_mesh_index.reserve(groups.size());
  for (const Omikron::MaterialGroup& group : groups) {
    m_group_mesh_index.push_back(group.mesh_index);
  }
  m_decor_culling.rebuild(model, runtime_objects);
  for (std::size_t index{0}; index < groups.size(); ++index) {
    const Omikron::MaterialGroup& group{groups.subspan(index, 1U).front()};
    m_decor_group_index.emplace(
        std::tuple{group.mesh_index, group.material_id, group.flags}, index);
  }
  m_decor_posed_objects.assign(runtime_objects.begin(), runtime_objects.end());
}

bool WorldRenderer::patch_decor_model(const ScenarioRuntime& runtime) {
  APP_PROFILE_FUNCTION();

  // Scripted decor motion (MoveObjectOnPath vehicles, doors, ...) bumps the
  // pose revision every frame while touching only a handful of objects.
  // Re-posing and re-uploading the whole decor for that stalls large scenes
  // such as ANEKBAH (1600+ groups), so only the affected meshes are rebuilt.
  const Omikron::Model3DOData& model{*runtime.decor_model()};
  const auto objects{runtime.decor_runtime_objects()};
  if (objects.size() != m_decor_posed_objects.size() ||
      model.skin_parent_index.size() != objects.size()) {
    return false;
  }
  std::vector<std::uint8_t>& changed{m_changed_objects};
  changed.assign(objects.size(), 0U);
  bool any_changed{false};
  for (std::size_t index{0}; index < objects.size(); ++index) {
    if (!same_world_transform(
            objects.subspan(index, 1U).front(), m_decor_posed_objects.at(index))) {
      changed.at(index) = 1U;
      any_changed = true;
    }
  }
  if (!any_changed) {
    return true;
  }
  // Skinned corners resolve against the skin parent's transform.
  std::vector<std::uint8_t>& mesh_mask{m_changed_mesh_mask};
  mesh_mask.assign(changed.begin(), changed.end());
  for (std::size_t index{0}; index < objects.size(); ++index) {
    const std::int32_t skin_parent{model.skin_parent_index.at(index)};
    if (skin_parent >= 0 && changed.at(static_cast<std::size_t>(skin_parent)) != 0U) {
      mesh_mask.at(index) = 1U;
    }
  }

  // The decor's corner layout is pose-independent; resolve it once and only
  // re-pose the groups of changed meshes.
  if (m_decor_layout_model != &model) {
    auto layout{Omikron::Model3DO::build_posed_layout(model)};
    if (!layout) {
      App::Log::warn(LogCategory::Renderer, "World decor pose layout failed: {}", layout.error());
      return true;
    }
    m_decor_layout = std::move(layout).value();
    m_decor_layout_model = &model;
    m_decor_color_slopes_ready = false;
    m_decor_color_slopes_failed = false;
  }

  std::vector<std::pair<std::size_t, std::vector<Vertex>>> updates;
  for (std::size_t group_index{0}; group_index < m_decor_layout.groups.size(); ++group_index) {
    const Omikron::MaterialGroup& group{m_decor_layout.groups.at(group_index)};
    if (group.mesh_index >= mesh_mask.size() || mesh_mask.at(group.mesh_index) == 0U) {
      continue;
    }
    const auto found{
        m_decor_group_index.find(std::tuple{group.mesh_index, group.material_id, group.flags})};
    if (found == m_decor_group_index.end() ||
        m_meshes.at(found->second).vertex_count() != group.vertices.size()) {
      return false;
    }
    if (auto posed{Omikron::Model3DO::pose_group(model,
            m_decor_layout,
            group_index,
            objects,
            std::span<const Omikron::RawVertex>{model.vertices},
            m_decor_pose_scratch)};
        !posed) {
      App::Log::warn(LogCategory::Renderer, "World decor pose patch failed: {}", posed.error());
      return true;
    }
    std::vector<Vertex> presentation_vertices;
    presentation_vertices.reserve(m_decor_pose_scratch.size());
    for (const Vertex& vertex : m_decor_pose_scratch) {
      const Vertex presentation{Runtime::Presentation::to_gl(vertex)};
      if (!std::ranges::all_of(presentation.position, [](const float coordinate) {
            return std::isfinite(coordinate);
          })) {
        App::Log::warn(LogCategory::Renderer,
            "World decor pose patch produced non-finite geometry for mesh {}; preserving prior "
            "GPU geometry",
            group.mesh_index);
        return true;
      }
      presentation_vertices.push_back(presentation);
    }
    updates.emplace_back(found->second, std::move(presentation_vertices));
  }
  for (const auto& [index, vertices] : updates) {
    static_cast<void>(m_meshes.at(index).update_vertices(vertices));
  }
  for (std::size_t index{0}; index < objects.size(); ++index) {
    if (changed.at(index) != 0U) {
      m_decor_posed_objects.at(index) = objects.subspan(index, 1U).front();
    }
  }
  // A sphere depends only on its own object's transform (skinned objects stay unbounded).
  m_decor_culling.refresh(model, objects, changed);
  return true;
}

void WorldRenderer::sync_decor_model(const ScenarioRuntime& runtime) {
  if (runtime.decor_model() == nullptr || runtime.decor_pose_revision() == m_decor_pose_revision) {
    return;
  }
  if (patch_decor_model(runtime)) {
    m_decor_pose_revision = runtime.decor_pose_revision();
    return;
  }
  auto groups{Omikron::Model3DO::build_posed_geometry(
      *runtime.decor_model(), runtime.decor_runtime_objects())};
  if (!groups) {
    App::Log::warn(LogCategory::Renderer, "World decor pose rebuild failed: {}", groups.error());
    return;
  }
  if (groups->size() != m_meshes.size()) {
    App::Log::warn(LogCategory::Renderer,
        "World decor pose rebuild changed group count from {} to {}; preserving prior GPU geometry",
        m_meshes.size(),
        groups->size());
    return;
  }
  const auto geometry{describe_decor_pose(groups.value())};
  if (!geometry.has_value()) {
    App::Log::warn(LogCategory::Renderer,
        "World decor pose rebuild produced empty or non-finite geometry; preserving prior GPU "
        "geometry");
    return;
  }
  if (m_decor_pose_vertex_count != 0U) {
    App::Log::debug(LogCategory::Renderer,
        "World decor pose rebuild — groups={} vertices={} bounds center=({}, {}, {}) radius={} "
        "(previous center=({}, {}, {}) radius={})",
        groups->size(),
        geometry->vertex_count,
        geometry->bounds.center.at(0),
        geometry->bounds.center.at(1),
        geometry->bounds.center.at(2),
        geometry->bounds.radius,
        m_decor_pose_bounds.center.at(0),
        m_decor_pose_bounds.center.at(1),
        m_decor_pose_bounds.center.at(2),
        m_decor_pose_bounds.radius);
  }
  m_meshes.clear();
  for (const Omikron::MaterialGroup& group : groups.value()) {
    std::vector<Vertex> presentation_vertices;
    presentation_vertices.reserve(group.vertices.size());
    for (const Vertex& vertex : group.vertices) {
      presentation_vertices.push_back(Runtime::Presentation::to_gl(vertex));
    }
    m_meshes.emplace_back(presentation_vertices, group.indices);
  }
  m_decor_color_slopes_ready = false;
  m_decor_color_slopes_failed = false;
  index_decor_groups(*runtime.decor_model(), groups.value(), runtime.decor_runtime_objects());
  rebuild_opaque_draw_order();
  m_decor_pose_bounds = geometry->bounds;
  m_decor_pose_vertex_count = geometry->vertex_count;
  m_decor_pose_revision = runtime.decor_pose_revision();
}

void WorldRenderer::begin_world_pass(
    const Shader& shader, const glm::mat4& mvp, const glm::mat4& view, const bool legacy_effect) {
  shader.bind();
  m_bound_shader = &shader;
  m_bound_texture = 0;
  m_bound_environment_texture = 0;
  shader.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
  shader.set_uniform_mat4("u_model_view", std::span<const GLfloat, 16>{glm::value_ptr(view), 16});
  apply_baked_lighting(shader);
  shader.set_uniform_int("u_texture0", 0);
  shader.set_uniform_int("u_texture1", 1);
  if (legacy_effect) {
    Fog::apply_far_fade_uniforms(shader, m_frame_fog);
  } else {
    Fog::apply_uniforms(shader, m_frame_fog);
  }
}

void WorldRenderer::bind_world_shader(const Shader& shader) {
  if (m_bound_shader != &shader) {
    shader.bind();
    m_bound_shader = &shader;
  }
}

void WorldRenderer::bind_world_texture(const Texture2D& texture) {
  if (m_bound_texture != texture.id()) {
    texture.bind(0);
    m_bound_texture = texture.id();
  }
}

const WorldRenderer::WorldUniforms& WorldRenderer::world_uniforms(const Shader& shader) const {
  for (const auto& [program, uniforms] : m_world_uniforms) {
    if (program == &shader) {
      return uniforms;
    }
  }
  WorldUniforms uniforms{.mvp = shader.uniform("u_mvp"),
      .model_view = shader.uniform("u_model_view"),
      .lighting_mode = shader.uniform("u_lighting_mode"),
      .ambient_floor = shader.uniform("u_ambient_floor"),
      .ambient_ceiling = shader.uniform("u_ambient_ceiling"),
      .light_count = shader.uniform("u_light_count"),
      .uv_offset = shader.uniform("u_uv_offset"),
      .alpha_test = shader.uniform("u_alpha_test"),
      .premultiply_alpha = shader.uniform("u_premultiply_alpha"),
      .far_fade_depth_scale = shader.uniform("u_far_fade_depth_scale"),
      .fog_depth_scale = shader.uniform("u_fog_depth_scale"),
      .environment_mapped = shader.uniform("u_environment_mapped"),
      .environment_phase = shader.uniform("u_environment_phase"),
      .light_direction = {},
      .light_scale = {},
      .light_color = {}};
  for (std::size_t index{0}; index < Lighting::K_MAX_NODE_LIGHTS; ++index) {
    const std::string suffix{"[" + std::to_string(index) + "]"};
    uniforms.light_direction.at(index) = shader.uniform("u_light_direction" + suffix);
    uniforms.light_scale.at(index) = shader.uniform("u_light_scale" + suffix);
    uniforms.light_color.at(index) = shader.uniform("u_light_color" + suffix);
  }
  return m_world_uniforms.emplace_back(&shader, uniforms).second;
}

void WorldRenderer::apply_group_uniforms(const Shader& shader,
    const std::uint32_t flags,
    const Omikron::BlendMode mode,
    const float uv_phase_u,
    const float uv_phase_v,
    const bool legacy_effect) {
  const WorldUniforms& uniforms{world_uniforms(shader)};
  const std::array<float, 2> uv_offset{Omikron::uv_scroll_offset(flags, uv_phase_u, uv_phase_v)};
  shader.set_uniform_vec2(uniforms.uv_offset, std::span<const GLfloat, 2>{uv_offset});
  shader.set_uniform_float(
      uniforms.alpha_test, mode == Omikron::BlendMode::k_alpha_test ? 1.0F : 0.0F);
  if (legacy_effect) {
    shader.set_uniform_float(
        uniforms.premultiply_alpha, mode == Omikron::BlendMode::k_alpha_blend ? 1.0F : 0.0F);
    shader.set_uniform_float(uniforms.far_fade_depth_scale, fog_depth_scale(flags));
  } else {
    shader.set_uniform_float(uniforms.fog_depth_scale, fog_depth_scale(flags));
  }

  const bool environment{environment_mapping_active(flags)};
  shader.set_uniform_int(uniforms.environment_mapped, environment ? 1 : 0);
  if (!environment) {
    return;
  }
  const std::array<float, 2> phase{uv_phase_u, uv_phase_v};
  shader.set_uniform_vec2(uniforms.environment_phase, std::span<const GLfloat, 2>{phase});
  const Texture2D* const texture{
      legacy_effect ? m_environment_texture->legacy_effect() : m_environment_texture->modern()};
  if (texture != nullptr && m_bound_environment_texture != texture->id()) {
    texture->bind(1);
    m_bound_environment_texture = texture->id();
    // Keep unit 0 current for the base texture binds that follow.
    glActiveTexture(GL_TEXTURE0);
  }
}

bool WorldRenderer::environment_mapping_active(const std::uint32_t flags) const {
  return m_environment_texture != nullptr && m_detail_level >= 1U &&
         Omikron::has_flag(flags, Omikron::MeshFlags::k_environment_mapped);
}

void WorldRenderer::draw_group(const std::size_t index,
    const float uv_phase_u,
    const float uv_phase_v,
    const bool legacy_effect) {
  // begin_world_pass() has already written the decor transforms, baked
  // lighting and frame fog on this shader.
  const Shader& shader{legacy_effect ? m_shaders->legacy() : *m_decor_shader};
  bind_world_shader(shader);
  apply_group_uniforms(shader,
      m_group_flags.at(index),
      m_group_modes.at(index),
      uv_phase_u,
      uv_phase_v,
      legacy_effect);

  const std::size_t material{static_cast<std::size_t>(m_group_material_ids.at(index))};
  const Texture2D* texture{
      legacy_effect ? m_textures.at(material).legacy_effect() : m_textures.at(material).modern()};
  if (texture == nullptr) {
    return;
  }
  bind_world_texture(*texture);
  m_meshes.at(index).draw();
}

void WorldRenderer::rebuild_opaque_draw_order() {
  m_opaque_draw_order.clear();
  for (std::size_t index{0}; index < m_meshes.size() && index < m_group_modes.size(); ++index) {
    if (!is_blended(m_group_modes.at(index))) {
      m_opaque_draw_order.push_back(index);
    }
  }
  // Depth-tested opaque/cutout draws are order-independent; grouping by
  // material lets consecutive draws reuse the bound texture.
  std::ranges::stable_sort(
      m_opaque_draw_order, [this](const std::size_t lhs, const std::size_t rhs) {
        return m_group_material_ids.at(lhs) < m_group_material_ids.at(rhs);
      });
}

bool WorldRenderer::ensure_decor_color_slopes() {
  if (m_decor_color_slopes_ready) {
    return true;
  }
  if (m_decor_color_slopes_failed || m_decor_layout_model == nullptr) {
    return false;
  }
  APP_PROFILE_FUNCTION();

  // Slope ratios are pose-invariant, so pose patches never rebuild them.
  const std::vector<std::vector<VertexColorSlopes>> slopes{
      CubicColor::build_corner_slopes(*m_decor_layout_model, m_decor_layout)};
  for (std::size_t group_index{0}; group_index < m_decor_layout.groups.size(); ++group_index) {
    const Omikron::MaterialGroup& group{m_decor_layout.groups.at(group_index)};
    const auto found{
        m_decor_group_index.find(std::tuple{group.mesh_index, group.material_id, group.flags})};
    if (found == m_decor_group_index.end() ||
        !m_meshes.at(found->second).set_color_slopes(slopes.at(group_index))) {
      m_decor_color_slopes_failed = true;
      break;
    }
  }
  if (!m_decor_color_slopes_failed && !std::ranges::all_of(m_meshes, [](const Mesh& mesh) {
        return mesh.has_color_slopes();
      })) {
    m_decor_color_slopes_failed = true;
  }
  if (m_decor_color_slopes_failed) {
    App::Log::warn(LogCategory::Renderer,
        "World decor layout does not match its meshes; keeping linear decor colours");
    return false;
  }
  m_decor_color_slopes_ready = true;
  return true;
}

bool WorldRenderer::update_character_meshes(
    CharacterGpuModel& gpu, const std::span<const Omikron::MaterialGroup> groups) {
  if (gpu.meshes.size() != groups.size()) {
    return false;
  }
  for (std::size_t index{0}; index < groups.size(); ++index) {
    const Omikron::MaterialGroup& group{groups.subspan(index, 1U).front()};
    if (gpu.meshes.at(index).vertex_count() != group.vertices.size() ||
        gpu.group_material_ids.at(index) != group.material_id ||
        gpu.group_flags.at(index) != group.flags) {
      return false;
    }
  }
  std::vector<Vertex> presentation_vertices;
  for (std::size_t index{0}; index < groups.size(); ++index) {
    const Omikron::MaterialGroup& group{groups.subspan(index, 1U).front()};
    presentation_vertices.clear();
    presentation_vertices.reserve(group.vertices.size());
    for (const Vertex& vertex : group.vertices) {
      presentation_vertices.push_back(Runtime::Presentation::to_gl(vertex));
    }
    static_cast<void>(gpu.meshes.at(index).update_vertices(presentation_vertices));
  }
  return true;
}

void WorldRenderer::rebuild_character_meshes(
    CharacterGpuModel& gpu, const std::span<const Omikron::MaterialGroup> groups) {
  gpu.meshes.clear();
  gpu.group_material_ids.clear();
  gpu.group_flags.clear();
  gpu.group_modes.clear();
  std::vector<Vertex> presentation_vertices;
  for (const Omikron::MaterialGroup& group : groups) {
    presentation_vertices.clear();
    presentation_vertices.reserve(group.vertices.size());
    for (const Vertex& vertex : group.vertices) {
      presentation_vertices.push_back(Runtime::Presentation::to_gl(vertex));
    }
    gpu.meshes.emplace_back(presentation_vertices, group.indices);
    gpu.group_material_ids.push_back(group.material_id);
    gpu.group_flags.push_back(group.flags);
    gpu.group_modes.push_back(Omikron::blend_mode(group.flags));
  }
}

bool WorldRenderer::character_presented(const std::size_t character_index) const {
  return character_index < m_character_visible.size() &&
         m_character_visible.at(character_index) != 0U;
}

void WorldRenderer::sync_character_models(ScenarioRuntime& runtime) {
  const std::span<const Character::RuntimeCharacter> characters{
      runtime.character_runtime().characters()};
  for (std::size_t character_index{0}; character_index < characters.size(); ++character_index) {
    const Character::RuntimeCharacter& character{characters.subspan(character_index, 1U).front()};
    // Off-screen characters are neither uploaded nor posed this frame.
    if (!character_presented(character_index) || character.model_resource == nullptr ||
        std::ranges::contains(m_failed_character_models, character.model_resource_name)) {
      continue;
    }

    auto found{m_character_models.find(character.instance_id)};
    if (found == m_character_models.end()) {
      auto gpu{std::make_unique<CharacterGpuModel>()};
      gpu->resource = character.model_resource;
      std::vector<unsigned char> usage_bits(character.model_resource->images.size(), 0U);
      for (const Omikron::MaterialGroup& group : character.posed_groups) {
        if (group.material_id < 0 ||
            static_cast<std::size_t>(group.material_id) >= usage_bits.size()) {
          continue;
        }
        usage_bits.at(static_cast<std::size_t>(group.material_id)) |=
            static_cast<unsigned char>(is_blended(Omikron::blend_mode(group.flags)) ? 2U : 1U);
      }
      bool failed{false};
      for (std::size_t image_index{0}; image_index < character.model_resource->images.size();
          ++image_index) {
        const Omikron::Texture3DTImage& image{character.model_resource->images.at(image_index)};
        const unsigned char bits{usage_bits.at(image_index)};
        const GameColorTextureUsage usage{texture_usage(bits)};
        auto texture{GameColorTexture::create(static_cast<int>(image.width),
            static_cast<int>(image.height),
            std::span<const std::uint8_t>{image.rgba8},
            usage)};
        if (!texture) {
          App::Log::warn(LogCategory::Renderer,
              "Character model '{}' texture upload failed: {}",
              character.model_resource_name,
              texture.error());
          failed = true;
          break;
        }
        gpu->textures.push_back(std::move(texture).value());
      }
      if (failed) {
        m_failed_character_models.push_back(character.model_resource_name);
        continue;
      }
      found = m_character_models.emplace(character.instance_id, std::move(gpu)).first;
    }

    CharacterGpuModel& gpu{*found->second};
    if (gpu.pose_revision == character.pose_revision) {
      continue;
    }
    // Runtime transforms a character's vertices only while submitting it, so
    // the pose is materialized here once per presented revision.
    if (Character::RuntimeCharacter* const live{
            runtime.character_runtime().find_body(character.body_identity)};
        live != nullptr) {
      if (auto posed{Character::materialize_posed_geometry(*live)}; !posed) {
        App::Log::warn(LogCategory::Renderer,
            "Character model '{}' pose failed: {}",
            character.model_resource_name,
            posed.error());
      }
    }
    // Animation re-poses every frame; reuse the GPU buffers when the group
    // layout is unchanged instead of recreating every VAO/VBO.
    if (update_character_meshes(gpu, character.posed_groups)) {
      gpu.pose_revision = character.pose_revision;
      continue;
    }
    rebuild_character_meshes(gpu, character.posed_groups);
    gpu.pose_revision = character.pose_revision;

    App::Log::debug(LogCategory::Renderer,
        "Character renderer pose ready — model={} instance={} groups={} materials={}",
        character.model_resource_name,
        character.instance_id,
        gpu.meshes.size(),
        gpu.textures.size());
  }
}

void WorldRenderer::sync_object_models(const ScenarioRuntime& runtime) {
  const auto placements{runtime.object_placement_runtime().placements()};
  m_live_object_ids.clear();
  for (const ObjectPlacement::RuntimePlacement& placement : placements) {
    m_live_object_ids.insert(placement.instance_id);
  }
  std::erase_if(m_object_models, [this](const auto& entry) {
    return !m_live_object_ids.contains(entry.first);
  });
  std::erase_if(m_object_models_by_resource, [](const auto& entry) {
    return entry.second.expired();
  });

  for (const ObjectPlacement::RuntimePlacement& placement : placements) {
    if (!placement.renderable() || placement.model_resource == nullptr ||
        m_object_models.contains(placement.instance_id) ||
        std::ranges::contains(m_failed_object_models, placement.model_resource_name)) {
      continue;
    }

    // Placements of one model resource share its textures and buffers; only
    // the per-instance transform differs at draw time.
    const ObjectPlacement::ModelResource* const resource_key{placement.model_resource.get()};
    if (const auto shared{m_object_models_by_resource.find(resource_key)};
        shared != m_object_models_by_resource.end()) {
      if (auto existing{shared->second.lock()}) {
        m_object_models.emplace(placement.instance_id, std::move(existing));
        continue;
      }
    }

    auto gpu{std::make_shared<ObjectGpuModel>()};
    gpu->resource = placement.model_resource;

    std::vector<unsigned char> usage_bits(placement.model_resource->images.size(), 0U);
    for (const Omikron::MaterialGroup& group : placement.model_resource->groups) {
      if (group.material_id < 0 ||
          static_cast<std::size_t>(group.material_id) >= usage_bits.size()) {
        continue;
      }
      usage_bits.at(static_cast<std::size_t>(group.material_id)) |=
          static_cast<unsigned char>(is_blended(Omikron::blend_mode(group.flags)) ? 2U : 1U);
    }

    bool failed{false};
    for (std::size_t image_index{0}; image_index < placement.model_resource->images.size();
        ++image_index) {
      const Omikron::Texture3DTImage& image{placement.model_resource->images.at(image_index)};
      auto texture{GameColorTexture::create(static_cast<int>(image.width),
          static_cast<int>(image.height),
          std::span<const std::uint8_t>{image.rgba8},
          texture_usage(usage_bits.at(image_index)))};
      if (!texture) {
        App::Log::warn(LogCategory::Renderer,
            "Object placement model '{}' texture upload failed: {}",
            placement.model_resource_name,
            texture.error());
        failed = true;
        break;
      }
      gpu->textures.push_back(std::move(texture).value());
    }
    if (failed) {
      m_failed_object_models.push_back(placement.model_resource_name);
      continue;
    }

    for (const Omikron::MaterialGroup& group : placement.model_resource->groups) {
      std::vector<Vertex> presentation_vertices;
      presentation_vertices.reserve(group.vertices.size());
      for (const Vertex& vertex : group.vertices) {
        presentation_vertices.push_back(Runtime::Presentation::to_gl(vertex));
      }
      gpu->meshes.emplace_back(presentation_vertices, group.indices);
      gpu->group_material_ids.push_back(group.material_id);
      gpu->group_flags.push_back(group.flags);
      gpu->group_modes.push_back(Omikron::blend_mode(group.flags));
    }
    gpu->local_bounds = model_bounds(placement.model_resource->groups);

    App::Log::debug(LogCategory::Renderer,
        "Object placement renderer ready — model={} instance={} object={} groups={} materials={}",
        placement.model_resource_name,
        placement.instance_id,
        placement.object_id,
        gpu->meshes.size(),
        gpu->textures.size());
    m_object_models_by_resource.insert_or_assign(resource_key, gpu);
    m_object_models.emplace(placement.instance_id, std::move(gpu));
  }
}

void WorldRenderer::sync_crowd(const ScenarioRuntime& runtime,
    const glm::mat4& view,
    const glm::mat4& projection,
    const ViewFrustum& frustum) {
  APP_PROFILE_FUNCTION();
  const Crowd::Runtime& crowd{runtime.crowd_runtime()};
  const std::span<const Crowd::Prototype> prototypes{crowd.prototypes()};
  const std::span<const Crowd::Agent> pedestrians{crowd.agents()};
  m_crowd_visible_count = 0;

  const bool same_prototypes{
      m_crowd_prototypes.size() == prototypes.size() && std::ranges::equal(
                                                            m_crowd_prototypes,
                                                            prototypes,
                                                            {},
                                                            [](const CrowdPrototypeGpu& gpu) {
                                                              return gpu.resource.get();
                                                            },
                                                            [](const Crowd::Prototype& prototype) {
                                                              return prototype.resource.get();
                                                            })};
  if (!same_prototypes) {
    m_crowd_prototypes.clear();
    m_crowd_instances.clear();
    m_crowd_prototypes_failed = false;
    for (const Crowd::Prototype& prototype : prototypes) {
      const Character::ModelResource& resource{*prototype.resource};
      std::vector<unsigned char> usage_bits(resource.images.size(), 0U);
      for (const Omikron::PosedGeometryLayout& layout : prototype.level_layouts) {
        for (const Omikron::MaterialGroup& group : layout.groups) {
          if (group.material_id >= 0 && std::cmp_less(group.material_id, usage_bits.size())) {
            usage_bits.at(static_cast<std::size_t>(group.material_id)) |=
                static_cast<unsigned char>(is_blended(Omikron::blend_mode(group.flags)) ? 2U : 1U);
          }
        }
      }
      CrowdPrototypeGpu gpu{.resource = prototype.resource, .textures = {}};
      for (std::size_t image_index{0}; image_index < resource.images.size(); ++image_index) {
        const Omikron::Texture3DTImage& image{resource.images.at(image_index)};
        auto texture{GameColorTexture::create(static_cast<int>(image.width),
            static_cast<int>(image.height),
            std::span<const std::uint8_t>{image.rgba8},
            texture_usage(usage_bits.at(image_index)))};
        if (!texture) {
          App::Log::warn(LogCategory::Renderer,
              "Passerby model '{}' texture upload failed: {}",
              resource.name,
              texture.error());
          m_crowd_prototypes_failed = true;
          break;
        }
        gpu.textures.push_back(std::move(texture).value());
      }
      m_crowd_prototypes.push_back(std::move(gpu));
    }
  }
  if (m_crowd_prototypes_failed) {
    m_crowd_instances.clear();
    return;
  }
  m_crowd_instances.resize(pedestrians.size());

  for (std::size_t index{0}; index < pedestrians.size(); ++index) {
    const Crowd::Agent& pedestrian{pedestrians[index]};
    CrowdInstanceGpu& instance{m_crowd_instances.at(index)};
    instance.visible = false;
    const Crowd::Prototype& prototype{prototypes[pedestrian.prototype]};
    // Passersby are animated by their ANI node; vehicles have no animation.
    const Omikron::Animation3DA* const animation{crowd.animation(pedestrian)};
    if (animation == nullptr && pedestrian.kind == Crowd::AgentKind::k_passer) {
      continue;
    }
    const Runtime::Vec3 gl_position{Runtime::Presentation::to_gl(pedestrian.instance)};
    const glm::vec4 eye_position{
        view * glm::vec4{gl_position.x, gl_position.y, gl_position.z, 1.0F}};
    // Runtime culls the instance by its root sphere; a passerby's root sits at
    // the pelvis, so twice its radius bounds the whole body.
    const BoundingSphere sphere{.center = {gl_position.x, gl_position.y, gl_position.z},
        .radius = prototype.root_radius * 2.0F};
    if (!frustum.intersects(sphere)) {
      continue;
    }

    // Runtime 0x0048D7F0: choose the level by camera depth, then bind each
    // object of that level to channel (script ID - level * level size).
    const std::size_t level{Crowd::select_level_of_detail(prototype.level_roots.size(),
        prototype.first_level,
        prototype.switch_depths,
        m_detail_level,
        -eye_position[2])};
    const Omikron::Model3DOData& model{prototype.resource->model};
    const std::vector<std::size_t>& level_objects{prototype.level_objects.at(level)};
    const std::uint32_t channel_offset{static_cast<std::uint32_t>(
        (level - prototype.first_level) * prototype.level_objects.front().size())};
    instance.objects = model.runtime_objects;
    for (const std::size_t object_index : animation == nullptr
                                              ? std::span<const std::size_t>{}
                                              : std::span<const std::size_t>{level_objects}) {
      const std::uint32_t script_id{model.meshes.at(object_index).script_id};
      if (script_id < channel_offset) {
        continue;
      }
      const Omikron::Animation3DAChannel* const channel{
          animation->channel_by_id(script_id - channel_offset)};
      const std::optional<Runtime::Quaternion> rotation{
          channel == nullptr ? std::nullopt : channel->sample_rotation(pedestrian.progress)};
      if (rotation.has_value()) {
        instance.objects.at(object_index).animation_matrix =
            Runtime::quaternion_matrix(rotation.value());
      }
    }
    // The level root takes the instance position (Runtime writes it into the
    // root's serialized position before submitting the subtree).
    instance.objects.at(level_objects.front()).local_offset = {};
    if (const auto resolved{Omikron::Model3DO::resolve_runtime_transforms(model, instance.objects)};
        !resolved) {
      continue;
    }
    if (const auto posed{Omikron::Model3DO::apply_pose(model,
            prototype.level_layouts.at(level),
            instance.objects,
            model.vertices,
            instance.groups)};
        !posed) {
      continue;
    }
    if (!update_character_meshes(instance.model, instance.groups)) {
      rebuild_character_meshes(instance.model, instance.groups);
    }

    instance.prototype = pedestrian.prototype;
    instance.lit = LitInstance{.groups = instance.groups,
        .objects = instance.objects,
        .presentation =
            Runtime::Transform{.matrix = pedestrian.facing, .translation = pedestrian.instance},
        .actor_position = pedestrian.instance,
        .actor_radius = prototype.root_radius};
    const glm::mat4 model_matrix{Runtime::Presentation::to_gl(instance.lit.presentation)};
    instance.matrices.model_view = view * model_matrix;
    instance.matrices.mvp = projection * instance.matrices.model_view;
    instance.matrices.to_model = glm::inverse(glm::mat3(model_matrix));
    instance.visible = true;
    ++m_crowd_visible_count;
  }
}

void WorldRenderer::draw_character_group(const Character::RuntimeCharacter& character,
    const InstanceMatrices& matrices,
    const std::size_t group_index,
    const float uv_phase_u,
    const float uv_phase_v,
    const bool legacy_effect) {
  const auto found{m_character_models.find(character.instance_id)};
  if (found == m_character_models.end()) {
    return;
  }
  const CharacterGpuModel& gpu{*found->second};
  const LitInstance lit{.groups = character.posed_groups,
      .objects = character.runtime_objects,
      .presentation = character.presentation_transform(),
      .actor_position = character.transform.translation,
      .actor_radius = character.actor_spatial_radius().value_or(0.0F)};
  draw_lit_group(
      gpu, gpu.textures, lit, matrices, group_index, uv_phase_u, uv_phase_v, legacy_effect);
}

void WorldRenderer::draw_lit_group(const CharacterGpuModel& gpu,
    const std::span<const GameColorTexture> textures,
    const LitInstance& lit,
    const InstanceMatrices& matrices,
    const std::size_t group_index,
    const float uv_phase_u,
    const float uv_phase_v,
    const bool legacy_effect) {
  if (group_index >= gpu.meshes.size()) {
    return;
  }

  const Shader& shader{legacy_effect ? m_shaders->legacy() : *m_character_shader};
  bind_world_shader(shader);
  const WorldUniforms& uniforms{world_uniforms(shader)};
  shader.set_uniform_mat4(
      uniforms.mvp, std::span<const GLfloat, 16>{glm::value_ptr(matrices.mvp), 16});
  shader.set_uniform_mat4(
      uniforms.model_view, std::span<const GLfloat, 16>{glm::value_ptr(matrices.model_view), 16});
  apply_actor_lighting(shader, lit, group_index, matrices.to_model);
  apply_group_uniforms(shader,
      gpu.group_flags.at(group_index),
      gpu.group_modes.at(group_index),
      uv_phase_u,
      uv_phase_v,
      legacy_effect);

  const std::int32_t material_id{gpu.group_material_ids.at(group_index)};
  if (material_id < 0 || static_cast<std::size_t>(material_id) >= textures.size()) {
    return;
  }
  const GameColorTexture& color_texture{textures[static_cast<std::size_t>(material_id)]};
  const Texture2D* texture{legacy_effect ? color_texture.legacy_effect() : color_texture.modern()};
  if (texture == nullptr) {
    return;
  }
  bind_world_texture(*texture);
  gpu.meshes.at(group_index).draw();
}

void WorldRenderer::draw_object_group(const ObjectPlacement::RuntimePlacement& placement,
    const InstanceMatrices& matrices,
    const std::size_t group_index,
    const float uv_phase_u,
    const float uv_phase_v,
    const bool legacy_effect) {
  const auto found{m_object_models.find(placement.instance_id)};
  if (found == m_object_models.end() || group_index >= found->second->meshes.size()) {
    return;
  }
  ObjectGpuModel& gpu{*found->second};

  const Shader& shader{legacy_effect ? m_shaders->legacy() : m_shaders->modern()};
  bind_world_shader(shader);
  shader.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(matrices.mvp), 16});
  shader.set_uniform_mat4(
      "u_model_view", std::span<const GLfloat, 16>{glm::value_ptr(matrices.model_view), 16});
  apply_baked_lighting(shader);
  apply_group_uniforms(shader,
      gpu.group_flags.at(group_index),
      gpu.group_modes.at(group_index),
      uv_phase_u,
      uv_phase_v,
      legacy_effect);

  const std::int32_t material_id{gpu.group_material_ids.at(group_index)};
  if (material_id < 0 || static_cast<std::size_t>(material_id) >= gpu.textures.size()) {
    return;
  }
  const GameColorTexture& color_texture{gpu.textures.at(static_cast<std::size_t>(material_id))};
  const Texture2D* texture{legacy_effect ? color_texture.legacy_effect() : color_texture.modern()};
  if (texture == nullptr) {
    return;
  }
  bind_world_texture(*texture);
  gpu.meshes.at(group_index).draw();
}

void WorldRenderer::apply_baked_lighting(const Shader& shader) const {
  shader.set_uniform_int("u_lighting_mode", 1);
  shader.set_uniform_uint("u_ambient_floor", m_frame_ambient.floor);
  shader.set_uniform_uint("u_ambient_ceiling", m_frame_ambient.ceiling);
  shader.set_uniform_int("u_light_count", 0);
}

void WorldRenderer::apply_actor_lighting(const Shader& shader,
    const LitInstance& lit,
    const std::size_t group_index,
    const glm::mat3& to_model) const {
  const WorldUniforms& uniforms{world_uniforms(shader)};
  shader.set_uniform_int(uniforms.lighting_mode, 2);
  shader.set_uniform_uint(uniforms.ambient_floor, m_frame_ambient.floor);
  shader.set_uniform_uint(uniforms.ambient_ceiling, m_frame_ambient.ceiling);

  // Runtime pairs a lit instance with every light whose shape box overlaps
  // the actor box, then attenuates per node by the node's distance.
  int light_count{0};
  if (group_index < lit.groups.size()) {
    const Omikron::MaterialGroup& group{lit.groups[group_index]};
    const bool lit_surface{(group.flags & Lighting::K_UNLIT_MESH_FLAGS) == 0U};
    if (lit_surface && group.mesh_index < lit.objects.size()) {
      const Runtime::Vec3 node_position{Runtime::transform_point(
          lit.objects[group.mesh_index].world_translation, lit.presentation)};
      const Lighting::NodeLightSet selected{Lighting::select_node_lights(
          m_lights, lit.actor_position, lit.actor_radius, node_position)};
      for (const Lighting::NodeLight& node_light : selected.active()) {
        const Lighting::LightSource& light{m_lights.at(node_light.light_index)};
        const Runtime::Vec3 gl_direction{Runtime::Presentation::to_gl(light.direction)};
        const glm::vec3 direction{
            glm::normalize(to_model * glm::vec3{gl_direction.x, gl_direction.y, gl_direction.z})};
        const std::array<float, 3> color{static_cast<float>((light.color_rgb >> 16U) & 0xFFU),
            static_cast<float>((light.color_rgb >> 8U) & 0xFFU),
            static_cast<float>(light.color_rgb & 0xFFU)};
        const auto slot{static_cast<std::size_t>(light_count)};
        shader.set_uniform_vec3(uniforms.light_direction.at(slot),
            std::span<const GLfloat, 3>{glm::value_ptr(direction), 3});
        shader.set_uniform_float(uniforms.light_scale.at(slot), node_light.scale);
        shader.set_uniform_vec3(uniforms.light_color.at(slot), std::span<const GLfloat, 3>{color});
        ++light_count;
      }
    }
  }
  shader.set_uniform_int(uniforms.light_count, light_count);
}

void WorldRenderer::render_geometry_wireframe(
    const Camera& camera, const ScenarioRuntime* const runtime) {
  if (m_shaders == nullptr) {
    return;
  }
  const Shader& wireframe{m_shaders->wireframe()};

  const glm::mat4 view{glm::make_mat4(camera.get_view_matrix().data())};
  const glm::mat4 projection{glm::make_mat4(camera.get_projection_matrix().data())};
  const glm::mat4 identity{1.0F};
  glm::mat4 mvp{projection * view * identity};

  wireframe.bind();
  wireframe.set_uniform_vec4("u_color", std::span<const GLfloat, 4>{K_WIREFRAME_COLOR});

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_CULL_FACE);
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

  wireframe.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
  for (const Mesh& mesh : m_meshes) {
    mesh.draw();
  }

  if (runtime != nullptr) {
    for (const Character::RuntimeCharacter& character : runtime->character_runtime().characters()) {
      const auto found{m_character_models.find(character.instance_id)};
      if (!character.renderable() || found == m_character_models.end()) {
        continue;
      }
      const glm::mat4 character_model{Runtime::Presentation::to_gl(character.transform)};
      mvp = projection * view * character_model;
      wireframe.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
      for (const Mesh& mesh : found->second->meshes) {
        mesh.draw();
      }
    }
    for (const ObjectPlacement::RuntimePlacement& placement :
        runtime->object_placement_runtime().placements()) {
      const auto found{m_object_models.find(placement.instance_id)};
      if (!placement.renderable() || found == m_object_models.end()) {
        continue;
      }
      const glm::mat4 object_model{Runtime::Presentation::to_gl(placement.transform)};
      mvp = projection * view * object_model;
      wireframe.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
      for (const Mesh& mesh : found->second->meshes) {
        mesh.draw();
      }
    }
  }

  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glEnable(GL_CULL_FACE);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void WorldRenderer::render(const Camera& camera,
    ScenarioRuntime* const runtime,
    const float uv_phase_u,
    const float uv_phase_v,
    const Fog::ShaderFog& fog,
    const Lighting::Ambient& ambient,
    WorldColorPipeline& color_pipeline) {
  APP_PROFILE_FUNCTION();
  m_frame_fog = fog;
  m_frame_ambient = ambient;

  if (m_shaders == nullptr) {
    return;
  }
  const Shader* const per_pixel_shader{
      m_per_pixel_lighting ? m_shaders->per_pixel_modern() : nullptr};
  m_character_shader = per_pixel_shader != nullptr ? per_pixel_shader : &m_shaders->modern();

  const glm::mat4 view{glm::make_mat4(camera.get_view_matrix().data())};
  const glm::mat4 projection{glm::make_mat4(camera.get_projection_matrix().data())};
  const glm::mat4 mvp{projection * view};
  const glm::vec3 eye{glm::make_vec3(camera.get_position().data())};

  m_last_sprite_runtime = runtime;
  if (runtime != nullptr && m_sprite_renderer.valid()) {
    const ViewBasis basis{view_basis(view)};
    m_sprite_renderer.build_queue(runtime->sprite_pool(),
        runtime->sprite_resource_ptrs(),
        eye,
        basis.front,
        basis.right,
        basis.up,
        camera.get_near_plane(),
        camera.get_far_plane());
  }

  const ViewFrustum frustum{
      ViewFrustum::from_clip_matrix(std::span<const float, 16>{glm::value_ptr(mvp), 16})};
  m_character_visible.clear();
  if (runtime != nullptr) {
    for (const Character::RuntimeCharacter& character : runtime->character_runtime().characters()) {
      m_character_visible.push_back(
          character.renderable() && character_visible(character, frustum) ? 1U : 0U);
    }
  }

  if (runtime != nullptr) {
    sync_decor_model(*runtime);
    sync_character_models(*runtime);
    sync_object_models(*runtime);
    sync_crowd(*runtime, view, projection, frustum);
  }
  // After the decor sync, which may rebuild the meshes the slopes live on.
  const Shader* const cubic_shader{
      m_cubic_decor_colors ? m_shaders->cubic_color_modern() : nullptr};
  m_decor_shader =
      cubic_shader != nullptr && ensure_decor_color_slopes() ? cubic_shader : &m_shaders->modern();

  const auto instance_matrices = [&](const glm::mat4& model, const bool lit) {
    InstanceMatrices matrices{.model_view = view * model};
    matrices.mvp = projection * matrices.model_view;
    if (lit) {
      matrices.to_model = glm::inverse(glm::mat3(model));
    }
    return matrices;
  };
  const auto character_gpu = [this](const Character::RuntimeCharacter& character,
                                 const std::size_t character_index) -> const CharacterGpuModel* {
    if (!character_presented(character_index)) {
      return nullptr;
    }
    const auto found{m_character_models.find(character.instance_id)};
    return found == m_character_models.end() ? nullptr : found->second.get();
  };

  // Lossless view-frustum culling of decor: Runtime draws everything inside the view volume, so
  // only objects whose broadphase sphere is entirely outside it are skipped.
  m_group_visible.assign(m_meshes.size(), 1U);
  m_decor_cull_stats = DecorCullStats{.groups_total = m_meshes.size()};
  if (m_group_mesh_index.size() == m_meshes.size()) {
    for (std::size_t index{0}; index < m_group_visible.size(); ++index) {
      if (!m_decor_culling.object_visible(m_group_mesh_index.at(index), frustum)) {
        m_group_visible.at(index) = 0U;
        ++m_decor_cull_stats.groups_culled;
      }
    }
  }
  m_decor_cull_stats.groups_submitted =
      m_decor_cull_stats.groups_total - m_decor_cull_stats.groups_culled;

  // Placed objects get the same lossless sphere test; their transforms are
  // computed once here for both passes.
  m_visible_objects.clear();
  if (runtime != nullptr) {
    for (const ObjectPlacement::RuntimePlacement& placement :
        runtime->object_placement_runtime().placements()) {
      const auto found{m_object_models.find(placement.instance_id)};
      if (!placement.renderable() || found == m_object_models.end()) {
        continue;
      }
      const glm::mat4 model{Runtime::Presentation::to_gl(placement.transform)};
      if (instance_visible(found->second->local_bounds, model, frustum)) {
        m_visible_objects.push_back(
            VisibleObject{.placement = &placement, .matrices = instance_matrices(model, false)});
      }
    }
  }

  // Opaque and cutout geometry first.
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  // Objects (and characters by default) draw with the original program, so it
  // always receives the frame state; enhanced variants receive their own.
  begin_world_pass(m_shaders->modern(), mvp, view, false);
  if (m_decor_shader != &m_shaders->modern()) {
    begin_world_pass(*m_decor_shader, mvp, view, false);
  }
  for (const std::size_t index : m_opaque_draw_order) {
    if (index < m_group_visible.size() && m_group_visible.at(index) != 0U) {
      draw_group(index, uv_phase_u, uv_phase_v, false);
    }
  }
  if (runtime != nullptr) {
    if (m_character_shader != &m_shaders->modern()) {
      // The per-pixel program needs the frame fog and sampler of its own.
      begin_world_pass(*m_character_shader, mvp, view, false);
    }
    const std::span<const Character::RuntimeCharacter> characters{
        runtime->character_runtime().characters()};
    for (std::size_t character_index{0}; character_index < characters.size(); ++character_index) {
      const Character::RuntimeCharacter& character{characters.subspan(character_index, 1U).front()};
      const CharacterGpuModel* const gpu{character_gpu(character, character_index)};
      if (gpu == nullptr) {
        continue;
      }
      const InstanceMatrices matrices{instance_matrices(
          Runtime::Presentation::to_gl(character.presentation_transform()), true)};
      for (std::size_t index{0}; index < gpu->meshes.size(); ++index) {
        if (!is_blended(gpu->group_modes.at(index))) {
          draw_character_group(character, matrices, index, uv_phase_u, uv_phase_v, false);
        }
      }
    }
    for (const CrowdInstanceGpu& instance : m_crowd_instances) {
      if (!instance.visible) {
        continue;
      }
      for (std::size_t index{0}; index < instance.model.meshes.size(); ++index) {
        if (!is_blended(instance.model.group_modes.at(index))) {
          draw_lit_group(instance.model,
              m_crowd_prototypes.at(instance.prototype).textures,
              instance.lit,
              instance.matrices,
              index,
              uv_phase_u,
              uv_phase_v,
              false);
        }
      }
    }

    for (const VisibleObject& visible : m_visible_objects) {
      const ObjectGpuModel& gpu{*m_object_models.at(visible.placement->instance_id)};
      for (std::size_t index{0}; index < gpu.meshes.size(); ++index) {
        if (!is_blended(gpu.group_modes.at(index))) {
          draw_object_group(
              *visible.placement, visible.matrices, index, uv_phase_u, uv_phase_v, false);
        }
      }
    }
  }
  if (runtime != nullptr && m_sprite_renderer.valid()) {
    m_sprite_renderer.set_fog(fog);
    m_sprite_renderer.draw_modern_opaque(view, projection, runtime->sprite_textures());
  }

  // Transparent decor is sorted by group centre, matching ModelViewerScene's
  // established pass ordering.
  m_blended_groups.clear();
  for (std::size_t index{0}; index < m_meshes.size(); ++index) {
    if (m_group_visible.at(index) != 0U && is_blended(m_group_modes.at(index))) {
      m_blended_groups.push_back(index);
    }
  }
  std::ranges::stable_sort(
      m_blended_groups, [this, &eye](const std::size_t lhs, const std::size_t rhs) {
        const glm::vec3 lhs_offset{glm::make_vec3(m_group_centers.at(lhs).data()) - eye};
        const glm::vec3 rhs_offset{glm::make_vec3(m_group_centers.at(rhs).data()) - eye};
        return glm::dot(lhs_offset, lhs_offset) > glm::dot(rhs_offset, rhs_offset);
      });

  std::size_t begin{0};
  while (begin < m_blended_groups.size()) {
    const LegacyBlendOperator blend_operator{
        legacy_operator(m_group_modes.at(m_blended_groups.at(begin)))};
    std::size_t end{begin + 1U};
    while (end < m_blended_groups.size() &&
           legacy_operator(m_group_modes.at(m_blended_groups.at(end))) == blend_operator) {
      ++end;
    }
    color_pipeline.composite_legacy_stage(blend_operator, end - begin, [&] {
      begin_world_pass(m_shaders->legacy(), mvp, view, true);
      for (std::size_t offset{begin}; offset < end; ++offset) {
        draw_group(m_blended_groups.at(offset), uv_phase_u, uv_phase_v, true);
      }
    });
    begin = end;
  }

  // Composites each contiguous run of same-operator blended groups of one
  // character or placement, in model group order.
  const auto composite_blended_runs = [&](const std::vector<Omikron::BlendMode>& modes,
                                          const auto& draw) {
    std::size_t run_begin{0};
    while (run_begin < modes.size()) {
      while (run_begin < modes.size() && !is_blended(modes.at(run_begin))) {
        ++run_begin;
      }
      if (run_begin == modes.size()) {
        break;
      }
      const LegacyBlendOperator blend_operator{legacy_operator(modes.at(run_begin))};
      std::size_t run_end{run_begin + 1U};
      while (run_end < modes.size() && is_blended(modes.at(run_end)) &&
             legacy_operator(modes.at(run_end)) == blend_operator) {
        ++run_end;
      }
      color_pipeline.composite_legacy_stage(blend_operator, run_end - run_begin, [&] {
        begin_world_pass(m_shaders->legacy(), mvp, view, true);
        for (std::size_t index{run_begin}; index < run_end; ++index) {
          draw(index);
        }
      });
      run_begin = run_end;
    }
  };

  if (runtime != nullptr) {
    const std::span<const Character::RuntimeCharacter> characters{
        runtime->character_runtime().characters()};
    for (std::size_t character_index{0}; character_index < characters.size(); ++character_index) {
      const Character::RuntimeCharacter& character{characters.subspan(character_index, 1U).front()};
      const CharacterGpuModel* const gpu{character_gpu(character, character_index)};
      if (gpu == nullptr ||
          std::ranges::none_of(gpu->group_modes, [](const Omikron::BlendMode mode) {
            return is_blended(mode);
          })) {
        continue;
      }
      const InstanceMatrices matrices{instance_matrices(
          Runtime::Presentation::to_gl(character.presentation_transform()), true)};
      composite_blended_runs(gpu->group_modes, [&](const std::size_t index) {
        draw_character_group(character, matrices, index, uv_phase_u, uv_phase_v, true);
      });
    }
    for (const CrowdInstanceGpu& instance : m_crowd_instances) {
      if (!instance.visible ||
          std::ranges::none_of(instance.model.group_modes, [](const Omikron::BlendMode mode) {
            return is_blended(mode);
          })) {
        continue;
      }
      composite_blended_runs(instance.model.group_modes, [&](const std::size_t index) {
        draw_lit_group(instance.model,
            m_crowd_prototypes.at(instance.prototype).textures,
            instance.lit,
            instance.matrices,
            index,
            uv_phase_u,
            uv_phase_v,
            true);
      });
    }

    for (const VisibleObject& visible : m_visible_objects) {
      const ObjectGpuModel& gpu{*m_object_models.at(visible.placement->instance_id)};
      composite_blended_runs(gpu.group_modes, [&](const std::size_t index) {
        draw_object_group(
            *visible.placement, visible.matrices, index, uv_phase_u, uv_phase_v, true);
      });
    }
  }
  if (runtime != nullptr && m_sprite_renderer.valid()) {
    for (const std::uint16_t bucket : m_sprite_renderer.legacy_buckets()) {
      const std::size_t count{m_sprite_renderer.legacy_bucket_draw_count(bucket)};
      color_pipeline.composite_legacy_stage(sprite_legacy_operator(bucket), count, [&] {
        m_sprite_renderer.draw_legacy_bucket(bucket, view, projection, runtime->sprite_textures());
      });
    }
  }

  m_bound_shader = nullptr;
  m_bound_texture = 0;
  color_pipeline.bind_current_scene();
  Shader::unbind();
}

void WorldRenderer::render_debug_overlay(
    const Camera& camera, const ScenarioRuntime* const runtime) {
  if (m_geometry_wireframe) {
    render_geometry_wireframe(camera, runtime);
  }
}

}  // namespace App

// NOLINTEND(misc-include-cleaner)

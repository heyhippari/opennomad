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
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Camera.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
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
#include "Core/WorldColorPipeline.hpp"

namespace App {

namespace {

constexpr std::string_view K_WORLD_VERTEX_SHADER{R"glsl(
#version 410 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;

uniform mat4 u_mvp;
uniform mat4 u_model;
uniform vec2 u_uv_offset;

out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;

void main() {
    v_normal = mat3(u_model) * a_normal;
    v_uv = a_uv + u_uv_offset;
    v_color = a_color;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)glsl"};

constexpr std::string_view K_MODERN_WORLD_FRAGMENT_SHADER{R"glsl(
#version 410 core
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform sampler2D u_texture0;
uniform float u_vertex_color;
uniform float u_alpha_test;
uniform vec3 u_light_direction;
uniform float u_ambient;

out vec4 frag_colour;

float srgb_to_linear(float encoded) {
    return encoded <= 0.04045 ? encoded / 12.92
                              : pow((encoded + 0.055) / 1.055, 2.4);
}

void main() {
    vec4 texel = texture(u_texture0, v_uv);
    if (u_alpha_test > 0.5 && texel.a < 0.5) {
        discard;
    }
    vec3 linear_vertex = vec3(srgb_to_linear(v_color.r), srgb_to_linear(v_color.g),
                              srgb_to_linear(v_color.b));
    vec4 baked = mix(vec4(1.0), vec4(linear_vertex, v_color.a), u_vertex_color);
    float normal_length = length(v_normal);
    float diffuse = normal_length > 0.0001
        ? max(dot(v_normal / normal_length, normalize(u_light_direction)), 0.0)
        : 1.0;
    float light = mix(u_ambient + ((1.0 - u_ambient) * diffuse), 1.0, u_vertex_color);
    frag_colour = vec4(texel.rgb * baked.rgb * light, texel.a * baked.a);
}
)glsl"};

constexpr std::string_view K_LEGACY_WORLD_FRAGMENT_SHADER{R"glsl(
#version 410 core
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_texture0;
uniform float u_vertex_color;
uniform float u_alpha_test;
uniform float u_premultiply_alpha;
uniform vec3 u_light_direction;
uniform float u_ambient;
out vec4 frag_colour;
void main() {
    vec4 texel = texture(u_texture0, v_uv);
    if (u_alpha_test > 0.5 && texel.a < 0.5) {
        discard;
    }
    vec4 baked = mix(vec4(1.0), v_color, u_vertex_color);
    float normal_length = length(v_normal);
    float diffuse = normal_length > 0.0001
        ? max(dot(v_normal / normal_length, normalize(u_light_direction)), 0.0)
        : 1.0;
    float light = mix(u_ambient + ((1.0 - u_ambient) * diffuse), 1.0, u_vertex_color);
    vec4 source = vec4(texel.rgb * baked.rgb * light, texel.a * baked.a);
    frag_colour = vec4(mix(source.rgb, source.rgb * source.a, u_premultiply_alpha), source.a);
}
)glsl"};

constexpr std::string_view K_WIREFRAME_VERTEX_SHADER{R"glsl(
#version 410 core
layout(location = 0) in vec3 a_position;

uniform mat4 u_mvp;

void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)glsl"};

constexpr std::string_view K_WIREFRAME_FRAGMENT_SHADER{R"glsl(
#version 410 core
uniform vec4 u_color;

out vec4 frag_colour;

void main() {
    frag_colour = u_color;
}
)glsl"};

constexpr std::array<GLfloat, 4> K_WIREFRAME_COLOR{0.15F, 0.95F, 1.0F, 0.9F};

/// Legacy fallback lighting used when the decor relies on ordinary normals
/// rather than baked vertex lighting. Keep these aligned with ModelViewerScene.
constexpr std::array<GLfloat, 3> K_LIGHT_DIRECTION{0.35F, 0.75F, 0.55F};

/// CPU-side integrity summary for one posed decor rebuild. Keep it beside the
/// renderer boundary so an invalid gameplay pose never reaches GL buffers.
struct DecorPoseGeometry {
  WorldBounds bounds;
  std::size_t vertex_count{0};
};

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
constexpr float K_AMBIENT_STRENGTH{0.35F};

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
    WorldSceneContext& context) {
  APP_PROFILE_FUNCTION();

  if (!context.decor_model.has_value()) {
    return std::expected<std::unique_ptr<WorldRenderer>, std::string>{
        std::unexpect, "active world context has no decoded decor model"};
  }

  const Omikron::Model3DOData& model{context.decor_model.value()};
  auto groups{
      context.runtime != nullptr && context.runtime->decor_model() == &model
          ? Omikron::Model3DO::build_posed_geometry(model, context.runtime->decor_runtime_objects())
          : Omikron::Model3DO::build_static_geometry(model)};
  if (!groups) {
    return std::expected<std::unique_ptr<WorldRenderer>, std::string>{
        std::unexpect, std::move(groups).error()};
  }

  auto modern_shader{Shader::create(K_WORLD_VERTEX_SHADER, K_MODERN_WORLD_FRAGMENT_SHADER)};
  if (!modern_shader) {
    return std::expected<std::unique_ptr<WorldRenderer>, std::string>{
        std::unexpect, std::move(modern_shader).error()};
  }
  auto legacy_shader{Shader::create(K_WORLD_VERTEX_SHADER, K_LEGACY_WORLD_FRAGMENT_SHADER)};
  if (!legacy_shader) {
    return std::expected<std::unique_ptr<WorldRenderer>, std::string>{
        std::unexpect, std::move(legacy_shader).error()};
  }
  auto wireframe_shader{Shader::create(K_WIREFRAME_VERTEX_SHADER, K_WIREFRAME_FRAGMENT_SHADER)};
  if (!wireframe_shader) {
    return std::expected<std::unique_ptr<WorldRenderer>, std::string>{
        std::unexpect, std::move(wireframe_shader).error()};
  }

  auto textures{load_decor_textures(context, model, groups.value())};
  if (!textures) {
    return std::expected<std::unique_ptr<WorldRenderer>, std::string>{
        std::unexpect, std::move(textures).error()};
  }

  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) -- private constructor.
  auto renderer{std::unique_ptr<WorldRenderer>{new WorldRenderer()}};
  renderer->m_modern_shader = std::make_unique<Shader>(std::move(modern_shader).value());
  renderer->m_legacy_shader = std::make_unique<Shader>(std::move(legacy_shader).value());
  renderer->m_wireframe_shader = std::make_unique<Shader>(std::move(wireframe_shader).value());
  renderer->m_textures = std::move(textures).value();
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

    special_flags_seen = special_flags_seen ||
                         Omikron::has_flag(group.flags, Omikron::MeshFlags::k_mirror) ||
                         Omikron::has_flag(group.flags, Omikron::MeshFlags::k_environment_mapped);
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
  if (const auto pose_geometry{describe_decor_pose(groups.value())}; pose_geometry.has_value()) {
    renderer->m_decor_pose_bounds = pose_geometry->bounds;
    renderer->m_decor_pose_vertex_count = pose_geometry->vertex_count;
  } else {
    App::Log::warn(LogCategory::Renderer,
        "World decor initial geometry has no finite vertices; pose rebuilds will be rejected");
  }

  if (special_flags_seen) {
    App::Log::debug(LogCategory::Renderer,
        "WorldRenderer: mirror/environment flags currently use the base textured pass; "
        "specialized ModelViewer passes remain to be extracted into the shared renderer");
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

void WorldRenderer::sync_decor_model(const ScenarioRuntime& runtime) {
  if (runtime.decor_model() == nullptr || runtime.decor_pose_revision() == m_decor_pose_revision) {
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
  m_decor_pose_bounds = geometry->bounds;
  m_decor_pose_vertex_count = geometry->vertex_count;
  m_decor_pose_revision = runtime.decor_pose_revision();
}

void WorldRenderer::draw_group(const std::size_t index,
    const float uv_phase_u,
    const float uv_phase_v,
    const bool legacy_effect) {
  const Shader& shader{legacy_effect ? *m_legacy_shader : *m_modern_shader};
  shader.bind();
  const std::array<float, 2> uv_offset{
      Omikron::uv_scroll_offset(m_group_flags.at(index), uv_phase_u, uv_phase_v)};
  shader.set_uniform_vec2("u_uv_offset", std::span<const GLfloat, 2>{uv_offset});
  const Omikron::BlendMode mode{m_group_modes.at(index)};
  const bool vertex_lit{
      Omikron::has_flag(m_group_flags.at(index), Omikron::MeshFlags::k_vertex_lit)};
  shader.set_uniform_float("u_vertex_color", vertex_lit ? 1.0F : 0.0F);
  shader.set_uniform_float("u_alpha_test", mode == Omikron::BlendMode::k_alpha_test ? 1.0F : 0.0F);
  if (legacy_effect) {
    shader.set_uniform_float(
        "u_premultiply_alpha", mode == Omikron::BlendMode::k_alpha_blend ? 1.0F : 0.0F);
  }

  const std::size_t material{static_cast<std::size_t>(m_group_material_ids.at(index))};
  const Texture2D* texture{
      legacy_effect ? m_textures.at(material).legacy_effect() : m_textures.at(material).modern()};
  if (texture == nullptr) {
    return;
  }
  texture->bind(0);
  shader.set_uniform_int("u_texture0", 0);
  m_meshes.at(index).draw();
}

void WorldRenderer::sync_character_models(const ScenarioRuntime& runtime) {
  for (const Character::RuntimeCharacter& character : runtime.character_runtime().characters()) {
    if (!character.renderable() || character.model_resource == nullptr ||
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
    gpu.meshes.clear();
    gpu.group_material_ids.clear();
    gpu.group_flags.clear();
    gpu.group_modes.clear();
    for (const Omikron::MaterialGroup& group : character.posed_groups) {
      std::vector<Vertex> presentation_vertices;
      presentation_vertices.reserve(group.vertices.size());
      for (const Vertex& vertex : group.vertices) {
        presentation_vertices.push_back(Runtime::Presentation::to_gl(vertex));
      }
      gpu.meshes.emplace_back(presentation_vertices, group.indices);
      gpu.group_material_ids.push_back(group.material_id);
      gpu.group_flags.push_back(group.flags);
      gpu.group_modes.push_back(Omikron::blend_mode(group.flags));
    }
    gpu.pose_revision = character.pose_revision;

    App::Log::debug(LogCategory::Renderer,
        "Character renderer pose ready — model={} instance={} groups={} materials={}",
        character.model_resource_name,
        character.instance_id,
        gpu.meshes.size(),
        gpu.textures.size());
  }
}

void WorldRenderer::draw_character_group(const Character::RuntimeCharacter& character,
    const Camera& camera,
    const std::size_t group_index,
    const float uv_phase_u,
    const float uv_phase_v,
    const bool legacy_effect) {
  const auto found{m_character_models.find(character.instance_id)};
  if (found == m_character_models.end()) {
    return;
  }
  CharacterGpuModel& gpu{*found->second};
  if (group_index >= gpu.meshes.size()) {
    return;
  }

  const glm::mat4 view{glm::make_mat4(camera.get_view_matrix().data())};
  const glm::mat4 projection{glm::make_mat4(camera.get_projection_matrix().data())};
  const glm::mat4 model{Runtime::Presentation::to_gl(character.presentation_transform())};
  const glm::mat4 mvp{projection * view * model};
  const Shader& shader{legacy_effect ? *m_legacy_shader : *m_modern_shader};
  shader.bind();
  shader.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
  shader.set_uniform_mat4("u_model", std::span<const GLfloat, 16>{glm::value_ptr(model), 16});
  const std::array<float, 2> uv_offset{
      Omikron::uv_scroll_offset(gpu.group_flags.at(group_index), uv_phase_u, uv_phase_v)};
  shader.set_uniform_vec2("u_uv_offset", std::span<const GLfloat, 2>{uv_offset});

  const Omikron::BlendMode mode{gpu.group_modes.at(group_index)};
  const bool vertex_lit{
      Omikron::has_flag(gpu.group_flags.at(group_index), Omikron::MeshFlags::k_vertex_lit)};
  shader.set_uniform_float("u_vertex_color", vertex_lit ? 1.0F : 0.0F);
  shader.set_uniform_float("u_alpha_test", mode == Omikron::BlendMode::k_alpha_test ? 1.0F : 0.0F);
  if (legacy_effect) {
    shader.set_uniform_float(
        "u_premultiply_alpha", mode == Omikron::BlendMode::k_alpha_blend ? 1.0F : 0.0F);
  }

  const std::int32_t material_id{gpu.group_material_ids.at(group_index)};
  if (material_id < 0 || static_cast<std::size_t>(material_id) >= gpu.textures.size()) {
    return;
  }
  const GameColorTexture& color_texture{gpu.textures.at(static_cast<std::size_t>(material_id))};
  const Texture2D* texture{legacy_effect ? color_texture.legacy_effect() : color_texture.modern()};
  if (texture == nullptr) {
    return;
  }
  texture->bind(0);
  shader.set_uniform_int("u_texture0", 0);
  gpu.meshes.at(group_index).draw();
}

void WorldRenderer::render_geometry_wireframe(
    const Camera& camera, const ScenarioRuntime* const runtime) {
  if (m_wireframe_shader == nullptr) {
    return;
  }
  
  const glm::mat4 view{glm::make_mat4(camera.get_view_matrix().data())};
  const glm::mat4 projection{glm::make_mat4(camera.get_projection_matrix().data())};
  const glm::mat4 identity{1.0F};
  glm::mat4 mvp{projection * view * identity};

  m_wireframe_shader->bind();
  m_wireframe_shader->set_uniform_vec4("u_color", std::span<const GLfloat, 4>{K_WIREFRAME_COLOR});

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_CULL_FACE);
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

  m_wireframe_shader->set_uniform_mat4(
      "u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
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
      m_wireframe_shader->set_uniform_mat4(
          "u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
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
    WorldColorPipeline& color_pipeline) {
  APP_PROFILE_FUNCTION();

  if (m_modern_shader == nullptr || m_legacy_shader == nullptr) {
    return;
  }

  const glm::mat4 view{glm::make_mat4(camera.get_view_matrix().data())};
  const glm::mat4 projection{glm::make_mat4(camera.get_projection_matrix().data())};
  const glm::mat4 mvp{projection * view};
  const glm::mat4 identity{1.0F};
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

  if (runtime != nullptr) {
    sync_decor_model(*runtime);
    sync_character_models(*runtime);
  }

  const auto configure_world_shader = [&](Shader& shader) {
    shader.bind();
    shader.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
    shader.set_uniform_mat4("u_model", std::span<const GLfloat, 16>{glm::value_ptr(identity), 16});
    shader.set_uniform_vec3("u_light_direction", std::span<const GLfloat, 3>{K_LIGHT_DIRECTION});
    shader.set_uniform_float("u_ambient", K_AMBIENT_STRENGTH);
  };
  configure_world_shader(*m_modern_shader);

  // Opaque and cutout geometry first.
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  for (std::size_t index{0}; index < m_meshes.size(); ++index) {
    if (!is_blended(m_group_modes.at(index))) {
      draw_group(index, uv_phase_u, uv_phase_v, false);
    }
  }
  if (runtime != nullptr) {
    for (const Character::RuntimeCharacter& character : runtime->character_runtime().characters()) {
      const auto found{m_character_models.find(character.instance_id)};
      if (!character.renderable() || found == m_character_models.end()) {
        continue;
      }
      for (std::size_t index{0}; index < found->second->meshes.size(); ++index) {
        if (!is_blended(found->second->group_modes.at(index))) {
          draw_character_group(character, camera, index, uv_phase_u, uv_phase_v, false);
        }
      }
    }
  }
  if (runtime != nullptr && m_sprite_renderer.valid()) {
    m_sprite_renderer.draw_modern_opaque(view, projection, runtime->sprite_textures());
  }

  // Transparent decor is sorted by group centre, matching ModelViewerScene's
  // established pass ordering.
  std::vector<std::size_t> blended;
  blended.reserve(m_meshes.size());
  for (std::size_t index{0}; index < m_meshes.size(); ++index) {
    if (is_blended(m_group_modes.at(index))) {
      blended.push_back(index);
    }
  }
  std::ranges::stable_sort(blended, [this, &eye](const std::size_t lhs, const std::size_t rhs) {
    const glm::vec3 lhs_offset{glm::make_vec3(m_group_centers.at(lhs).data()) - eye};
    const glm::vec3 rhs_offset{glm::make_vec3(m_group_centers.at(rhs).data()) - eye};
    return glm::dot(lhs_offset, lhs_offset) > glm::dot(rhs_offset, rhs_offset);
  });

  std::size_t begin{0};
  while (begin < blended.size()) {
    const LegacyBlendOperator blend_operator{legacy_operator(m_group_modes.at(blended.at(begin)))};
    std::size_t end{begin + 1U};
    while (end < blended.size() &&
           legacy_operator(m_group_modes.at(blended.at(end))) == blend_operator) {
      ++end;
    }
    color_pipeline.composite_legacy_stage(blend_operator, end - begin, [&] {
      configure_world_shader(*m_legacy_shader);
      for (std::size_t offset{begin}; offset < end; ++offset) {
        draw_group(blended.at(offset), uv_phase_u, uv_phase_v, true);
      }
    });
    begin = end;
  }

  if (runtime != nullptr) {
    for (const Character::RuntimeCharacter& character : runtime->character_runtime().characters()) {
      const auto found{m_character_models.find(character.instance_id)};
      if (!character.renderable() || found == m_character_models.end()) {
        continue;
      }
      std::size_t character_begin{0};
      while (character_begin < found->second->meshes.size()) {
        while (character_begin < found->second->meshes.size() &&
               !is_blended(found->second->group_modes.at(character_begin))) {
          ++character_begin;
        }
        if (character_begin == found->second->meshes.size()) {
          break;
        }
        const LegacyBlendOperator blend_operator{
            legacy_operator(found->second->group_modes.at(character_begin))};
        std::size_t character_end{character_begin + 1U};
        while (character_end < found->second->meshes.size() &&
               is_blended(found->second->group_modes.at(character_end)) &&
               legacy_operator(found->second->group_modes.at(character_end)) == blend_operator) {
          ++character_end;
        }
        color_pipeline.composite_legacy_stage(blend_operator, character_end - character_begin, [&] {
          configure_world_shader(*m_legacy_shader);
          for (std::size_t index{character_begin}; index < character_end; ++index) {
            draw_character_group(character, camera, index, uv_phase_u, uv_phase_v, true);
          }
        });
        character_begin = character_end;
      }
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

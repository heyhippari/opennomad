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

out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;

void main() {
    v_normal = mat3(u_model) * a_normal;
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)glsl"};

constexpr std::string_view K_WORLD_FRAGMENT_SHADER{R"glsl(
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
    frag_colour = vec4(texel.rgb * baked.rgb * light, texel.a * baked.a);
}
)glsl"};

/// Legacy fallback lighting used when the decor relies on ordinary normals
/// rather than baked vertex lighting. Keep these aligned with ModelViewerScene.
constexpr std::array<GLfloat, 3> K_LIGHT_DIRECTION{0.35F, 0.75F, 0.55F};
constexpr float K_AMBIENT_STRENGTH{0.35F};

bool is_blended(const Omikron::BlendMode mode) {
  return mode == Omikron::BlendMode::k_alpha_blend || mode == Omikron::BlendMode::k_additive ||
         mode == Omikron::BlendMode::k_subtractive;
}

std::expected<std::vector<Texture2D>, std::string> make_white_textures(const std::size_t count) {
  static constexpr std::array<std::uint8_t, 4> k_white_pixel{255, 255, 255, 255};
  std::vector<Texture2D> textures;
  textures.reserve(count);
  for (std::size_t index{0}; index < count; ++index) {
    auto texture{Texture2D::create(1, 1, std::span<const std::uint8_t>{k_white_pixel}, true)};
    if (!texture) {
      return std::expected<std::vector<Texture2D>, std::string>{
          std::unexpect, std::move(texture).error()};
    }
    textures.push_back(std::move(texture).value());
  }
  return textures;
}

std::expected<std::vector<Texture2D>, std::string> load_decor_textures(
    const WorldSceneContext& context, const Omikron::Model3DOData& model) {
  if (!context.decor_path.has_value()) {
    App::Log::warn(LogCategory::Renderer,
        "World decor has no source path; rendering {} materials with white fallback textures",
        model.materials.size());
    return make_white_textures(model.materials.size());
  }

  std::filesystem::path texture_path{context.decor_path.value()};
  texture_path.replace_extension(".3dt");
  auto file{load_game_file(texture_path)};
  if (!file) {
    App::Log::warn(LogCategory::Renderer,
        "World decor texture '{}' unavailable: {}; using white fallbacks",
        texture_path.string(),
        file.error());
    return make_white_textures(model.materials.size());
  }

  auto images{Omikron::Texture3DT::load(std::span<const std::byte>{file->bytes}, model.materials)};
  if (!images) {
    App::Log::warn(LogCategory::Renderer,
        "World decor texture '{}' failed to decode: {}; using white fallbacks",
        texture_path.string(),
        images.error());
    return make_white_textures(model.materials.size());
  }

  std::vector<Texture2D> textures;
  textures.reserve(images->size());
  for (const Omikron::Texture3DTImage& image : images.value()) {
    if (image.width == 0U || image.height == 0U) {
      auto fallback{make_white_textures(1)};
      if (!fallback) {
        return fallback;
      }
      textures.push_back(std::move(fallback->front()));
      continue;
    }
    auto texture{Texture2D::create(static_cast<int>(image.width),
        static_cast<int>(image.height),
        std::span<const std::uint8_t>{image.rgba8},
        true)};
    if (!texture) {
      return std::expected<std::vector<Texture2D>, std::string>{
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
  auto groups{Omikron::Model3DO::build_static_geometry(model)};
  if (!groups) {
    return std::expected<std::unique_ptr<WorldRenderer>, std::string>{
        std::unexpect, std::move(groups).error()};
  }

  auto shader{Shader::create(K_WORLD_VERTEX_SHADER, K_WORLD_FRAGMENT_SHADER)};
  if (!shader) {
    return std::expected<std::unique_ptr<WorldRenderer>, std::string>{
        std::unexpect, std::move(shader).error()};
  }

  auto textures{load_decor_textures(context, model)};
  if (!textures) {
    return std::expected<std::unique_ptr<WorldRenderer>, std::string>{
        std::unexpect, std::move(textures).error()};
  }

  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) -- private constructor.
  auto renderer{std::unique_ptr<WorldRenderer>{new WorldRenderer()}};
  renderer->m_shader = std::make_unique<Shader>(std::move(shader).value());
  renderer->m_textures = std::move(textures).value();

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

void WorldRenderer::draw_group(const std::size_t index) {
  m_shader->bind();
  const Omikron::BlendMode mode{m_group_modes.at(index)};
  const bool vertex_lit{
      Omikron::has_flag(m_group_flags.at(index), Omikron::MeshFlags::k_vertex_lit)};
  m_shader->set_uniform_float("u_vertex_color", vertex_lit ? 1.0F : 0.0F);
  m_shader->set_uniform_float(
      "u_alpha_test", mode == Omikron::BlendMode::k_alpha_test ? 1.0F : 0.0F);

  const std::size_t material{static_cast<std::size_t>(m_group_material_ids.at(index))};
  m_textures.at(material).bind(0);
  m_shader->set_uniform_int("u_texture0", 0);
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
      bool failed{false};
      for (const Omikron::Texture3DTImage& image : character.model_resource->images) {
        auto texture{Texture2D::create(static_cast<int>(image.width),
            static_cast<int>(image.height),
            std::span<const std::uint8_t>{image.rgba8},
            true)};
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
    const std::size_t group_index) {
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
  const glm::mat4 model{Runtime::Presentation::to_gl(character.transform)};
  const glm::mat4 mvp{projection * view * model};
  m_shader->bind();
  m_shader->set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
  m_shader->set_uniform_mat4("u_model", std::span<const GLfloat, 16>{glm::value_ptr(model), 16});

  const Omikron::BlendMode mode{gpu.group_modes.at(group_index)};
  const bool vertex_lit{
      Omikron::has_flag(gpu.group_flags.at(group_index), Omikron::MeshFlags::k_vertex_lit)};
  m_shader->set_uniform_float("u_vertex_color", vertex_lit ? 1.0F : 0.0F);
  m_shader->set_uniform_float(
      "u_alpha_test", mode == Omikron::BlendMode::k_alpha_test ? 1.0F : 0.0F);

  const std::int32_t material_id{gpu.group_material_ids.at(group_index)};
  if (material_id < 0 || static_cast<std::size_t>(material_id) >= gpu.textures.size()) {
    return;
  }
  gpu.textures.at(static_cast<std::size_t>(material_id)).bind(0);
  m_shader->set_uniform_int("u_texture0", 0);
  gpu.meshes.at(group_index).draw();
}

void WorldRenderer::render(const Camera& camera, ScenarioRuntime* const runtime) {
  APP_PROFILE_FUNCTION();

  if (m_shader == nullptr) {
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
    sync_character_models(*runtime);
  }

  m_shader->bind();
  m_shader->set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
  m_shader->set_uniform_mat4(
      "u_model", std::span<const GLfloat, 16>{glm::value_ptr(identity), 16});
  m_shader->set_uniform_vec3("u_light_direction", std::span<const GLfloat, 3>{K_LIGHT_DIRECTION});
  m_shader->set_uniform_float("u_ambient", K_AMBIENT_STRENGTH);

  // Opaque and cutout geometry first.
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  for (std::size_t index{0}; index < m_meshes.size(); ++index) {
    if (!is_blended(m_group_modes.at(index))) {
      draw_group(index);
    }
  }
  if (runtime != nullptr) {
    for (const Character::RuntimeCharacter& character :
        runtime->character_runtime().characters()) {
      const auto found{m_character_models.find(character.instance_id)};
      if (!character.renderable() || found == m_character_models.end()) {
        continue;
      }
      for (std::size_t index{0}; index < found->second->meshes.size(); ++index) {
        if (!is_blended(found->second->group_modes.at(index))) {
          draw_character_group(character, camera, index);
        }
      }
    }
  }
  if (runtime != nullptr && m_sprite_renderer.valid()) {
    m_sprite_renderer.draw_pass(
        Sprite::SpritePass::k_opaque, view, projection, runtime->sprite_textures());
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
  std::ranges::sort(blended, [this, &eye](const std::size_t lhs, const std::size_t rhs) {
    const glm::vec3 lhs_offset{glm::make_vec3(m_group_centers.at(lhs).data()) - eye};
    const glm::vec3 rhs_offset{glm::make_vec3(m_group_centers.at(rhs).data()) - eye};
    return glm::dot(lhs_offset, lhs_offset) > glm::dot(rhs_offset, rhs_offset);
  });

  glEnable(GL_BLEND);
  glDepthMask(GL_FALSE);
  m_shader->bind();
  m_shader->set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
  m_shader->set_uniform_mat4(
      "u_model", std::span<const GLfloat, 16>{glm::value_ptr(identity), 16});
  for (const std::size_t index : blended) {
    switch (m_group_modes.at(index)) {
      case Omikron::BlendMode::k_additive:
        glBlendFunc(GL_ONE, GL_ONE);
        glBlendEquation(GL_FUNC_ADD);
        break;
      case Omikron::BlendMode::k_subtractive:
        glBlendFunc(GL_ONE, GL_ONE);
        glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        break;
      default:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        break;
    }
    draw_group(index);
  }
  if (runtime != nullptr) {
    for (const Character::RuntimeCharacter& character :
        runtime->character_runtime().characters()) {
      const auto found{m_character_models.find(character.instance_id)};
      if (!character.renderable() || found == m_character_models.end()) {
        continue;
      }
      for (std::size_t index{0}; index < found->second->meshes.size(); ++index) {
        const Omikron::BlendMode mode{found->second->group_modes.at(index)};
        if (!is_blended(mode)) {
          continue;
        }
        switch (mode) {
          case Omikron::BlendMode::k_additive:
            glBlendFunc(GL_ONE, GL_ONE);
            glBlendEquation(GL_FUNC_ADD);
            break;
          case Omikron::BlendMode::k_subtractive:
            glBlendFunc(GL_ONE, GL_ONE);
            glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
            break;
          default:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
            break;
        }
        draw_character_group(character, camera, index);
      }
    }
  }
  if (runtime != nullptr && m_sprite_renderer.valid()) {
    m_sprite_renderer.draw_pass(
        Sprite::SpritePass::k_translucent, view, projection, runtime->sprite_textures());
  }

  glBlendEquation(GL_FUNC_ADD);
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  Shader::unbind();
}

}  // namespace App

// NOLINTEND(misc-include-cleaner)

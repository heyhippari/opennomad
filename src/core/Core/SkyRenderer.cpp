#include "Core/SkyRenderer.hpp"

#include <glad/glad.h>

// NOLINTBEGIN(misc-include-cleaner) -- GLM uses umbrella headers by design.
#include <fmt/format.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Camera.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/Texture3DT.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/RuntimePresentation.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/Vertex.hpp"
#include "Core/WorldFog.hpp"
#include "Core/WorldLighting.hpp"
#include "Core/WorldShaders.hpp"

namespace App {

namespace Sky {

std::string resource_path(const std::string_view sky_name, const std::string_view extension) {
  return fmt::format("MESHES/MISC/{}{}", sky_name, extension);
}

std::expected<std::vector<Omikron::MaterialGroup>, std::string> build_geometry(
    const Omikron::Model3DOData& model) {
  if (model.root_mesh_index < 0 ||
      static_cast<std::size_t>(model.root_mesh_index) >= model.runtime_objects.size()) {
    return std::expected<std::vector<Omikron::MaterialGroup>, std::string>{
        std::unexpect, "sky model has no root object"};
  }
  const auto root{static_cast<std::size_t>(model.root_mesh_index)};

  std::vector<Omikron::Model3DOData::RuntimeObjectState> objects{model.runtime_objects};
  for (Omikron::Model3DOData::RuntimeObjectState& object : objects) {
    object.scale = {.x = K_RUNTIME_SKY_SCALE, .y = K_RUNTIME_SKY_SCALE, .z = K_RUNTIME_SKY_SCALE};
  }
  Omikron::Model3DOData::RuntimeObjectState& root_object{objects.at(root)};
  root_object.local_offset = {
      .x = 0.0F, .y = model.meshes.at(root).position.y - K_RUNTIME_SKY_ROOT_Y_OFFSET, .z = 0.0F};
  if (auto resolved{Omikron::Model3DO::resolve_runtime_transforms(model, std::span{objects})};
      !resolved) {
    return std::expected<std::vector<Omikron::MaterialGroup>, std::string>{
        std::unexpect, std::move(resolved).error()};
  }

  auto groups{Omikron::Model3DO::build_posed_geometry(model, objects)};
  if (!groups) {
    return groups;
  }
  for (Omikron::MaterialGroup& group : groups.value()) {
    if (group.mesh_index == root) {
      group.flags &= ~K_RUNTIME_SKY_CLEARED_ROOT_FLAGS;
      group.flags |= K_RUNTIME_SKY_SET_ROOT_FLAGS;
    }
  }
  return groups;
}

Runtime::Transform follow_transform(const Runtime::Vec3& camera_eye) {
  return Runtime::Transform{.matrix = Runtime::Matrix3::identity(),
      .translation = {.x = camera_eye.x, .y = 0.0F, .z = camera_eye.z}};
}

}  // namespace Sky

std::expected<std::unique_ptr<SkyRenderer>, std::string> SkyRenderer::create(
    const std::string_view sky_name) {
  using Result = std::expected<std::unique_ptr<SkyRenderer>, std::string>;

  const std::string model_path{Sky::resource_path(sky_name, ".3DO")};
  auto model_file{load_game_file(model_path)};
  if (!model_file) {
    return Result{std::unexpect, fmt::format("sky model '{}': {}", model_path, model_file.error())};
  }
  auto model{Omikron::Model3DO::load(std::span<const std::byte>{model_file->bytes})};
  if (!model) {
    return Result{std::unexpect,
        fmt::format("sky model '{}': {}", model_file->resolved.string(), model.error())};
  }
  auto groups{Sky::build_geometry(model.value())};
  if (!groups) {
    return Result{std::unexpect,
        fmt::format("sky model '{}' geometry: {}", model_file->resolved.string(), groups.error())};
  }

  const std::string texture_path{Sky::resource_path(sky_name, ".3DT")};
  auto texture_file{load_game_file(texture_path)};
  if (!texture_file) {
    return Result{
        std::unexpect, fmt::format("sky texture '{}': {}", texture_path, texture_file.error())};
  }
  auto images{
      Omikron::Texture3DT::load(std::span<const std::byte>{texture_file->bytes}, model->materials)};
  if (!images) {
    return Result{std::unexpect,
        fmt::format("sky texture '{}': {}", texture_file->resolved.string(), images.error())};
  }

  auto shader{
      Shader::create(K_WORLD_VERTEX_SHADER, fogged_fragment_shader(K_MODERN_WORLD_FRAGMENT_BODY))};
  if (!shader) {
    return Result{std::unexpect, std::move(shader).error()};
  }

  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) -- private constructor.
  auto renderer{std::unique_ptr<SkyRenderer>{new SkyRenderer()}};
  renderer->m_name = std::string{sky_name};
  renderer->m_shader = std::make_unique<Shader>(std::move(shader).value());
  for (const Omikron::Texture3DTImage& image : images.value()) {
    static constexpr std::array<std::uint8_t, 4> k_white_pixel{255, 255, 255, 255};
    const bool empty{image.width == 0U || image.height == 0U};
    auto texture{GameColorTexture::create(empty ? 1 : static_cast<int>(image.width),
        empty ? 1 : static_cast<int>(image.height),
        empty ? std::span<const std::uint8_t>{k_white_pixel}
              : std::span<const std::uint8_t>{image.rgba8},
        GameColorTextureUsage::k_modern)};
    if (!texture) {
      return Result{std::unexpect, std::move(texture).error()};
    }
    renderer->m_textures.push_back(std::move(texture).value());
  }
  // 0x0041D280 reads material 0's texture page; the copy is sampled by both
  // the opaque and the blended world programs.
  if (const std::vector<Omikron::Texture3DTImage>& sky_images{images.value()};
      !sky_images.empty() && sky_images.front().width != 0U && sky_images.front().height != 0U) {
    const Omikron::Texture3DTImage& image{sky_images.front()};
    auto environment{GameColorTexture::create(static_cast<int>(image.width),
        static_cast<int>(image.height),
        std::span<const std::uint8_t>{image.rgba8},
        GameColorTextureUsage::k_both)};
    if (!environment) {
      return Result{std::unexpect, std::move(environment).error()};
    }
    renderer->m_environment_texture = std::move(environment).value();
  }
  for (const Omikron::MaterialGroup& group : groups.value()) {
    std::vector<Vertex> presentation_vertices;
    presentation_vertices.reserve(group.vertices.size());
    for (const Vertex& vertex : group.vertices) {
      presentation_vertices.push_back(Runtime::Presentation::to_gl(vertex));
    }
    renderer->m_meshes.emplace_back(presentation_vertices, group.indices);
    renderer->m_group_material_ids.push_back(group.material_id);
    renderer->m_group_flags.push_back(group.flags);
  }
  return renderer;
}

void SkyRenderer::render(const Camera& camera,
    const Runtime::Vec3& camera_eye,
    const float uv_phase_u,
    const float uv_phase_v,
    const Fog::ShaderFog& fog,
    const Lighting::Ambient& ambient) const {
  const glm::mat4 view{glm::make_mat4(camera.get_view_matrix().data())};
  const glm::mat4 projection{glm::make_mat4(camera.get_projection_matrix().data())};
  const glm::mat4 model{Runtime::Presentation::to_gl(Sky::follow_transform(camera_eye))};
  const glm::mat4 model_view{view * model};
  const glm::mat4 mvp{projection * model_view};

  const GLboolean depth_test{glIsEnabled(GL_DEPTH_TEST)};
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_DEPTH_CLAMP);

  m_shader->bind();
  m_shader->set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
  m_shader->set_uniform_mat4(
      "u_model_view", std::span<const GLfloat, 16>{glm::value_ptr(model_view), 16});
  m_shader->set_uniform_int("u_texture0", 0);
  m_shader->set_uniform_int("u_lighting_mode", 1);
  m_shader->set_uniform_uint("u_ambient_floor", ambient.floor);
  m_shader->set_uniform_uint("u_ambient_ceiling", ambient.ceiling);
  m_shader->set_uniform_int("u_light_count", 0);
  for (std::size_t index{0}; index < m_meshes.size(); ++index) {
    const std::int32_t material_id{m_group_material_ids.at(index)};
    if (material_id < 0 || static_cast<std::size_t>(material_id) >= m_textures.size()) {
      continue;
    }
    const Texture2D* const texture{m_textures.at(static_cast<std::size_t>(material_id)).modern()};
    if (texture == nullptr) {
      continue;
    }
    const std::uint32_t flags{m_group_flags.at(index)};
    const std::array<float, 2> uv_offset{Omikron::uv_scroll_offset(flags, uv_phase_u, uv_phase_v)};
    m_shader->set_uniform_vec2("u_uv_offset", std::span<const GLfloat, 2>{uv_offset});
    m_shader->set_uniform_float("u_alpha_test",
        Omikron::blend_mode(flags) == Omikron::BlendMode::k_alpha_test ? 1.0F : 0.0F);
    Fog::apply_uniforms(*m_shader,
        fog,
        Omikron::has_flag(flags, Omikron::MeshFlags::k_doubled_fog_range)
            ? Fog::K_DOUBLED_RANGE_DEPTH_SCALE
            : 1.0F);
    texture->bind(0);
    m_meshes.at(index).draw();
  }

  glDisable(GL_DEPTH_CLAMP);
  glDepthMask(GL_TRUE);
  if (depth_test == GL_TRUE) {
    glEnable(GL_DEPTH_TEST);
  }
  Shader::unbind();
}

}  // namespace App

// NOLINTEND(misc-include-cleaner)

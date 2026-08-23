#include "Core/WorldColorPipeline.hpp"

#include <glad/glad.h>

#include <array>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Framebuffer.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"

namespace App {

namespace {

constexpr std::string_view K_TRANSFER_VERTEX_SHADER{R"glsl(
#version 410 core

out vec2 v_uv;

void main() {
  const vec2 positions[3] = vec2[3](
      vec2(-1.0, -1.0),
      vec2( 3.0, -1.0),
      vec2(-1.0,  3.0));
  vec2 position = positions[gl_VertexID];
  v_uv = (position * 0.5) + 0.5;
  gl_Position = vec4(position, 0.0, 1.0);
}
)glsl"};

constexpr std::string_view K_TRANSFER_FRAGMENT_SHADER{R"glsl(
#version 410 core

in vec2 v_uv;
uniform sampler2D u_source;
uniform int u_decode;
out vec4 frag_color;

float srgb_to_linear(float encoded) {
  if (encoded <= 0.04045) {
    return encoded / 12.92;
  }
  return pow((encoded + 0.055) / 1.055, 2.4);
}

float linear_to_srgb(float linear) {
  if (linear <= 0.0031308) {
    return 12.92 * linear;
  }
  return (1.055 * pow(linear, 1.0 / 2.4)) - 0.055;
}

void main() {
  vec4 source = texture(u_source, v_uv);
  vec3 converted;
  if (u_decode != 0) {
    converted = vec3(srgb_to_linear(source.r),
                     srgb_to_linear(source.g),
                     srgb_to_linear(source.b));
  } else {
    converted = vec3(linear_to_srgb(source.r),
                     linear_to_srgb(source.g),
                     linear_to_srgb(source.b));
  }
  frag_color = vec4(converted, source.a);
}
)glsl"};

}  // namespace

std::expected<std::unique_ptr<WorldColorPipeline>, std::string> WorldColorPipeline::create() {
  auto shader{Shader::create(K_TRANSFER_VERTEX_SHADER, K_TRANSFER_FRAGMENT_SHADER)};
  if (!shader) {
    return std::expected<std::unique_ptr<WorldColorPipeline>, std::string>{
        std::unexpect, std::move(shader).error()};
  }

  GLuint vertex_array{0};
  glGenVertexArrays(1, &vertex_array);
  if (vertex_array == 0U) {
    return std::expected<std::unique_ptr<WorldColorPipeline>, std::string>{
        std::unexpect, "failed to create color-transfer vertex array"};
  }

  // The constructor is private; only the factory may build the pipeline.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return std::unique_ptr<WorldColorPipeline>{
      new WorldColorPipeline(std::move(shader).value(), vertex_array)};
}

WorldColorPipeline::WorldColorPipeline(Shader transfer_shader, const GLuint vertex_array)
    : m_transfer_shader(std::move(transfer_shader)), m_vertex_array(vertex_array) {}

WorldColorPipeline::~WorldColorPipeline() {
  if (m_vertex_array != 0U) {
    glDeleteVertexArrays(1, &m_vertex_array);
  }
}

std::expected<void, std::string> WorldColorPipeline::ensure_targets(
    const int width, const int height) {
  APP_PROFILE_FUNCTION();

  if (width <= 0 || height <= 0) {
    return std::expected<void, std::string>{
        std::unexpect, "world color targets require positive drawable dimensions"};
  }
  if (m_legacy_target.has_value() && m_linear_target.has_value() &&
      m_legacy_target->width() == width && m_legacy_target->height() == height &&
      m_linear_target->width() == width && m_linear_target->height() == height) {
    return {};
  }

  auto legacy{Framebuffer::create(width, height, k_legacy_encoded_target_description)};
  if (!legacy) {
    return std::expected<void, std::string>{
        std::unexpect, "legacy encoded target: " + std::move(legacy).error()};
  }
  auto linear{Framebuffer::create(width, height, k_linear_scene_target_description)};
  if (!linear) {
    return std::expected<void, std::string>{
        std::unexpect, "linear scene target: " + std::move(linear).error()};
  }

  // Both allocations completed before either live target is replaced.
  m_legacy_target.emplace(std::move(legacy).value());
  m_linear_target.emplace(std::move(linear).value());
  return {};
}

void WorldColorPipeline::begin_legacy(const std::array<float, 4> clear_color) const {
  if (!m_legacy_target.has_value()) {
    return;
  }
  m_legacy_target->bind();
  glDisable(GL_FRAMEBUFFER_SRGB);
  glDisable(GL_DITHER);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_TRUE);
  glStencilMask(0xFFU);
  glViewport(0, 0, m_legacy_target->width(), m_legacy_target->height());
  glClearColor(clear_color.at(0), clear_color.at(1), clear_color.at(2), clear_color.at(3));
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void WorldColorPipeline::draw_transfer(const Texture2D& source, const bool decode) const {
  m_transfer_shader.bind();
  m_transfer_shader.set_uniform_int("u_source", 0);
  m_transfer_shader.set_uniform_int("u_decode", decode ? 1 : 0);
  source.bind(0);
  glBindVertexArray(m_vertex_array);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

void WorldColorPipeline::resolve_legacy_to_linear() const {
  if (!m_legacy_target.has_value() || !m_linear_target.has_value()) {
    return;
  }

  glDisable(GL_FRAMEBUFFER_SRGB);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);

  m_linear_target->bind();
  glViewport(0, 0, m_linear_target->width(), m_linear_target->height());
  draw_transfer(m_legacy_target->color_texture(), true);
  m_legacy_target->blit_depth_stencil_to(*m_linear_target);
  m_linear_target->bind();

  Texture2D::unbind();
  glBindVertexArray(0);
  Shader::unbind();

  // Establish the normal 3D baseline for optional modern scene overlays.
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_STENCIL_TEST);
  glEnable(GL_CULL_FACE);
  glDisable(GL_BLEND);
}

void WorldColorPipeline::present_linear() const {
  if (!m_linear_target.has_value()) {
    return;
  }

  // Automatic encoding remains disabled: the shader is the single OETF.
  glDisable(GL_FRAMEBUFFER_SRGB);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);

  Framebuffer::unbind();
  glViewport(0, 0, m_linear_target->width(), m_linear_target->height());
  draw_transfer(m_linear_target->color_texture(), false);

  Texture2D::unbind();
  glBindVertexArray(0);
  Shader::unbind();

  // Restore the normal scene baseline for the ImGui backend that follows.
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_STENCIL_TEST);
  glEnable(GL_CULL_FACE);
  glDisable(GL_BLEND);
}

int WorldColorPipeline::width() const {
  return m_legacy_target.has_value() ? m_legacy_target->width() : 0;
}

int WorldColorPipeline::height() const {
  return m_legacy_target.has_value() ? m_legacy_target->height() : 0;
}

}  // namespace App

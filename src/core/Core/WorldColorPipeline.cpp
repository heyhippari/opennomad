#include "Core/WorldColorPipeline.hpp"

#include <glad/glad.h>

#include <array>
#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "Core/ColorManagement.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Framebuffer.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"

namespace App {

namespace {

constexpr std::string_view K_FULLSCREEN_VERTEX_SHADER{R"glsl(
#version 410 core
out vec2 v_uv;
void main() {
  const vec2 positions[3] = vec2[3](
      vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
  vec2 position = positions[gl_VertexID];
  v_uv = (position * 0.5) + 0.5;
  gl_Position = vec4(position, 0.0, 1.0);
}
)glsl"};

constexpr std::string_view K_COMPOSITOR_FRAGMENT_SHADER{R"glsl(
#version 410 core
in vec2 v_uv;
uniform sampler2D u_scene;
uniform sampler2D u_accumulator;
uniform int u_operator;
out vec4 frag_color;
float srgb_to_linear(float encoded) {
  return encoded <= 0.04045 ? encoded / 12.92
                            : pow((encoded + 0.055) / 1.055, 2.4);
}
float linear_to_srgb(float linear) {
  return linear <= 0.0031308 ? 12.92 * linear
                             : (1.055 * pow(linear, 1.0 / 2.4)) - 0.055;
}
vec3 decode(vec3 encoded) {
  return vec3(srgb_to_linear(encoded.r), srgb_to_linear(encoded.g),
              srgb_to_linear(encoded.b));
}
vec3 encode(vec3 linear) {
  return vec3(linear_to_srgb(linear.r), linear_to_srgb(linear.g),
              linear_to_srgb(linear.b));
}
void main() {
  vec4 scene = texture(u_scene, v_uv);
  vec4 accumulator = texture(u_accumulator, v_uv);
  vec3 base = clamp(scene.rgb, vec3(0.0), vec3(1.0));
  vec3 excess = max(scene.rgb - base, vec3(0.0));
  vec3 destination_encoded = encode(base);
  vec3 result_encoded;
  vec3 result_excess;
  float result_alpha = scene.a;
  if (u_operator == 0) {
    float transmittance = 1.0 - clamp(accumulator.a, 0.0, 1.0);
    result_encoded = accumulator.rgb + (destination_encoded * transmittance);
    result_excess = excess * transmittance;
    result_alpha = accumulator.a + (scene.a * transmittance);
  } else if (u_operator == 1) {
    result_encoded = destination_encoded + accumulator.rgb;
    result_excess = excess;
  } else if (u_operator == 2) {
    vec3 factor = clamp(accumulator.rgb, vec3(0.0), vec3(1.0));
    result_encoded = destination_encoded * factor;
    result_excess = excess * factor;
  } else {
    result_encoded = max(destination_encoded - accumulator.rgb, vec3(0.0));
    result_excess = excess;
  }
  frag_color = vec4(decode(clamp(result_encoded, vec3(0.0), vec3(1.0))) +
                        result_excess,
                    result_alpha);
}
)glsl"};

constexpr std::string_view K_DISPLAY_FRAGMENT_SHADER{R"glsl(
#version 410 core
in vec2 v_uv;
uniform sampler2D u_scene;
out vec4 frag_color;
float linear_to_srgb(float linear) {
  return linear <= 0.0031308 ? 12.92 * linear
                             : (1.055 * pow(linear, 1.0 / 2.4)) - 0.055;
}
void main() {
  vec4 scene = texture(u_scene, v_uv);
  vec3 linear = clamp(scene.rgb, vec3(0.0), vec3(1.0));
  frag_color = vec4(linear_to_srgb(linear.r), linear_to_srgb(linear.g),
                    linear_to_srgb(linear.b), scene.a);
}
)glsl"};

}  // namespace

class WorldColorPipeline::Targets {
 public:
  static std::expected<std::unique_ptr<Targets>, std::string> create(
      const int width, const int height) {
    auto depth{DepthStencilBuffer::create(width, height, DepthStencilFormat::k_depth24_stencil8)};
    if (!depth) {
      return std::expected<std::unique_ptr<Targets>, std::string>{
          std::unexpect, "shared depth/stencil: " + std::move(depth).error()};
    }
    auto scene_a{Framebuffer::create_with_shared_depth(
        width, height, k_linear_scene_target_description, *depth)};
    if (!scene_a) {
      return std::expected<std::unique_ptr<Targets>, std::string>{
          std::unexpect, "linear scene A: " + std::move(scene_a).error()};
    }
    auto scene_b{Framebuffer::create_with_shared_depth(
        width, height, k_linear_scene_target_description, *depth)};
    if (!scene_b) {
      return std::expected<std::unique_ptr<Targets>, std::string>{
          std::unexpect, "linear scene B: " + std::move(scene_b).error()};
    }
    auto accumulator{Framebuffer::create_with_shared_depth(
        width, height, k_legacy_accumulator_target_description, *depth)};
    if (!accumulator) {
      return std::expected<std::unique_ptr<Targets>, std::string>{
          std::unexpect, "legacy accumulator: " + std::move(accumulator).error()};
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) -- private constructor.
    return std::unique_ptr<Targets>{new Targets(std::move(depth).value(),
        std::move(scene_a).value(),
        std::move(scene_b).value(),
        std::move(accumulator).value())};
  }

  [[nodiscard]] Framebuffer& current(const bool scene_a) {
    return scene_a ? m_scene_a : m_scene_b;
  }
  [[nodiscard]] const Framebuffer& current(const bool scene_a) const {
    return scene_a ? m_scene_a : m_scene_b;
  }
  [[nodiscard]] Framebuffer& alternate(const bool scene_a) {
    return scene_a ? m_scene_b : m_scene_a;
  }
  [[nodiscard]] Framebuffer& accumulator() {
    return m_accumulator;
  }
  [[nodiscard]] int width() const {
    return m_scene_a.width();
  }
  [[nodiscard]] int height() const {
    return m_scene_a.height();
  }

 private:
  Targets(
      DepthStencilBuffer depth, Framebuffer scene_a, Framebuffer scene_b, Framebuffer accumulator)
      : m_depth(std::move(depth)),
        m_scene_a(std::move(scene_a)),
        m_scene_b(std::move(scene_b)),
        m_accumulator(std::move(accumulator)) {}

  DepthStencilBuffer m_depth;
  Framebuffer m_scene_a;
  Framebuffer m_scene_b;
  Framebuffer m_accumulator;
};

std::expected<std::unique_ptr<WorldColorPipeline>, std::string> WorldColorPipeline::create() {
  auto compositor{Shader::create(K_FULLSCREEN_VERTEX_SHADER, K_COMPOSITOR_FRAGMENT_SHADER)};
  if (!compositor) {
    return std::expected<std::unique_ptr<WorldColorPipeline>, std::string>{
        std::unexpect, std::move(compositor).error()};
  }
  auto display{Shader::create(K_FULLSCREEN_VERTEX_SHADER, K_DISPLAY_FRAGMENT_SHADER)};
  if (!display) {
    return std::expected<std::unique_ptr<WorldColorPipeline>, std::string>{
        std::unexpect, std::move(display).error()};
  }
  GLuint vertex_array{0};
  glGenVertexArrays(1, &vertex_array);
  if (vertex_array == 0U) {
    return std::expected<std::unique_ptr<WorldColorPipeline>, std::string>{
        std::unexpect, "failed to create color-pipeline vertex array"};
  }
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) -- private constructor.
  return std::unique_ptr<WorldColorPipeline>{new WorldColorPipeline(
      std::move(compositor).value(), std::move(display).value(), vertex_array)};
}

WorldColorPipeline::WorldColorPipeline(
    Shader compositor_shader, Shader display_shader, const GLuint vertex_array)
    : m_compositor_shader(std::move(compositor_shader)),
      m_display_shader(std::move(display_shader)),
      m_vertex_array(vertex_array) {}

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
  if (m_targets != nullptr && m_targets->width() == width && m_targets->height() == height) {
    return {};
  }
  auto replacement{Targets::create(width, height)};
  if (!replacement) {
    return std::expected<void, std::string>{std::unexpect, std::move(replacement).error()};
  }
  m_targets = std::move(replacement).value();
  return {};
}

void WorldColorPipeline::begin_scene(const std::array<float, 4> encoded_clear_color) {
  if (m_targets == nullptr) {
    return;
  }
  m_current_scene_a = true;
  m_stats = {};
  m_targets->current(true).bind();
  glDisable(GL_FRAMEBUFFER_SRGB);
  glDisable(GL_DITHER);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_TRUE);
  glStencilMask(0xFFU);
  glViewport(0, 0, width(), height());
  glClearColor(ColorManagement::srgb_to_linear(encoded_clear_color.at(0)),
      ColorManagement::srgb_to_linear(encoded_clear_color.at(1)),
      ColorManagement::srgb_to_linear(encoded_clear_color.at(2)),
      encoded_clear_color.at(3));
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void WorldColorPipeline::draw_fullscreen(const Shader& shader) const {
  shader.bind();
  glBindVertexArray(m_vertex_array);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

void WorldColorPipeline::composite_legacy_stage(const LegacyBlendOperator blend_operator,
    const std::size_t source_draws,
    const std::function<void()>& draw_sources) {
  if (m_targets == nullptr || source_draws == 0U) {
    return;
  }
  m_targets->accumulator().bind();
  glViewport(0, 0, width(), height());
  glDisable(GL_FRAMEBUFFER_SRGB);
  glDisable(GL_DITHER);
  glDepthMask(GL_FALSE);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_STENCIL_TEST);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  if (blend_operator == LegacyBlendOperator::k_darken) {
    glClearColor(1.0F, 1.0F, 1.0F, 1.0F);
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
  } else {
    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    glBlendFunc(
        GL_ONE, blend_operator == LegacyBlendOperator::k_alpha ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE);
  }
  glClear(GL_COLOR_BUFFER_BIT);
  draw_sources();

  const Framebuffer& destination{m_targets->alternate(m_current_scene_a)};
  destination.bind();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  m_compositor_shader.bind();
  m_compositor_shader.set_uniform_int("u_scene", 0);
  m_compositor_shader.set_uniform_int("u_accumulator", 1);
  m_compositor_shader.set_uniform_int("u_operator", static_cast<int>(blend_operator));
  m_targets->current(m_current_scene_a).color_texture().bind(0);
  m_targets->accumulator().color_texture().bind(1);
  draw_fullscreen(m_compositor_shader);
  m_current_scene_a = !m_current_scene_a;
  m_stats.stages += 1U;
  m_stats.source_draws += source_draws;
  m_stats.composites += 1U;
  bind_current_scene();
}

void WorldColorPipeline::bind_current_scene() const {
  if (m_targets == nullptr) {
    return;
  }
  m_targets->current(m_current_scene_a).bind();
  glViewport(0, 0, width(), height());
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_STENCIL_TEST);
  glEnable(GL_CULL_FACE);
  glDisable(GL_BLEND);
}

void WorldColorPipeline::present_linear() const {
  if (m_targets == nullptr) {
    return;
  }
  glDisable(GL_FRAMEBUFFER_SRGB);
  glDisable(GL_DITHER);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
  Framebuffer::unbind();
  glViewport(0, 0, width(), height());
  m_display_shader.bind();
  m_display_shader.set_uniform_int("u_scene", 0);
  m_targets->current(m_current_scene_a).color_texture().bind(0);
  draw_fullscreen(m_display_shader);
  Texture2D::unbind();
  glBindVertexArray(0);
  Shader::unbind();
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_STENCIL_TEST);
  glEnable(GL_CULL_FACE);
  glDisable(GL_BLEND);
}

int WorldColorPipeline::width() const {
  return m_targets != nullptr ? m_targets->width() : 0;
}
int WorldColorPipeline::height() const {
  return m_targets != nullptr ? m_targets->height() : 0;
}
bool WorldColorPipeline::current_scene_is_a() const {
  return m_current_scene_a;
}
const LegacyBlendCompositorStats& WorldColorPipeline::stats() const {
  return m_stats;
}

}  // namespace App

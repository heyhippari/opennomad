#include "Core/WorldShaderSet.hpp"

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Shader.hpp"
#include "Core/WorldShaders.hpp"

namespace App {

namespace {

/// Blended world body; Runtime never fogs these but applies the far alpha fade.
constexpr std::string_view K_LEGACY_WORLD_FRAGMENT_BODY{R"glsl(
in vec2 v_uv;
in vec4 v_color;
in vec2 v_environment_uv;
uniform sampler2D u_texture0;
uniform sampler2D u_texture1;
uniform int u_environment_mapped;
uniform float u_alpha_test;
uniform float u_premultiply_alpha;
out vec4 frag_colour;
void main() {
    vec4 texel = texture(u_texture0, v_uv);
    if (u_alpha_test > 0.5 && texel.a < 0.5) {
        discard;
    }
    vec3 color = texel.rgb * v_color.rgb;
    if (u_environment_mapped != 0) {
        color *= texture(u_texture1, v_environment_uv).rgb;
    }
    vec4 source = vec4(color, texel.a * runtime_far_fade());
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

}  // namespace

std::expected<std::unique_ptr<WorldShaderSet>, std::string> WorldShaderSet::create() {
  const WorldShaderOptions original{};
  auto modern{
      Shader::create(world_vertex_shader(original), modern_world_fragment_shader(original))};
  if (!modern) {
    return std::expected<std::unique_ptr<WorldShaderSet>, std::string>{
        std::unexpect, std::move(modern).error()};
  }
  auto legacy{Shader::create(
      world_vertex_shader(original), faded_fragment_shader(K_LEGACY_WORLD_FRAGMENT_BODY))};
  if (!legacy) {
    return std::expected<std::unique_ptr<WorldShaderSet>, std::string>{
        std::unexpect, std::move(legacy).error()};
  }
  auto wireframe{Shader::create(K_WIREFRAME_VERTEX_SHADER, K_WIREFRAME_FRAGMENT_SHADER)};
  if (!wireframe) {
    return std::expected<std::unique_ptr<WorldShaderSet>, std::string>{
        std::unexpect, std::move(wireframe).error()};
  }

  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) -- private constructor.
  auto shaders{std::unique_ptr<WorldShaderSet>{new WorldShaderSet()}};
  shaders->m_modern = std::make_unique<Shader>(std::move(modern).value());
  shaders->m_legacy = std::make_unique<Shader>(std::move(legacy).value());
  shaders->m_wireframe = std::make_unique<Shader>(std::move(wireframe).value());
  return shaders;
}

const Shader* WorldShaderSet::per_pixel_modern() {
  if (m_per_pixel_modern == nullptr && !m_per_pixel_modern_failed) {
    const WorldShaderOptions options{.per_pixel_lighting = true};
    auto shader{
        Shader::create(world_vertex_shader(options), modern_world_fragment_shader(options))};
    if (shader) {
      m_per_pixel_modern = std::make_unique<Shader>(std::move(shader).value());
    } else {
      m_per_pixel_modern_failed = true;
      App::Log::warn(LogCategory::Renderer,
          "Per-pixel character lighting unavailable, using vertex lighting: {}",
          shader.error());
    }
  }
  return m_per_pixel_modern.get();
}

const Shader* WorldShaderSet::cubic_color_modern() {
  if (m_cubic_color_modern == nullptr && !m_cubic_color_modern_failed) {
    const WorldShaderOptions options{.cubic_vertex_color = true};
    auto shader{Shader::create(world_vertex_shader(options),
        K_WORLD_CUBIC_COLOR_GEOMETRY_SHADER,
        modern_world_fragment_shader(options))};
    if (shader) {
      m_cubic_color_modern = std::make_unique<Shader>(std::move(shader).value());
    } else {
      m_cubic_color_modern_failed = true;
      App::Log::warn(LogCategory::Renderer,
          "Cubic decor colour interpolation unavailable, using linear colours: {}",
          shader.error());
    }
  }
  return m_cubic_color_modern.get();
}

}  // namespace App

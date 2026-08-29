#include "Core/Video/VideoScene.hpp"

#include <glad/glad.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Mesh.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/Vertex.hpp"
#include "Core/Video/VideoPlayer.hpp"

namespace App::Video {

namespace {

std::vector<Vertex> make_quad_vertices() {
  return {
      Vertex{.position = {-1.0F, -1.0F, 0.0F}, .uv = {0.0F, 0.0F}},
      Vertex{.position = {1.0F, -1.0F, 0.0F}, .uv = {1.0F, 0.0F}},
      Vertex{.position = {1.0F, 1.0F, 0.0F}, .uv = {1.0F, 1.0F}},
      Vertex{.position = {-1.0F, 1.0F, 0.0F}, .uv = {0.0F, 1.0F}},
  };
}

std::vector<std::uint32_t> make_quad_indices() {
  return {0U, 1U, 2U, 0U, 2U, 3U};
}

/// Minimal textured-quad blit shader: only the contain-fit scale is applied;
/// the video otherwise remains a plain 2D image with no lighting or tinting.
Shader make_blit_shader() {
  // clang-format off
  static constexpr std::string_view k_vertex_source = R"glsl(
#version 410 core

layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv;

uniform vec2 u_scale;

out vec2 v_uv;

void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_position.xy * u_scale, a_position.z, 1.0);
}
)glsl";

  static constexpr std::string_view k_fragment_source = R"glsl(
#version 410 core

in vec2 v_uv;

uniform sampler2D u_texture0;

out vec4 frag_colour;

void main() {
    frag_colour = texture(u_texture0, v_uv);
}
)glsl";
  // clang-format on

  auto shader{Shader::create(k_vertex_source, k_fragment_source)};
  if (!shader) {
    throw std::runtime_error{"Failed to build video blit shader: " + shader.error()};
  }
  return std::move(shader).value();
}

}  // namespace

std::unique_ptr<VideoScene> VideoScene::create() {
  APP_PROFILE_FUNCTION();

  // The constructor is private; only the factory may build a presenter.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return std::unique_ptr<VideoScene>{new VideoScene(make_blit_shader())};
}

VideoScene::VideoScene(Shader shader)
    : m_shader(std::move(shader)),
      m_quad(make_quad_vertices(), make_quad_indices()) {}

void VideoScene::present_frame(
    const VideoFrame& frame, const int viewport_width, const int viewport_height) {
  APP_PROFILE_FUNCTION();

  if (frame.width <= 0 || frame.height <= 0 || frame.rgba.empty()) {
    return;
  }

  if (m_texture == nullptr || m_texture_width != frame.width || m_texture_height != frame.height) {
    auto texture{Texture2D::create(frame.width,
        frame.height,
        std::span<const std::uint8_t>{frame.rgba},
        TextureColorEncoding::k_linear)};
    if (!texture) {
      return;
    }
    m_texture = std::make_unique<Texture2D>(std::move(texture).value());
    m_texture_width = frame.width;
    m_texture_height = frame.height;
  } else {
    m_texture->update(std::span<const std::uint8_t>{frame.rgba});
  }

  // A video is a plain 2D blit: depth testing and back-face culling are
  // 3D-rendering concerns and would reject the fullscreen quad before the
  // main loop has ever cleared the depth buffer.
  const bool depth_test_was_enabled{glIsEnabled(GL_DEPTH_TEST) == GL_TRUE};
  const bool cull_face_was_enabled{glIsEnabled(GL_CULL_FACE) == GL_TRUE};
  const bool framebuffer_srgb_was_enabled{glIsEnabled(GL_FRAMEBUFFER_SRGB) == GL_TRUE};
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  if (framebuffer_srgb_was_enabled) {
    // FFmpeg produces display-referred RGB code values. A DirectDraw-style
    // video blit passes those values through unchanged instead of treating
    // them as linear light and applying the framebuffer's sRGB encoding.
    glDisable(GL_FRAMEBUFFER_SRGB);
  }

  glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT);

  const std::array<GLfloat, 2> scale{
      compute_contain_scale(frame.width, frame.height, viewport_width, viewport_height)};
  m_shader.bind();
  m_shader.set_uniform_vec2("u_scale", std::span<const GLfloat, 2>{scale});
  m_shader.set_uniform_int("u_texture0", 0);
  m_texture->bind(0);
  m_quad.draw();
  Texture2D::unbind();
  Shader::unbind();

  if (framebuffer_srgb_was_enabled) {
    glEnable(GL_FRAMEBUFFER_SRGB);
  }
  if (depth_test_was_enabled) {
    glEnable(GL_DEPTH_TEST);
  }
  if (cull_face_was_enabled) {
    glEnable(GL_CULL_FACE);
  }
}

std::array<float, 2> VideoScene::compute_contain_scale(const int frame_width,
    const int frame_height,
    const int viewport_width,
    const int viewport_height) {
  if (frame_width <= 0 || frame_height <= 0 || viewport_width <= 0 || viewport_height <= 0) {
    return {1.0F, 1.0F};
  }

  const float frame_aspect{static_cast<float>(frame_width) / static_cast<float>(frame_height)};
  const float viewport_aspect{
      static_cast<float>(viewport_width) / static_cast<float>(viewport_height)};

  if (frame_aspect > viewport_aspect) {
    // The frame is wider than the viewport: keep its full width and letterbox.
    return {1.0F, viewport_aspect / frame_aspect};
  }

  // The frame is taller or equal: keep its full height and pillarbox.
  return {frame_aspect / viewport_aspect, 1.0F};
}

}  // namespace App::Video

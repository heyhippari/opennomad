#include "Core/SplashScene.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>
#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Input/InputManager.hpp"
#include "Core/Omikron/BmpImage.hpp"
#include "Core/Resources.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/Vertex.hpp"

namespace App {

namespace {
/// Smooth 0..1 interpolation with zero slope at either end.
float smoothstep01(const float value) {
  const float x{std::clamp(value, 0.0F, 1.0F)};
  return x * x * (3.0F - (2.0F * x));
}

/// Reads a whole file into memory using SDL.
std::expected<std::vector<std::byte>, std::string> read_file(const std::filesystem::path& path) {
  // Windows ignores filename case; mirror that on case-sensitive filesystems
  // so the game data's inconsistent casing always resolves.
  const std::filesystem::path resolved_path{Resources::resolve_case_insensitive(path)};
  std::size_t size{0};
  void* raw{SDL_LoadFile(resolved_path.string().c_str(), &size)};
  if (raw == nullptr) {
    return std::expected<std::vector<std::byte>, std::string>{std::unexpect, SDL_GetError()};
  }

  std::vector<std::byte> bytes(size);
  if (size > 0) {
    std::memcpy(bytes.data(), raw, size);
  }
  SDL_free(raw);
  return bytes;
}

/// Unit quad in NDC: two triangles with CCW winding (back-face culling is
/// enabled globally), UVs mapping the texture to the full quad.
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

}  // namespace

std::expected<std::unique_ptr<SplashScene>, std::string> SplashScene::create(
    const float duration_seconds) {
  APP_PROFILE_FUNCTION();

  // The game stores the loading screen under IMAGES/OMIKRON.BMP; build the
  // path from components so the separators are correct on every platform.
  const std::filesystem::path splash_path{
      Resources::game_data_path(std::filesystem::path{"IMAGES"} / "OMIKRON.BMP")};
  auto file{read_file(splash_path)};
  if (!file) {
    return std::expected<std::unique_ptr<SplashScene>, std::string>{std::unexpect,
        fmt::format("Splash: cannot read '{}': {}", splash_path.string(), file.error())};
  }
  auto image{Omikron::BmpImageDecoder::load(*file)};
  if (!image) {
    return std::expected<std::unique_ptr<SplashScene>, std::string>{
        std::unexpect, fmt::format("Splash: {}", image.error())};
  }
  auto texture{Texture2D::create(
      image->width, image->height, std::span<const std::uint8_t>{image->rgba8}, true)};
  if (!texture) {
    return std::expected<std::unique_ptr<SplashScene>, std::string>{
        std::unexpect, fmt::format("Splash: {}", texture.error())};
  }

  // The default shader is a hardcoded textured quad; failure is a programmer
  // error, not a recoverable runtime condition.
  Shader shader{Shader::create_default()};

  // The constructor is private; only the factory may build a scene.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return std::unique_ptr<SplashScene>{
      new SplashScene(std::move(texture).value(), std::move(shader), duration_seconds)};
}

SplashScene::SplashScene(Texture2D texture, Shader shader, const float duration_seconds)
    : m_texture(std::move(texture)),
      m_shader(std::move(shader)),
      m_quad(make_quad_vertices(), make_quad_indices()),
      m_duration_seconds(std::max(0.0F, duration_seconds)) {}

void SplashScene::update(const float delta_time, const Input::InputManager& /*input*/) {
  // Application owns the authoritative countdown. This local clock drives
  // only the presentation and advances from the same frame delta.
  m_elapsed_seconds = std::min(m_duration_seconds, m_elapsed_seconds + std::max(0.0F, delta_time));
}

void SplashScene::render() {
  APP_PROFILE_FUNCTION();

  // Black behind the image so the contain-fit bars stay neutral.
  glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT);

  const std::array<float, 4> bounds{compute_contain_bounds(
      m_texture.width(), m_texture.height(), m_viewport_width, m_viewport_height)};
  const float half_width{(bounds.at(1) - bounds.at(0)) * 0.5F};
  const float half_height{(bounds.at(3) - bounds.at(2)) * 0.5F};
  // Column-major scale matrix mapping the unit quad onto the contain-fit rect.
  const std::array<GLfloat, 16> mvp{half_width,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      half_height,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F};
  // Fade from black during the first 0.75 s and back to black during the
  // final 0.75 s. Taking the smaller ramp naturally gives a full-intensity
  // plateau between them.
  const float fade_in{smoothstep01(m_elapsed_seconds / kFadeInDuration)};
  const float fade_out{smoothstep01((m_duration_seconds - m_elapsed_seconds) / kFadeOutDuration)};
  const float fade{std::min(fade_in, fade_out)};

  // The framebuffer has already been cleared to black, so modulating RGB is
  // sufficient for a black fade and avoids altering/restoring GL blend state.
  const std::array<GLfloat, 4> tint{fade, fade, fade, 1.0F};

  m_shader.bind();
  m_shader.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{mvp});
  m_shader.set_uniform_int("u_texture0", 0);
  m_shader.set_uniform_vec4("u_tint", std::span<const GLfloat, 4>{tint});
  m_texture.bind(0);
  m_quad.draw();
  Texture2D::unbind();
  Shader::unbind();
}

void SplashScene::resize(const int width, const int height) {
  m_viewport_width = width;
  m_viewport_height = height;
}

std::array<float, 4> SplashScene::compute_contain_bounds(const int image_width,
    const int image_height,
    const int viewport_width,
    const int viewport_height) {
  if (image_width <= 0 || image_height <= 0 || viewport_width <= 0 || viewport_height <= 0) {
    return {-1.0F, 1.0F, -1.0F, 1.0F};
  }

  const float image_aspect{static_cast<float>(image_width) / static_cast<float>(image_height)};
  const float viewport_aspect{
      static_cast<float>(viewport_width) / static_cast<float>(viewport_height)};

  float half_width{1.0F};
  float half_height{1.0F};
  if (image_aspect > viewport_aspect) {
    // The image is wider than the viewport: pillarbox, keep the full width.
    half_height = viewport_aspect / image_aspect;
  } else {
    // The image is taller or equal: letterbox, keep the full height.
    half_width = image_aspect / viewport_aspect;
  }
  return {-half_width, half_width, -half_height, half_height};
}

}  // namespace App

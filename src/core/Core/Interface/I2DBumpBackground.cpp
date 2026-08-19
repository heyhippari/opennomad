#include "Core/Interface/I2DBumpBackground.hpp"

// NOLINTBEGIN(misc-include-cleaner)
#include <glad/glad.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Interface/I2DBumpEffect.hpp"
#include "Core/Interface/I2DPresentation.hpp"
#include "Core/Log.hpp"
#include "Core/Omikron/IndexedBmp8.hpp"
#include "Core/Resources.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/TextureR8.hpp"
#include "Core/VertexArray.hpp"

namespace App::Interface {

namespace {

constexpr std::string_view K_CLOUD_PATH{"IMAGES/CLOUD.BMP"};

/// One original effect update per 1/30 s, matching the rest of OpenNomad's
/// recovered 30 Hz timing model. Runtime constants are per effect update.
constexpr float K_EFFECT_TICK_SECONDS{1.0F / 30.0F};
/// Largest backlog drained in one host frame (100 ms, mirroring the frame
/// timing model's k_max_dynamic_delta).
constexpr int K_MAX_TICKS_PER_UPDATE{3};

/// Fullscreen-triangle vertex shader: the background covers the whole
/// viewport, so geometry is generated from gl_VertexID (no vertex buffer).
constexpr std::string_view K_BUMP_VERTEX_SOURCE = R"glsl(
#version 410 core

void main() {
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

/// Fragment shader: the recovered final warp, evaluated at native framebuffer
/// resolution from the small 256x256 lit field and the per-row/per-column
/// warp lookup textures.
constexpr std::string_view K_BUMP_FRAGMENT_SOURCE = R"glsl(
#version 410 core

uniform sampler2D u_lit;        // R8 256x256 intensity (0..63)
uniform sampler2D u_palette;    // 64x1 RGBA8 sRGB ramp
uniform sampler2D u_row_warp;   // R8 pixel_height x 1
uniform sampler2D u_col_warp;   // R8 pixel_width x 1
uniform vec2 u_viewport_size;
uniform float u_logical_left;
uniform float u_scale;

out vec4 frag_colour;

void main() {
    vec2 frag = gl_FragCoord.xy;

    // Warp lookup values are precomputed per physical row/column on the CPU.
    float row_off = round(texture(u_row_warp, vec2(frag.y / u_viewport_size.y, 0.5)).r * 255.0);
    float col_off = round(texture(u_col_warp, vec2(frag.x / u_viewport_size.x, 0.5)).r * 255.0);

    float logical_x = u_logical_left + frag.x / u_scale;
    float logical_y = frag.y / u_scale;

    // Continuous extension of Runtime's wrapped 256x256 source coordinates.
    float src_x = mod(logical_x + col_off, 256.0);
    float src_y = mod(logical_y + row_off, 256.0);

    float intensity = round(texture(u_lit, vec2(src_x, src_y) / 256.0).r * 255.0);
    vec3 colour = texture(u_palette, vec2((intensity + 0.5) / 64.0, 0.5)).rgb;

    frag_colour = vec4(colour, 1.0);
}
)glsl";

/// Reads a whole file through the case-insensitive game-data resolver.
std::expected<std::vector<std::byte>, std::string> read_file(const std::string& relative_path) {
  const std::filesystem::path root_relative{Resources::game_data_path(
      std::filesystem::path{relative_path})};
  const std::filesystem::path resolved{Resources::resolve_case_insensitive(root_relative)};

  std::size_t size{0};
  void* raw{SDL_LoadFile(resolved.string().c_str(), &size)};
  if (raw == nullptr) {
    return std::expected<std::vector<std::byte>, std::string>{std::unexpect,
        fmt::format("cannot read '{}' (resolved '{}'): {}",
            relative_path,
            resolved.string(),
            SDL_GetError())};
  }

  std::vector<std::byte> bytes(size);
  if (size > 0) {
    std::memcpy(bytes.data(), raw, size);
  }
  SDL_free(raw);
  return bytes;
}

/// Builds a 64x1 RGBA8 palette texture from the recovered 64-entry ramp.
std::expected<Texture2D, std::string> create_palette_texture() {
  const auto& palette{I2DBumpEffect::palette()};
  constexpr std::size_t k_palette_texels{64};
  std::array<std::uint8_t, k_palette_texels * 4U> pixels{};
  for (std::size_t i{0}; i < palette.size(); ++i) {
    pixels.at((i * 4U) + 0U) = palette.at(i).at(0);
    pixels.at((i * 4U) + 1U) = palette.at(i).at(1);
    pixels.at((i * 4U) + 2U) = palette.at(i).at(2);
    pixels.at((i * 4U) + 3U) = 255U;
  }
  return Texture2D::create(64, 1, std::span<const std::uint8_t>{pixels}, /*srgb=*/true);
}

}  // namespace

I2DBumpBackground::I2DBumpBackground(I2DBumpEffect effect) : m_effect(std::move(effect)) {}

I2DBumpBackground::~I2DBumpBackground() = default;

std::expected<std::unique_ptr<I2DBumpBackground>, std::string> I2DBumpBackground::create() {
  APP_PROFILE_FUNCTION();

  auto file{read_file(std::string{K_CLOUD_PATH})};
  if (!file) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", file.error())};
  }

  auto bmp{Omikron::IndexedBmp8Decoder::load(std::span<const std::byte>{file.value()})};
  if (!bmp) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", bmp.error())};
  }
  const int width{bmp->width};
  const int height{bmp->height};

  auto effect{I2DBumpEffect::create(std::move(bmp).value())};
  if (!effect) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", effect.error())};
  }

  // The constructor is private; only the factory may build one.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  auto background{std::unique_ptr<I2DBumpBackground>{
      new I2DBumpBackground{std::move(effect).value()}}};

  // First 30 Hz effect update happens immediately when the menu opens, so
  // the lit field is defined before the first render().
  background->m_effect.advance_ticks(1);
  background->m_effect.regenerate_lighting();
  background->m_last_ticks = 1;

  auto lit{TextureR8::create(256, 256)};
  if (!lit) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", lit.error())};
  }
  background->m_lit_texture.emplace(std::move(lit).value());
  background->m_lit_texture.value().update(background->m_effect.lit_field());
  background->m_last_upload_bytes = background->m_effect.lit_field().size();

  auto palette{create_palette_texture()};
  if (!palette) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", palette.error())};
  }
  background->m_palette_texture.emplace(std::move(palette).value());

  auto shader{Shader::create(K_BUMP_VERTEX_SOURCE, K_BUMP_FRAGMENT_SOURCE)};
  if (!shader) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground shader: {}", shader.error())};
  }
  background->m_shader = std::make_unique<Shader>(std::move(shader).value());
  background->m_vertex_array = std::make_unique<VertexArray>();

  App::Log::info("[I2D] background: IMAGES/CLOUD.BMP ({}x{}) — recovered Runtime bump effect",
      width,
      height);

  return background;
}

void I2DBumpBackground::update(const float delta_time) {
  APP_PROFILE_FUNCTION();

  // Runtime's constants are per original effect update, not per second, so
  // the host frame rate must not scale them. Accumulate host time and drain
  // it in fixed 30 Hz ticks; cap the backlog so a long pause (debugger,
  // focus loss) does not trigger a runaway catch-up loop.
  m_tick_accumulator += delta_time;

  const std::uint32_t ticks{static_cast<std::uint32_t>(
      m_tick_accumulator / K_EFFECT_TICK_SECONDS)};
  const std::uint32_t capped_ticks{std::min(
      ticks, static_cast<std::uint32_t>(K_MAX_TICKS_PER_UPDATE))};

  if (capped_ticks > 0U) {
    // Advance the logical state by all missed ticks, then regenerate and
    // upload only the current lit field — never the invisible intermediate
    // catch-up frames the old implementation produced.
    {
      APP_PROFILE_SCOPE("I2D.Bump.AdvanceState");
      m_effect.advance_ticks(capped_ticks);
    }
    {
      APP_PROFILE_SCOPE("I2D.Bump.Lighting");
      m_effect.regenerate_lighting();
    }
    {
      APP_PROFILE_SCOPE("I2D.Bump.UploadLighting");
      if (m_lit_texture.has_value()) {
        m_lit_texture.value().update(m_effect.lit_field());
      }
    }
    m_last_ticks = capped_ticks;
    m_last_upload_bytes = m_effect.lit_field().size();
    m_warp_dirty = true;
    m_tick_accumulator -= static_cast<float>(capped_ticks) * K_EFFECT_TICK_SECONDS;
  } else {
    m_last_ticks = 0;
    m_last_upload_bytes = 0;
  }

  if (m_tick_accumulator >= K_EFFECT_TICK_SECONDS) {
    // Backlog still exceeds one tick after the cap: drop it rather than
    // replaying every missed tick.
    m_tick_accumulator = 0.0F;
  }
}

void I2DBumpBackground::rebuild_warp_tables(const I2DPresentationTransform& transform) {
  APP_PROFILE_SCOPE("I2D.Bump.WarpTables");

  const int width{transform.pixel_width};
  const int height{transform.pixel_height};
  const float scale{transform.pixels_per_reference_unit};
  const float logical_left{transform.logical_left};

  const double phase_a{m_effect.phase_a()};
  const double phase_b{m_effect.phase_b()};
  const double phase_c{m_effect.phase_c()};
  const double phase_d{m_effect.phase_d()};

  // Resize staging buffers and recreate the warp textures only when the
  // viewport dimensions changed.
  if (width != m_warp_width) {
    m_column_warp_cpu.assign(static_cast<std::size_t>(width), 0U);
    auto created{TextureR8::create(width, 1)};
    if (created) {
      m_column_warp_texture.emplace(std::move(created).value());
    }
  }
  if (height != m_warp_height) {
    m_row_warp_cpu.assign(static_cast<std::size_t>(height), 0U);
    auto created{TextureR8::create(height, 1)};
    if (created) {
      m_row_warp_texture.emplace(std::move(created).value());
    }
  }
  m_warp_width = width;
  m_warp_height = height;

  for (int row{0}; row < height; ++row) {
    const double logical_y{static_cast<double>(row) / static_cast<double>(scale)};
    m_row_warp_cpu.at(static_cast<std::size_t>(row)) =
        I2DBumpEffect::row_warp_offset(phase_a, phase_b, logical_y);
  }
  for (int column{0}; column < width; ++column) {
    const double logical_x{static_cast<double>(logical_left) +
                           (static_cast<double>(column) / static_cast<double>(scale))};
    m_column_warp_cpu.at(static_cast<std::size_t>(column)) =
        I2DBumpEffect::column_warp_offset(phase_c, phase_d, logical_x);
  }

  if (m_row_warp_texture.has_value()) {
    m_row_warp_texture->update(m_row_warp_cpu);
  }
  if (m_column_warp_texture.has_value()) {
    m_column_warp_texture->update(m_column_warp_cpu);
  }
}

void I2DBumpBackground::render(const I2DPresentationTransform& transform) {
  APP_PROFILE_SCOPE("I2D.Bump.Render");

  if (!m_shader || !m_lit_texture.has_value() || !m_palette_texture.has_value()) {
    return;
  }
  const TextureR8& lit{*m_lit_texture};
  const Texture2D& palette{*m_palette_texture};

  if (transform.pixel_width != m_warp_width || transform.pixel_height != m_warp_height ||
      m_warp_dirty) {
    rebuild_warp_tables(transform);
    m_warp_dirty = false;
  }

  m_shader->bind();
  m_shader->set_uniform_int("u_lit", 0);
  m_shader->set_uniform_int("u_palette", 1);
  m_shader->set_uniform_int("u_row_warp", 2);
  m_shader->set_uniform_int("u_col_warp", 3);
  const std::array<GLfloat, 2> viewport_size{
      static_cast<GLfloat>(transform.pixel_width),
      static_cast<GLfloat>(transform.pixel_height)};
  m_shader->set_uniform_vec2(
      "u_viewport_size", std::span<const GLfloat, 2>{viewport_size.data(), 2});
  m_shader->set_uniform_float("u_logical_left", transform.logical_left);
  m_shader->set_uniform_float("u_scale", transform.pixels_per_reference_unit);

  lit.bind(0);
  palette.bind(1);
  if (m_row_warp_texture.has_value()) {
    m_row_warp_texture.value().bind(2);
  }
  if (m_column_warp_texture.has_value()) {
    m_column_warp_texture.value().bind(3);
  }

  m_vertex_array->bind();
  glDrawArrays(GL_TRIANGLES, 0, 3);
  VertexArray::unbind();
  TextureR8::unbind();
  Texture2D::unbind();
}

}  // namespace App::Interface

// NOLINTEND(misc-include-cleaner)

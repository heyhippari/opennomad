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
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Interface/I2DBumpEffect.hpp"
#include "Core/Interface/I2DPresentation.hpp"
#include "Core/IntegerTexture.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/IndexedBmp8.hpp"
#include "Core/Resources.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/VertexArray.hpp"

namespace App::Interface {

namespace {

constexpr std::string_view K_CLOUD_PATH{"IMAGES/CLOUD.BMP"};

/// Fullscreen-triangle vertex shader: every GPU pass generates geometry from
/// gl_VertexID, so no vertex buffer is required.
constexpr std::string_view K_FULLSCREEN_VERTEX_SOURCE = R"glsl(
#version 410 core

void main() {
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

/// Gradient preprocessing: derive the static signed 8-bit X/Y gradients of the
/// 256x256 height field, using Runtime's wrapped-neighbour semantics and
/// signed-byte wrap. Runs once during initialization (GPU, not CPU).
constexpr std::string_view K_GRADIENT_FRAGMENT_SOURCE = R"glsl(
#version 410 core

uniform usampler2D u_height;

out ivec2 frag_gradient;

int signed_byte(int value) {
    int byte_value = value & 255;
    return (byte_value >= 128) ? (byte_value - 256) : byte_value;
}

void main() {
    ivec2 cell = ivec2(gl_FragCoord.xy);
    uint h0 = texelFetch(u_height, cell, 0).r;
    uint hx = texelFetch(u_height, ivec2((cell.x + 1) & 255, cell.y), 0).r;
    uint hy = texelFetch(u_height, ivec2(cell.x, (cell.y + 1) & 255), 0).r;
    int dx = signed_byte(int(h0) - int(hx));
    int dy = signed_byte(int(h0) - int(hy));
    frag_gradient = ivec2(dx, dy);
}
)glsl";

/// Row warp endpoint generation: for each physical row, evaluate the recovered
/// continuous row-warp extension at both endpoint states. R = offset at tick
/// N, G = offset at tick N+1. Run only when the tick or viewport changes.
///
/// Runtime warp generation: ~0x004B1F40 (tables consumed in reverse).
constexpr std::string_view K_ROW_WARP_FRAGMENT_SOURCE = R"glsl(
#version 410 core

uniform float u_pixel_height;
uniform float u_scale;
uniform float u_phase_a_n;
uniform float u_phase_b_n;
uniform float u_phase_a_n1;
uniform float u_phase_b_n1;

out uvec2 frag_warp;

uint row_warp_byte(float phase_a, float phase_b, float logical_y) {
    // i = 479 - logical_y; a = phase_a - 0.0043*i; b = phase_b + 0.0067*i;
    // offset = trunc(32.0*cos(b) + 64.0*cos(a)) & 0xFF.
    float i = 479.0 - logical_y;
    float a = phase_a - 0.0043 * i;
    float b = phase_b + 0.0067 * i;
    int value = int(32.0 * cos(b) + 64.0 * cos(a));
    return uint(value & 255);
}

void main() {
    // Map the fragment to its physical row. The row endpoint target is
    // pixel_height x 1, so gl_FragCoord.x indexes the bottom-up row and
    // gl_FragCoord.y is the single pixel row (0.5).
    float row_bottom = gl_FragCoord.x - 0.5;
    float physical_y_top = u_pixel_height - row_bottom - 1.0;
    float logical_y = physical_y_top / u_scale;

    uint n = row_warp_byte(u_phase_a_n, u_phase_b_n, logical_y);
    uint n1 = row_warp_byte(u_phase_a_n1, u_phase_b_n1, logical_y);
    frag_warp = uvec2(n, n1);
}
)glsl";

/// Column warp endpoint generation: see the row warp pass.
constexpr std::string_view K_COLUMN_WARP_FRAGMENT_SOURCE = R"glsl(
#version 410 core

uniform float u_scale;
uniform float u_logical_left;
uniform float u_phase_c_n;
uniform float u_phase_d_n;
uniform float u_phase_c_n1;
uniform float u_phase_d_n1;

out uvec2 frag_warp;

uint column_warp_byte(float phase_c, float phase_d, float logical_x) {
    // i = 639 - logical_x; c = phase_c + 0.0057*i; d = phase_d - 0.0099*i;
    // offset = trunc(48.0*(cos(c)+cos(d))) & 0xFF.
    float i = 639.0 - logical_x;
    float c = phase_c + 0.0057 * i;
    float d = phase_d - 0.0099 * i;
    int value = int(48.0 * (cos(c) + cos(d)));
    return uint(value & 255);
}

void main() {
    float col_index = gl_FragCoord.x - 0.5;
    float logical_x = u_logical_left + col_index / u_scale;

    uint n = column_warp_byte(u_phase_c_n, u_phase_d_n, logical_x);
    uint n1 = column_warp_byte(u_phase_c_n1, u_phase_d_n1, logical_x);
    frag_warp = uvec2(n, n1);
}
)glsl";

/// Final interpolated bump evaluation at native resolution. One fullscreen
/// draw per displayed frame. The temporal interpolation (wrapped warp offsets
/// + light position + bilinear gradient sampling) is an OpenNomad presentation
/// modernization, not recovered Runtime behavior.
constexpr std::string_view K_BACKGROUND_FRAGMENT_SOURCE = R"glsl(
#version 410 core

uniform isampler2D u_gradient;   // RG8I 256x256 signed gradients
uniform sampler2D u_palette;     // 64x1 legacy-encoded RGBA8 ramp
uniform usampler2D u_row_warp;   // RG8UI pixel_height x 1 (R=N, G=N+1)
uniform usampler2D u_col_warp;   // RG8UI pixel_width x 1 (R=N, G=N+1)
uniform vec2 u_viewport_size;
uniform float u_logical_left;
uniform float u_scale;
uniform float u_alpha;
uniform vec2 u_light;

out vec4 frag_colour;

// Circular (shortest-path) interpolation of modulo-256 warp offsets.
float wrapped_lerp_256(float a, float b, float alpha) {
    float delta = b - a;
    if (delta > 128.0) {
        delta -= 256.0;
    } else if (delta < -128.0) {
        delta += 256.0;
    }
    return a + delta * alpha;
}

// Manual bilinear sampling of the signed RG8I gradient field with wrapped
// neighbours. At an integer source coordinate this reduces exactly to the
// corresponding gradient texel (endpoint fidelity).
vec2 sample_gradient(vec2 source) {
    vec2 wrapped = mod(source, 256.0);
    vec2 base = floor(wrapped);
    vec2 frac = wrapped - base;
    ivec2 cell = ivec2(base) & 255;
    ivec2 right = (cell + ivec2(1, 0)) & 255;
    ivec2 down = (cell + ivec2(0, 1)) & 255;
    ivec2 diag = (cell + ivec2(1, 1)) & 255;

    vec2 g00 = vec2(texelFetch(u_gradient, cell, 0).xy);
    vec2 g10 = vec2(texelFetch(u_gradient, right, 0).xy);
    vec2 g01 = vec2(texelFetch(u_gradient, down, 0).xy);
    vec2 g11 = vec2(texelFetch(u_gradient, diag, 0).xy);

    vec2 lower = mix(g00, g10, frac.x);
    vec2 upper = mix(g01, g11, frac.x);
    return mix(lower, upper, frac.y);
}

void main() {
    // Pixel-centre mapping: at exactly 640x480 the top-left pixel maps to
    // logical (0, 0) and the bottom-right to (639, 479).
    float physical_x = gl_FragCoord.x - 0.5;
    float physical_y_top = u_viewport_size.y - gl_FragCoord.y - 0.5;
    float logical_x = u_logical_left + physical_x / u_scale;
    float logical_y = physical_y_top / u_scale;

    int row_index = int(gl_FragCoord.y - 0.5);
    int col_index = int(gl_FragCoord.x - 0.5);
    uvec2 row_pair = texelFetch(u_row_warp, ivec2(row_index, 0), 0).rg;
    uvec2 col_pair = texelFetch(u_col_warp, ivec2(col_index, 0), 0).rg;

    // Axis assignment matches the CPU oracle: source X receives the
    // row/Y-dependent offset and source Y the column/X-dependent offset.
    float row_off = wrapped_lerp_256(float(row_pair.r), float(row_pair.g), u_alpha);
    float col_off = wrapped_lerp_256(float(col_pair.r), float(col_pair.g), u_alpha);
    float src_x = mod(logical_x + row_off, 256.0);
    float src_y = mod(logical_y + col_off, 256.0);

    vec2 grad = sample_gradient(vec2(src_x, src_y));

    // Recovered bump-light formula with presentation interpolation:
    //   clamp(SAR5(dx*(x-lx) + dy*(y-ly)) + 32, 0, 63)
    // For the fractional float form, SAR5 corresponds to floor(value / 32.0)
    // (negative values round toward negative infinity, not zero).
    float dot_value = grad.x * (src_x - u_light.x) + grad.y * (src_y - u_light.y);
    int intensity = clamp(int(floor(dot_value / 32.0)) + 32, 0, 63);

    vec3 colour = texelFetch(u_palette, ivec2(intensity, 0), 0).rgb;
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
  return Texture2D::create(
      64, 1, std::span<const std::uint8_t>{pixels}, TextureColorEncoding::k_legacy_encoded);
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// RenderTarget (integer texture + framebuffer)
// ─────────────────────────────────────────────────────────────────────────────

I2DBumpBackground::RenderTarget::RenderTarget(RenderTarget&& other) noexcept
    : texture(std::move(other.texture)),
      framebuffer(std::exchange(other.framebuffer, 0)),
      width(other.width),
      height(other.height) {}

I2DBumpBackground::RenderTarget& I2DBumpBackground::RenderTarget::operator=(
    RenderTarget&& other) noexcept {
  if (this != &other) {
    if (framebuffer != 0) {
      glDeleteFramebuffers(1, &framebuffer);
    }
    texture = std::move(other.texture);
    framebuffer = std::exchange(other.framebuffer, 0);
    width = other.width;
    height = other.height;
  }
  return *this;
}

I2DBumpBackground::RenderTarget::~RenderTarget() {
  APP_PROFILE_FUNCTION();

  if (framebuffer != 0) {
    glDeleteFramebuffers(1, &framebuffer);
  }
}

void I2DBumpBackground::RenderTarget::bind() const {
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
}

void I2DBumpBackground::RenderTarget::unbind() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

std::expected<I2DBumpBackground::RenderTarget, std::string>
I2DBumpBackground::create_render_target(const int width,
    const int height,
    const IntegerFormat format) {
  auto texture{IntegerTexture::create(width, height, format)};
  if (!texture) {
    return std::expected<RenderTarget, std::string>{std::unexpect,
        std::move(texture).error()};
  }

  GLuint framebuffer{0};
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture->id(), 0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);

  const GLenum status{glCheckFramebufferStatus(GL_FRAMEBUFFER)};
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    glDeleteFramebuffers(1, &framebuffer);
    return std::expected<RenderTarget, std::string>{
        std::unexpect, fmt::format("bump render target incomplete: status 0x{:x}", status)};
  }

  return RenderTarget{IntegerTexture{std::move(texture).value()},
      framebuffer,
      width,
      height};
}

// ─────────────────────────────────────────────────────────────────────────────
// I2DBumpBackground
// ─────────────────────────────────────────────────────────────────────────────

I2DBumpBackground::I2DBumpBackground(std::vector<std::uint8_t> height_indices)
    : m_height_indices(std::move(height_indices)) {}

I2DBumpBackground::~I2DBumpBackground() = default;

void I2DBumpBackground::set_interpolated(const bool interpolated) {
  m_mode = interpolated ? BumpAnimationMode::k_interpolated : BumpAnimationMode::k_stepped;
}

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
  if (width != 256 || height != 256) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: expected 256x256, got {}x{}", width, height)};
  }

  // The constructor is private; only the factory may build one.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  auto background{std::unique_ptr<I2DBumpBackground>{
      new I2DBumpBackground{std::move(bmp->indices)}}};

  background->m_vertex_array = std::make_unique<VertexArray>();

  auto gradient_shader{Shader::create(K_FULLSCREEN_VERTEX_SOURCE, K_GRADIENT_FRAGMENT_SOURCE)};
  if (!gradient_shader) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground gradient shader: {}", gradient_shader.error())};
  }
  background->m_gradient_shader = std::make_unique<Shader>(std::move(gradient_shader).value());

  auto row_warp_shader{Shader::create(K_FULLSCREEN_VERTEX_SOURCE, K_ROW_WARP_FRAGMENT_SOURCE)};
  if (!row_warp_shader) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect,
        fmt::format("I2DBumpBackground row-warp shader: {}", row_warp_shader.error())};
  }
  background->m_row_warp_shader = std::make_unique<Shader>(std::move(row_warp_shader).value());

  auto col_warp_shader{Shader::create(K_FULLSCREEN_VERTEX_SOURCE, K_COLUMN_WARP_FRAGMENT_SOURCE)};
  if (!col_warp_shader) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect,
        fmt::format("I2DBumpBackground column-warp shader: {}", col_warp_shader.error())};
  }
  background->m_col_warp_shader = std::make_unique<Shader>(std::move(col_warp_shader).value());

  auto background_shader{Shader::create(K_FULLSCREEN_VERTEX_SOURCE, K_BACKGROUND_FRAGMENT_SOURCE)};
  if (!background_shader) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect,
        fmt::format("I2DBumpBackground shader: {}", background_shader.error())};
  }
  background->m_background_shader = std::make_unique<Shader>(std::move(background_shader).value());

  if (auto result{background->build_static_resources()}; !result) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", result.error())};
  }

  auto palette{create_palette_texture()};
  if (!palette) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", palette.error())};
  }
  background->m_palette.emplace(std::move(palette).value());

  // The first 30 Hz effect update happens immediately when the menu opens, so
  // the first rendered frame corresponds to tick 1 (matching the previous
  // hybrid renderer). current = tick 1, next = tick 2.
  advance_endpoint(background->m_current);
  background->m_next = background->m_current;
  advance_endpoint(background->m_next);
  background->m_timeline.current_tick = 1;

  App::Log::debug(LogCategory::I2D,
      "background: IMAGES/CLOUD.BMP ({}x{}) — GPU bump renderer (30 Hz timeline)",
      width,
      height);

  return background;
}

std::expected<void, std::string> I2DBumpBackground::build_static_resources() {
  APP_PROFILE_FUNCTION();

  // Runtime uses CLOUD.BMP's raw indexed bytes as height values. Upload them
  // as an integer R8UI texture — no sRGB, no normalized colour, no palette
  // RGB conversion — so the shader sees exact 0..255 heights.
  auto height{IntegerTexture::create_with_data(256,
      256,
      IntegerFormat::k_r8ui,
      std::span<const std::uint8_t>{m_height_indices})};
  if (!height) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("height texture: {}", height.error())};
  }
  m_height = std::move(height).value();

  auto gradient{create_render_target(256, 256, IntegerFormat::k_rg8i)};
  if (!gradient) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("gradient target: {}", gradient.error())};
  }
  m_gradient.emplace(std::move(gradient).value());

  // One GPU preprocessing pass over the 256x256 height field.
  glDisable(GL_BLEND);
  m_gradient->bind();
  glViewport(0, 0, 256, 256);
  m_gradient_shader->bind();
  m_gradient_shader->set_uniform_int("u_height", 0);
  m_height.bind(0);
  m_vertex_array->bind();
  glDrawArrays(GL_TRIANGLES, 0, 3);
  VertexArray::unbind();
  Shader::unbind();
  IntegerTexture::unbind();
  RenderTarget::unbind();
  glEnable(GL_BLEND);

  return {};
}

void I2DBumpBackground::update(const float delta_time) {
  APP_PROFILE_FUNCTION();

  // Feed host time into the 30 Hz timeline. When a tick boundary is crossed,
  // advance only the scalar endpoint state (a handful of doubles/ints) — no
  // per-pixel catch-up work. The interpolated render then lands on the
  // correct tick + alpha and renders once.
  const BumpTimelineAdvance advance{
      advance_bump_timeline(m_timeline, m_remainder_seconds, static_cast<double>(delta_time))};
  if (advance.ticks_advanced > 0U) {
    advance_endpoint(m_current, advance.ticks_advanced);
    m_next = m_current;
    advance_endpoint(m_next, 1U);
    m_warp_dirty = true;
  }
  m_timeline = advance.state;
  m_last_ticks = advance.ticks_advanced;
}

void I2DBumpBackground::rebuild_warp_textures(const I2DPresentationTransform& transform) {
  APP_PROFILE_SCOPE("I2D.Bump.WarpTables");

  const int width{transform.pixel_width};
  const int height{transform.pixel_height};
  const float scale{transform.pixels_per_reference_unit};
  const float logical_left{transform.logical_left};

  // Recreate the endpoint targets only when the viewport dimensions changed.
  if (width != m_warp_width) {
    auto created{create_render_target(width, 1, IntegerFormat::k_rg8ui)};
    if (created) {
      m_column_warp.emplace(std::move(created).value());
    }
  }
  if (height != m_warp_height) {
    auto created{create_render_target(height, 1, IntegerFormat::k_rg8ui)};
    if (created) {
      m_row_warp.emplace(std::move(created).value());
    }
  }
  m_warp_width = width;
  m_warp_height = height;

  if (!m_row_warp || !m_column_warp || !m_row_warp_shader || !m_col_warp_shader) {
    return;
  }

  glDisable(GL_BLEND);

  // Row warp: pixel_height x 1 (one fragment per physical row).
  {
    APP_PROFILE_SCOPE("I2D.Bump.WarpRowPass");
    m_row_warp->bind();
    glViewport(0, 0, height, 1);
    m_row_warp_shader->bind();
    m_row_warp_shader->set_uniform_float("u_pixel_height", static_cast<float>(height));
    m_row_warp_shader->set_uniform_float("u_scale", scale);
    m_row_warp_shader->set_uniform_float("u_phase_a_n", static_cast<float>(m_current.phase_a));
    m_row_warp_shader->set_uniform_float("u_phase_b_n", static_cast<float>(m_current.phase_b));
    m_row_warp_shader->set_uniform_float("u_phase_a_n1", static_cast<float>(m_next.phase_a));
    m_row_warp_shader->set_uniform_float("u_phase_b_n1", static_cast<float>(m_next.phase_b));
    m_vertex_array->bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    VertexArray::unbind();
    ++m_last_warp_passes;
  }

  // Column warp: pixel_width x 1 (one fragment per physical column).
  {
    APP_PROFILE_SCOPE("I2D.Bump.WarpColumnPass");
    m_column_warp->bind();
    glViewport(0, 0, width, 1);
    m_col_warp_shader->bind();
    m_col_warp_shader->set_uniform_float("u_scale", scale);
    m_col_warp_shader->set_uniform_float("u_logical_left", logical_left);
    m_col_warp_shader->set_uniform_float("u_phase_c_n", static_cast<float>(m_current.phase_c));
    m_col_warp_shader->set_uniform_float("u_phase_d_n", static_cast<float>(m_current.phase_d));
    m_col_warp_shader->set_uniform_float("u_phase_c_n1", static_cast<float>(m_next.phase_c));
    m_col_warp_shader->set_uniform_float("u_phase_d_n1", static_cast<float>(m_next.phase_d));
    m_vertex_array->bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    VertexArray::unbind();
    ++m_last_warp_passes;
  }

  RenderTarget::unbind();
  Shader::unbind();
  glEnable(GL_BLEND);
}

void I2DBumpBackground::render(const I2DPresentationTransform& transform) {
  APP_PROFILE_SCOPE("I2D.Bump.Render");

  m_last_warp_passes = 0;
  m_last_draw_calls = 0;

  if (!m_background_shader) {
    return;
  }

  const int width{transform.pixel_width};
  const int height{transform.pixel_height};

  if (width != m_warp_width || height != m_warp_height || m_warp_dirty) {
    rebuild_warp_textures(transform);
    m_warp_dirty = false;
  }

  // Bind locals immediately after the guarded check so the accesses below are
  // provably safe (a rebuild may have just replaced the warp targets).
  if (!m_gradient || !m_palette || !m_row_warp || !m_column_warp) {
    return;
  }
  const RenderTarget& gradient{*m_gradient};
  const Texture2D& palette{*m_palette};
  const RenderTarget& row_warp{*m_row_warp};
  const RenderTarget& col_warp{*m_column_warp};

  // Final draw into the default framebuffer at the full viewport.
  RenderTarget::unbind();
  glViewport(0, 0, width, height);

  // Stepped mode pins alpha to 0 so the effect advances only at authentic
  // 30 Hz boundaries.
  const float alpha{interpolated() ? m_timeline.alpha : 0.0F};
  const float light_x{std::lerp(static_cast<float>(m_current.light_x),
      static_cast<float>(m_next.light_x),
      alpha)};
  const float light_y{std::lerp(static_cast<float>(m_current.light_y),
      static_cast<float>(m_next.light_y),
      alpha)};

  m_background_shader->bind();
  m_background_shader->set_uniform_int("u_gradient", 0);
  m_background_shader->set_uniform_int("u_palette", 1);
  m_background_shader->set_uniform_int("u_row_warp", 2);
  m_background_shader->set_uniform_int("u_col_warp", 3);
  const std::array<GLfloat, 2> viewport_size{static_cast<GLfloat>(width),
      static_cast<GLfloat>(height)};
  m_background_shader->set_uniform_vec2(
      "u_viewport_size", std::span<const GLfloat, 2>{viewport_size.data(), 2});
  m_background_shader->set_uniform_float("u_logical_left", transform.logical_left);
  m_background_shader->set_uniform_float("u_scale", transform.pixels_per_reference_unit);
  m_background_shader->set_uniform_float("u_alpha", alpha);
  const std::array<GLfloat, 2> light{light_x, light_y};
  m_background_shader->set_uniform_vec2("u_light", std::span<const GLfloat, 2>{light.data(), 2});

  gradient.texture.bind(0);
  palette.bind(1);
  row_warp.texture.bind(2);
  col_warp.texture.bind(3);

  glDisable(GL_BLEND);
  m_vertex_array->bind();
  glDrawArrays(GL_TRIANGLES, 0, 3);
  VertexArray::unbind();
  glEnable(GL_BLEND);

  Shader::unbind();
  IntegerTexture::unbind();
  Texture2D::unbind();

  m_last_draw_calls = 1;
}

std::size_t I2DBumpBackground::debug_compare_gradient() const {
  APP_PROFILE_FUNCTION();

  if (!m_gradient) {
    App::Log::debug(LogCategory::I2D, "debug gradient compare: gradient texture not built");
    return std::numeric_limits<std::size_t>::max();
  }

  Omikron::IndexedBmp8 source{.width = 256, .height = 256, .indices = m_height_indices};
  auto reference{I2DBumpEffect::create(std::move(source))};
  if (!reference) {
    App::Log::debug(LogCategory::I2D, "debug gradient compare: {}", reference.error());
    return std::numeric_limits<std::size_t>::max();
  }

  // Read back the GPU-generated signed gradient field.
  std::vector<std::int8_t> gpu(static_cast<std::size_t>(65536U) * 2U, 0);
  m_gradient->texture.bind(0);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RG_INTEGER, GL_BYTE, gpu.data());
  IntegerTexture::unbind();

  std::size_t mismatches{0};
  for (int cell_y{0}; cell_y < 256; ++cell_y) {
    for (int cell_x{0}; cell_x < 256; ++cell_x) {
      const std::size_t index{((static_cast<std::size_t>(cell_y) * 256U) +
                               static_cast<std::size_t>(cell_x)) *
                              2U};
      if (gpu.at(index) != reference->gradient_x(static_cast<std::size_t>(cell_x),
                                    static_cast<std::size_t>(cell_y)) ||
          gpu.at(index + 1U) != reference->gradient_y(static_cast<std::size_t>(cell_x),
                                       static_cast<std::size_t>(cell_y))) {
        ++mismatches;
      }
    }
  }

  App::Log::debug(LogCategory::I2D,
      "debug gradient compare: {} mismatches of 65536 cells",
      mismatches);
  return mismatches;
}

}  // namespace App::Interface

// NOLINTEND(misc-include-cleaner)

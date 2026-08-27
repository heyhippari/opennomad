#include "Core/Interface/I2DRenderer.hpp"

// NOLINTBEGIN(misc-include-cleaner)
// glm follows a single-include convention (see ModelViewerScene.cpp); clang-tidy
// cannot trace each symbol back to a direct sub-header.
#include <fmt/format.h>
#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Buffers.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/Interface/DialogTextLayout.hpp"
#include "Core/Interface/FontManager.hpp"
#include "Core/Interface/I2DBumpBackground.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Interface/I2DPresentation.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/Vertex.hpp"
#include "Core/VertexArray.hpp"

namespace App::Interface {

namespace {

// clang-format off
/// I2D vertex shader: same inputs as the default textured shader, plus the
/// per-vertex tint colour (used so selected/unselected text glyphs share one
/// batch while carrying distinct colours).
constexpr std::string_view K_I2D_VERTEX_SOURCE = R"glsl(
#version 410 core

layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;

uniform mat4 u_mvp;

out vec2 v_uv;
out vec4 v_color;

void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)glsl";

/// I2D fragment shader: textured + per-vertex tinted, with an optional
/// per-draw source colour key. Runtime bitmaps perform DirectDraw source-keyed
/// blits, which match the source pixel value exactly in a 16-bit RGB555
/// surface; the key "pixel value 0" therefore also matches near-black palette
/// entries (e.g. the (4,4,4) bitmap background) whose 5-bit channels all
/// truncate to zero.
constexpr std::string_view K_I2D_FRAGMENT_SOURCE = R"glsl(
#version 410 core

in vec2 v_uv;
in vec4 v_color;

uniform sampler2D u_texture0;

uniform int u_source_colour_key_enabled;
uniform vec3 u_source_colour_key;

out vec4 frag_colour;

void main() {
    vec4 texel = texture(u_texture0, v_uv);

    if (u_source_colour_key_enabled != 0) {
        // Recovered DirectDraw semantics: exact comparison of the 5-bit
        // (RGB555) pixel value against the key. Quantising the sRGB source
        // bytes to 5 bits makes the near-black background (4,4,4 -> 0) key
        // out without any chroma-key threshold.
        ivec3 key = ivec3(u_source_colour_key * 255.0 + 0.5) >> 3;
        ivec3 pixel = ivec3(texel.rgb * 255.0 + 0.5) >> 3;
        if (all(equal(pixel, key))) {
            discard;
        }
    }

    frag_colour = texel * v_color;
    }
)glsl";

/// Full-screen triangle used for interface lifecycle presentation hints.
constexpr std::string_view K_OVERLAY_VERTEX_SOURCE = R"glsl(
#version 410 core

void main() {
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

constexpr std::string_view K_OVERLAY_FRAGMENT_SOURCE = R"glsl(
#version 410 core

uniform vec3 u_color;
uniform float u_alpha;
out vec4 frag_colour;

void main() {
    frag_colour = vec4(u_color, u_alpha);
}
)glsl";
// clang-format on

/// Converts an authored display-encoded byte to its unchanged normalized
/// numeric value for encoded-domain Runtime composition.
float encoded_from_byte(const std::uint8_t byte) {
  return static_cast<float>(byte) / 255.0F;
}

/// Minimal UTF-8 decoder: advances `cursor` past one codepoint and returns
/// it (U+FFFD for malformed sequences).
unsigned int decode_utf8(const char*& cursor, const char* end) {
  const auto first{static_cast<unsigned char>(*cursor)};
  if (first < 0x80U) {
    ++cursor;
    return first;
  }
  unsigned int codepoint{0};
  int extra{0};
  if ((first & 0xE0U) == 0xC0U) {
    codepoint = first & 0x1FU;
    extra = 1;
  } else if ((first & 0xF0U) == 0xE0U) {
    codepoint = first & 0x0FU;
    extra = 2;
  } else if ((first & 0xF8U) == 0xF0U) {
    codepoint = first & 0x07U;
    extra = 3;
  } else {
    ++cursor;
    return 0xFFFDU;
  }
  for (int index{0}; index < extra; ++index) {
    ++cursor;
    if (cursor >= end) {
      return 0xFFFDU;
    }
    codepoint = (codepoint << 6U) | (static_cast<unsigned char>(*cursor) & 0x3FU);
  }
  ++cursor;
  return codepoint;
}

}  // namespace

std::expected<void, std::string> I2DRenderer::initialize() {
  APP_PROFILE_FUNCTION();

  auto shader{Shader::create(K_I2D_VERTEX_SOURCE, K_I2D_FRAGMENT_SOURCE)};
  if (!shader) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("I2D renderer shader: {}", shader.error())};
  }
  m_shader = std::make_unique<Shader>(std::move(shader).value());

  auto overlay_shader{Shader::create(K_OVERLAY_VERTEX_SOURCE, K_OVERLAY_FRAGMENT_SOURCE)};
  if (!overlay_shader) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("I2D presentation overlay shader: {}", overlay_shader.error())};
  }
  m_overlay_shader = std::make_unique<Shader>(std::move(overlay_shader).value());

  m_vertex_buffer = std::make_unique<VertexBuffer>(std::span<const std::byte>{}, GL_DYNAMIC_DRAW);
  m_index_buffer = std::make_unique<IndexBuffer>(std::span<const std::uint32_t>{}, GL_DYNAMIC_DRAW);
  m_vertex_array = std::make_unique<VertexArray>();

  m_vertex_array->bind();
  m_vertex_buffer->bind();

  const GLsizei stride{static_cast<GLsizei>(sizeof(Vertex))};
  glEnableVertexAttribArray(Vertex::k_position_location);
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
  glVertexAttribPointer(Vertex::k_position_location,
      3,
      GL_FLOAT,
      GL_FALSE,
      stride,
      reinterpret_cast<const void*>(offsetof(Vertex, position)));
  glEnableVertexAttribArray(Vertex::k_normal_location);
  glVertexAttribPointer(Vertex::k_normal_location,
      3,
      GL_FLOAT,
      GL_FALSE,
      stride,
      reinterpret_cast<const void*>(offsetof(Vertex, normal)));
  glEnableVertexAttribArray(Vertex::k_uv_location);
  glVertexAttribPointer(Vertex::k_uv_location,
      2,
      GL_FLOAT,
      GL_FALSE,
      stride,
      reinterpret_cast<const void*>(offsetof(Vertex, uv)));
  glEnableVertexAttribArray(Vertex::k_color_location);
  glVertexAttribPointer(Vertex::k_color_location,
      4,
      GL_FLOAT,
      GL_FALSE,
      stride,
      reinterpret_cast<const void*>(offsetof(Vertex, color)));
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)

  m_index_buffer->bind();
  VertexArray::unbind();
  VertexBuffer::unbind();
  IndexBuffer::unbind();

  // A practical initial reserve so the batch buffers never reallocate during
  // normal menu rendering.
  m_vertices.reserve(1024);
  m_indices.reserve(1536);
  m_commands.reserve(8);

  // Runtime volume sliders use a green->yellow->red horizontal colour ramp.
  // Build it once as ordinary I2D texture data; later slider rows only crop
  // the U range to their normalized value.
  constexpr std::size_t k_slider_texel_count{256U};
  std::array<std::uint8_t, k_slider_texel_count * 4U> slider_pixels{};
  for (std::size_t index{0}; index < k_slider_texel_count; ++index) {
    const float position{
        static_cast<float>(index) / static_cast<float>(k_slider_texel_count - 1U)};
    const float red{std::min(position * 2.0F, 1.0F)};
    const float green{std::min((1.0F - position) * 2.0F, 1.0F)};
    const std::size_t pixel{index * 4U};
    slider_pixels.at(pixel) = static_cast<std::uint8_t>(red * 255.0F);
    slider_pixels.at(pixel + 1U) = static_cast<std::uint8_t>(green * 255.0F);
    slider_pixels.at(pixel + 2U) = 0U;
    slider_pixels.at(pixel + 3U) = 255U;
  }
  auto slider_texture{Texture2D::create(static_cast<int>(k_slider_texel_count),
      1,
      std::span<const std::uint8_t>{slider_pixels},
      TextureColorEncoding::k_legacy_encoded,
      TextureFilter::k_linear)};
  if (!slider_texture) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("I2D option slider texture: {}", slider_texture.error())};
  }
  m_option_slider_texture.emplace(std::move(slider_texture).value());

  m_initialized = true;
  return {};
}

void I2DRenderer::render(const InterfaceInstance& instance,
    FontManager& fonts,
    const int pixel_width,
    const int pixel_height,
    Debug::I2DCounters& counters) {
  APP_PROFILE_FUNCTION();

  if (!m_initialized || instance.current_state == nullptr) {
    return;
  }

  const I2DPresentationTransform transform{make_presentation_transform(pixel_width, pixel_height)};

  // The recovered 640x480 reference canvas fills the entire drawable
  // framebuffer; no 4:3 letterboxing.
  glViewport(0, 0, pixel_width, pixel_height);

  // The I2D pass is a flat 2D composition: no depth, no culling, alpha blend.
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // 1. Animated background, when the current state has one. It owns its own
  // shader and draws over the full viewport.
  if (instance.current_state->background != nullptr) {
    I2DBumpBackground& background{*instance.current_state->background};
    background.render(transform);
    counters.draw_calls += 1;
    counters.background_ticks = background.last_ticks();
    counters.background_bytes_uploaded = background.last_upload_bytes();
    counters.background_tick = background.current_tick();
    counters.background_alpha = background.alpha();
    counters.background_warp_passes = background.last_warp_passes();
    counters.background_draw_calls = background.last_draw_calls();
    counters.background_interpolated = background.interpolated();
  }

  // 2. Interface bitmap artwork + text, batched by texture/blit state.
  m_shader->bind();
  m_shader->set_uniform_mat4(
      "u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(transform.projection), 16});
  m_shader->set_uniform_int("u_texture0", 0);

  reset();

  // Selection ordinal over the current state's selectable text elements.
  const std::vector<I2DTextElement*> selectable{selectable_text_elements(*instance.current_state)};
  std::size_t selectable_ordinal{0};

  for (const I2DGroup& group : instance.current_state->groups) {
    for (const I2DElement& element : group.elements) {
      if (const auto* bitmap{std::get_if<I2DBitmapElement>(&element.data)}) {
        if (!instance.bitmap.has_value()) {
          continue;
        }
        const Texture2D& texture{*instance.bitmap};
        const float texture_width{static_cast<float>(texture.width())};
        const float texture_height{static_cast<float>(texture.height())};
        const float u0{static_cast<float>(bitmap->source.x) / texture_width};
        const float u1{static_cast<float>(bitmap->source.x + bitmap->source.width) / texture_width};
        const float v_top{1.0F - (static_cast<float>(bitmap->source.y) / texture_height)};
        const float v_bottom{
            1.0F - (static_cast<float>(bitmap->source.y + bitmap->source.height) / texture_height)};

        float x0{static_cast<float>(bitmap->destination.x)};
        float y0{static_cast<float>(bitmap->destination.y)};
        float x1{static_cast<float>(bitmap->destination.x + bitmap->destination.width)};
        float y1{static_cast<float>(bitmap->destination.y + bitmap->destination.height)};

        // Interface-specific presentation hints (e.g. the main-menu logo is
        // top-centred with a small modernization margin). Recovered Runtime
        // coordinates are not mutated; the adjustment is applied here.
        if (element.presentation.anchor_top_center) {
          const I2DTopCenterPlacement placement{compute_top_center_placement(bitmap->destination,
              element.presentation.top_margin_reference,
              element.presentation.clamp_width_to_viewport,
              pixel_width,
              pixel_height,
              element.presentation.top_center_scale)};
          x0 = placement.x0;
          y0 = placement.y0;
          x1 = placement.x1;
          y1 = placement.y1;
        }

        push_quad(texture,
            x0,
            y0,
            x1,
            y1,
            u0,
            v_top,
            u1,
            v_bottom,
            std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F},
            resolve_bitmap_blit_options(*bitmap));
        counters.quads += 1;
      } else if (const auto* text{std::get_if<I2DTextElement>(&element.data)}) {
        const bool selectable_text{text->selectable()};
        const bool selected{
            selectable_text && selectable_ordinal == instance.current_state->selected_element};
        if (selectable_text) {
          ++selectable_ordinal;
        }

        const std::string_view label{text->literal_text.empty()
                                         ? instance.strings.at(text->string_index)
                                         : std::string_view{text->literal_text}};
        if (label.empty()) {
          continue;
        }
        const FontResource* font{
            fonts.ensure_font(text->font_key, transform.pixels_per_reference_unit)};
        if (font == nullptr) {
          App::Log::warn(LogCategory::I2D, "no font loaded for key '{}'", text->font_key);
          continue;
        }

        // Recovered Runtime text-style behaviour (0x004769A0): inactive
        // selectable elements divide the RGB components by two. Static text
        // such as the Quit title is not an inactive choice and stays at its
        // authored intensity.
        const bool full_intensity{!selectable_text || selected};
        const float red{encoded_from_byte(
            full_intensity ? text->red : static_cast<std::uint8_t>(text->red / 2U))};
        const float green{encoded_from_byte(
            full_intensity ? text->green : static_cast<std::uint8_t>(text->green / 2U))};
        const float blue{encoded_from_byte(
            full_intensity ? text->blue : static_cast<std::uint8_t>(text->blue / 2U))};
        const std::array<float, 4> tint{red, green, blue, 1.0F};

        const float line_height{font->line_height()};
        const float origin_y{static_cast<float>(text->bounds.y) +
                             ((static_cast<float>(text->bounds.height) - line_height) * 0.5F)};

        const auto draw_text = [this, font, &tint, &counters, origin_y](
                                   const std::string_view content, const float origin_x) {
          float cursor_x{origin_x};
          const char* cursor{content.data()};
          const char* end{content.data() + content.size()};
          while (cursor < end) {
            const unsigned int codepoint{decode_utf8(cursor, end)};
            const auto glyph{font->glyph_for(codepoint)};

            if (!glyph.has_value()) {
              continue;
            }

            // Whitespace and other invisible glyphs still carry layout
            // advance. measure() includes the same AdvanceX values.
            if (glyph->visible) {
              const float x0{cursor_x + glyph->x0};
              const float y0{origin_y + glyph->y0};
              const float x1{cursor_x + glyph->x1};
              const float y1{origin_y + glyph->y1};

              push_quad(font->texture(),
                  x0,
                  y0,
                  x1,
                  y1,
                  glyph->u_left,
                  glyph->v_top,
                  glyph->u_right,
                  glyph->v_bottom,
                  tint,
                  I2DBlitOptions{});

              counters.glyphs += 1;
              counters.quads += 1;
            }

            cursor_x += glyph->advance_x;
          }
        };
        if (text->layout == I2DTextLayout::k_option_pair ||
            text->layout == I2DTextLayout::k_option_slider) {
          // Recovered Runtime option-row geometry from 0x00493380: label
          // right-aligned against centre-20, value widget begins at centre+20.
          const float centre_x{
              static_cast<float>(text->bounds.x) + (static_cast<float>(text->bounds.width) * 0.5F)};
          const float label_x{centre_x - k_i2d_option_pair_half_gap - font->measure(label)};
          draw_text(label, label_x);

          if (text->layout == I2DTextLayout::k_option_slider) {
            if (m_option_slider_texture.has_value()) {
              const auto& slider_texture{*m_option_slider_texture};
              const float fraction{
                  std::clamp(text->value_scalar ? text->value_scalar() : 0.0F, 0.0F, 1.0F)};
              const float slider_x{centre_x + k_i2d_option_pair_half_gap};
              const float slider_y{
                  static_cast<float>(text->bounds.y) +
                  ((static_cast<float>(text->bounds.height) - k_i2d_option_slider_height) * 0.5F)};
              const std::array<float, 4> dim_tint{0.25F, 0.25F, 0.25F, 1.0F};
              push_quad(slider_texture,
                  slider_x,
                  slider_y,
                  slider_x + k_i2d_option_slider_width,
                  slider_y + k_i2d_option_slider_height,
                  0.0F,
                  1.0F,
                  1.0F,
                  0.0F,
                  dim_tint,
                  I2DBlitOptions{});
              counters.quads += 1U;
              if (fraction > 0.0F) {
                push_quad(slider_texture,
                    slider_x,
                    slider_y,
                    slider_x + (k_i2d_option_slider_width * fraction),
                    slider_y + k_i2d_option_slider_height,
                    0.0F,
                    1.0F,
                    fraction,
                    0.0F,
                    tint,
                    I2DBlitOptions{});
                counters.quads += 1U;
              }
            }
          } else {
            const std::string value_storage{text->value_text ? text->value_text() : std::string{}};
            if (!value_storage.empty()) {
              draw_text(std::string_view{value_storage}, centre_x + k_i2d_option_pair_half_gap);
            }
          }
        } else {
          const float text_width{font->measure(label)};
          const float origin_x{static_cast<float>(text->bounds.x) +
                               ((static_cast<float>(text->bounds.width) - text_width) * 0.5F)};
          draw_text(label, origin_x);
        }
      }
    }
  }

  flush();
  counters.draw_calls += m_commands.size();

  // Restore the GL state modified by the I2D pass.
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  Shader::unbind();
  Texture2D::unbind();
  VertexArray::unbind();
}

void I2DRenderer::render_overlay(const std::array<float, 3>& color,
    const float alpha,
    const int pixel_width,
    const int pixel_height) {
  if (!m_initialized || m_overlay_shader == nullptr || m_vertex_array == nullptr || alpha <= 0.0F) {
    return;
  }

  glViewport(0, 0, pixel_width, pixel_height);

  const std::array<GLfloat, 3> gl_color{color.at(0), color.at(1), color.at(2)};
  m_overlay_shader->bind();
  m_overlay_shader->set_uniform_vec3(
      "u_color", std::span<const GLfloat, 3>{gl_color.data(), gl_color.size()});
  m_overlay_shader->set_uniform_float("u_alpha", std::clamp(alpha, 0.0F, 1.0F));

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  m_vertex_array->bind();
  glDrawArrays(GL_TRIANGLES, 0, 3);
  VertexArray::unbind();

  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  Shader::unbind();
}

float I2DRenderer::render_dialog(const Dialog::DialogPresentation& dialog,
    const std::size_t selected_choice,
    const float scroll_offset,
    FontManager& fonts,
    const int pixel_width,
    const int pixel_height,
    Debug::I2DCounters& counters) {
  APP_PROFILE_FUNCTION();

  if (!m_initialized || pixel_width <= 0 || pixel_height <= 0) {
    return 0.0F;
  }

  const I2DPresentationTransform transform{make_presentation_transform(pixel_width, pixel_height)};
  const FontResource* dialog_font{
      fonts.ensure_font(dialog_font_key(), transform.pixels_per_reference_unit)};
  if (dialog_font == nullptr) {
    return 0.0F;
  }

  glViewport(0, 0, pixel_width, pixel_height);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  m_shader->bind();
  m_shader->set_uniform_mat4(
      "u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(transform.projection), 16});
  m_shader->set_uniform_int("u_texture0", 0);
  reset();
  float maximum_scroll{0.0F};

  const auto draw_line = [this, &counters](const FontResource& font,
                             const std::string_view line,
                             const float origin_x,
                             const float origin_y,
                             const std::array<float, 4>& tint) {
    float cursor_x{origin_x};
    std::size_t byte_offset{0};
    while (byte_offset < line.size()) {
      const auto glyph{font.next_glyph(line, byte_offset)};
      if (!glyph.has_value()) {
        continue;
      }
      if (glyph->visible) {
        push_quad(font.texture(),
            cursor_x + glyph->x0,
            origin_y + glyph->y0,
            cursor_x + glyph->x1,
            origin_y + glyph->y1,
            glyph->u_left,
            glyph->v_top,
            glyph->u_right,
            glyph->v_bottom,
            tint,
            I2DBlitOptions{});
        counters.glyphs += 1U;
        counters.quads += 1U;
      }
      cursor_x += glyph->advance_x;
    }
  };

  if (dialog.state == Dialog::DialogState::k_presenting_line ||
      dialog.state == Dialog::DialogState::k_presenting_automatic_player_line) {
    const DialogTextLayout layout{format_dialog_text(dialog.displayed_line,
        k_dialog_text_width,
        k_dialog_main_viewport_height,
        dialog_font->line_height(),
        [dialog_font](const std::string_view text) {
          return dialog_font->measure(text);
        })};
    maximum_scroll = layout.max_scroll();
    float origin_y{
        k_dialog_main_viewport_top - std::clamp(scroll_offset, 0.0F, layout.max_scroll())};
    for (const DialogTextLine& line : layout.lines) {
      draw_line(
          *dialog_font, line.text, k_dialog_text_left, origin_y, dialog_main_tint(dialog.state));
      origin_y += dialog_font->line_height();
    }

    std::array<GLint, 4> previous_scissor{};
    glGetIntegerv(GL_SCISSOR_BOX, previous_scissor.data());
    const GLboolean scissor_was_enabled{glIsEnabled(GL_SCISSOR_TEST)};
    const float scale{transform.pixels_per_reference_unit};
    const int scissor_x{std::clamp(
        static_cast<int>(std::lround((k_dialog_text_left - transform.logical_left) * scale)),
        0,
        pixel_width)};
    const int scissor_right{std::clamp(
        static_cast<int>(std::lround(
            ((k_dialog_text_left + k_dialog_text_width) - transform.logical_left) * scale)),
        0,
        pixel_width)};
    const int scissor_y{std::clamp(
        static_cast<int>(std::lround(
            (k_reference_height - (k_dialog_main_viewport_top + k_dialog_main_viewport_height)) *
            scale)),
        0,
        pixel_height)};
    const int scissor_top{std::clamp(
        static_cast<int>(std::lround((k_reference_height - k_dialog_main_viewport_top) * scale)),
        0,
        pixel_height)};
    glEnable(GL_SCISSOR_TEST);
    glScissor(scissor_x,
        scissor_y,
        std::max(0, scissor_right - scissor_x),
        std::max(0, scissor_top - scissor_y));
    flush();
    counters.draw_calls += m_commands.size();
    reset();
    if (scissor_was_enabled == GL_TRUE) {
      glScissor(previous_scissor.at(0),
          previous_scissor.at(1),
          previous_scissor.at(2),
          previous_scissor.at(3));
    } else {
      glDisable(GL_SCISSOR_TEST);
    }
  }

  if (dialog.state == Dialog::DialogState::k_waiting_for_choice) {
    const std::size_t response_count{std::min<std::size_t>(4U, dialog.choices.size())};
    std::vector<DialogTextLayout> response_layouts;
    std::vector<float> response_heights;
    response_layouts.reserve(response_count);
    response_heights.reserve(response_count);
    const std::size_t maximum_response_lines{static_cast<std::size_t>(
        std::floor(k_dialog_response_max_height / dialog_font->line_height()))};
    for (std::size_t choice_index{0}; choice_index < response_count; ++choice_index) {
      response_layouts.push_back(format_dialog_text(dialog.choices.at(choice_index).text,
          k_dialog_text_width,
          k_dialog_response_max_height,
          dialog_font->line_height(),
          [dialog_font](const std::string_view text) {
            return dialog_font->measure(text);
          }));
      const std::size_t visible_line_count{
          std::min(maximum_response_lines, response_layouts.back().lines.size())};
      response_heights.push_back(
          static_cast<float>(visible_line_count) * dialog_font->line_height());
    }
    const DialogResponseBlockLayout block{layout_dialog_responses(response_heights)};
    for (std::size_t choice_index{0}; choice_index < response_count; ++choice_index) {
      float origin_y{block.response_tops.at(choice_index)};
      const float response_bottom{origin_y + response_heights.at(choice_index)};
      for (const DialogTextLine& line : response_layouts.at(choice_index).lines) {
        if (origin_y + dialog_font->line_height() > response_bottom) {
          break;
        }
        draw_line(*dialog_font,
            line.text,
            k_dialog_text_left,
            origin_y,
            dialog_response_tint(choice_index == selected_choice));
        origin_y += dialog_font->line_height();
      }
    }
  }

  flush();
  counters.draw_calls += m_commands.size();
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  Shader::unbind();
  Texture2D::unbind();
  VertexArray::unbind();
  return maximum_scroll;
}

void I2DRenderer::render_world_subtitle(const std::string_view text,
    FontManager& fonts,
    const int pixel_width,
    const int pixel_height,
    Debug::I2DCounters& counters) {
  if (!m_initialized || text.empty() || pixel_width <= 0 || pixel_height <= 0) {
    return;
  }
  const I2DPresentationTransform transform{make_presentation_transform(pixel_width, pixel_height)};
  const FontResource* font{
      fonts.ensure_font(dialog_font_key(), transform.pixels_per_reference_unit)};
  if (font == nullptr) {
    return;
  }
  const DialogTextLayout layout{format_dialog_text(text,
      k_dialog_text_width,
      k_dialog_main_viewport_height,
      font->line_height(),
      [font](const std::string_view value) {
        return font->measure(value);
      })};
  if (layout.lines.empty()) {
    return;
  }
  glViewport(0, 0, pixel_width, pixel_height);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  m_shader->bind();
  m_shader->set_uniform_mat4(
      "u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(transform.projection), 16});
  m_shader->set_uniform_int("u_texture0", 0);
  reset();
  float origin_y{k_dialog_main_viewport_top +
                 ((k_dialog_main_viewport_height - layout.formatted_height) * 0.5F)};
  for (const DialogTextLine& line : layout.lines) {
    float cursor_x{(k_reference_width - line.width) * 0.5F};
    std::size_t byte_offset{0};
    while (byte_offset < line.text.size()) {
      const auto glyph{font->next_glyph(line.text, byte_offset)};
      if (!glyph.has_value()) {
        continue;
      }
      if (glyph->visible) {
        push_quad(font->texture(),
            cursor_x + glyph->x0,
            origin_y + glyph->y0,
            cursor_x + glyph->x1,
            origin_y + glyph->y1,
            glyph->u_left,
            glyph->v_top,
            glyph->u_right,
            glyph->v_bottom,
            k_dialog_white,
            I2DBlitOptions{});
        counters.glyphs += 1U;
        counters.quads += 1U;
      }
      cursor_x += glyph->advance_x;
    }
    origin_y += font->line_height();
  }
  flush();
  counters.draw_calls += m_commands.size();
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  Shader::unbind();
  Texture2D::unbind();
  VertexArray::unbind();
}

void I2DRenderer::reset() {
  m_vertices.clear();
  m_indices.clear();
  m_commands.clear();
  m_current_texture = nullptr;
  m_current_keyed = false;
  m_current_key = std::array<float, 3>{0.0F, 0.0F, 0.0F};
  m_current_first_index = 0;
}

void I2DRenderer::flush_command() {
  if (m_current_texture == nullptr) {
    return;
  }
  const std::size_t index_count{m_indices.size() - m_current_first_index};
  if (index_count == 0U) {
    return;
  }
  m_commands.push_back(DrawCommand{.first_index = m_current_first_index,
      .index_count = index_count,
      .texture = m_current_texture,
      .source_colour_key = m_current_keyed,
      .key = m_current_key});
}

void I2DRenderer::push_quad(const Texture2D& texture,
    const float x0,
    const float y0,
    const float x1,
    const float y1,
    const float u0,
    const float v0,
    const float u1,
    const float v1,
    const std::array<float, 4> tint,
    const I2DBlitOptions& blit_options) {
  const bool keyed{blit_options.source_colour_key.has_value()};
  const std::array<float, 3> key{
      keyed ? *blit_options.source_colour_key : std::array<float, 3>{0.0F, 0.0F, 0.0F}};

  if (m_current_texture != &texture || m_current_keyed != keyed || m_current_key != key) {
    flush_command();
    m_current_texture = &texture;
    m_current_keyed = keyed;
    m_current_key = key;
    m_current_first_index = m_indices.size();
  }

  const std::uint32_t base{static_cast<std::uint32_t>(m_vertices.size())};
  m_vertices.push_back(Vertex{.position = {x0, y0, 0.0F}, .uv = {u0, v0}, .color = tint});
  m_vertices.push_back(Vertex{.position = {x1, y0, 0.0F}, .uv = {u1, v0}, .color = tint});
  m_vertices.push_back(Vertex{.position = {x1, y1, 0.0F}, .uv = {u1, v1}, .color = tint});
  m_vertices.push_back(Vertex{.position = {x0, y1, 0.0F}, .uv = {u0, v1}, .color = tint});

  m_indices.push_back(base + 0U);
  m_indices.push_back(base + 1U);
  m_indices.push_back(base + 2U);
  m_indices.push_back(base + 0U);
  m_indices.push_back(base + 2U);
  m_indices.push_back(base + 3U);
}

void I2DRenderer::flush() {
  APP_PROFILE_SCOPE("I2D.BatchBuild");

  flush_command();
  if (m_vertices.empty() || m_commands.empty()) {
    return;
  }

  {
    APP_PROFILE_SCOPE("I2D.Draw");
    m_vertex_buffer->upload(std::as_bytes(std::span<const Vertex>{m_vertices}));
    m_index_buffer->upload(std::span<const std::uint32_t>{m_indices});

    m_vertex_array->bind();
    for (const DrawCommand& command : m_commands) {
      // Every draw explicitly sets the source-colour-key state so a keyed
      // draw never leaks into the next non-keyed draw.
      if (command.source_colour_key) {
        m_shader->set_uniform_int("u_source_colour_key_enabled", 1);
        m_shader->set_uniform_vec3(
            "u_source_colour_key", std::span<const GLfloat, 3>{command.key.data(), 3});
      } else {
        m_shader->set_uniform_int("u_source_colour_key_enabled", 0);
      }

      command.texture->bind(0);
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
      const auto* index_offset{reinterpret_cast<const void*>(
          static_cast<std::uintptr_t>(command.first_index) * sizeof(std::uint32_t))};
      glDrawElements(
          GL_TRIANGLES, static_cast<GLsizei>(command.index_count), GL_UNSIGNED_INT, index_offset);
    }
    Texture2D::unbind();
    VertexArray::unbind();
  }
}

}  // namespace App::Interface

// NOLINTEND(misc-include-cleaner)

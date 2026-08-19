#include "Core/Interface/I2DRenderer.hpp"

// NOLINTBEGIN(misc-include-cleaner)
// glm follows a single-include convention (see ModelViewerScene.cpp); clang-tidy
// cannot trace each symbol back to a direct sub-header.
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Buffers.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/Interface/FontManager.hpp"
#include "Core/Interface/I2DBumpBackground.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Interface/I2DPresentation.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Log.hpp"
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

vec3 srgb_from_linear(vec3 colour) {
    vec3 low = colour * 12.92;
    vec3 high = 1.055 * pow(colour, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(colour, vec3(0.0031308)));
}

void main() {
    vec4 texel = texture(u_texture0, v_uv);

    if (u_source_colour_key_enabled != 0) {
        // Recovered DirectDraw semantics: exact comparison of the 5-bit
        // (RGB555) pixel value against the key. Quantising the sRGB source
        // bytes to 5 bits makes the near-black background (4,4,4 -> 0) key
        // out without any chroma-key threshold.
        vec3 srgb = srgb_from_linear(texel.rgb);
        ivec3 key = ivec3(u_source_colour_key * 255.0 + 0.5) >> 3;
        ivec3 pixel = ivec3(srgb * 255.0 + 0.5) >> 3;
        if (all(equal(pixel, key))) {
            discard;
        }
    }

    frag_colour = texel * v_color;
}
)glsl";
// clang-format on

/// Converts an sRGB byte component (0..255) to linear light. The vertex tint
/// feeds a linear pipeline with a sRGB framebuffer, so recovered sRGB byte
/// values (255 / 127) must be linearized to display faithfully.
float srgb_to_linear(const std::uint8_t byte) {
  const float srgb{static_cast<float>(byte) / 255.0F};
  if (srgb <= 0.04045F) {
    return srgb / 12.92F;
  }
  return std::pow((srgb + 0.055F) / 1.055F, 2.4F);
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
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("I2D renderer shader: {}", shader.error())};
  }
  m_shader = std::make_unique<Shader>(std::move(shader).value());

  m_vertex_buffer = std::make_unique<VertexBuffer>(
      std::span<const std::byte>{}, GL_DYNAMIC_DRAW);
  m_index_buffer = std::make_unique<IndexBuffer>(
      std::span<const std::uint32_t>{}, GL_DYNAMIC_DRAW);
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

  const I2DPresentationTransform transform{
      make_presentation_transform(pixel_width, pixel_height)};

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
    instance.current_state->background->render(transform);
    counters.draw_calls += 1;
  }

  // 2. Interface bitmap artwork + text, batched by texture/blit state.
  m_shader->bind();
  m_shader->set_uniform_mat4("u_mvp",
      std::span<const GLfloat, 16>{glm::value_ptr(transform.projection), 16});
  m_shader->set_uniform_int("u_texture0", 0);

  reset();

  // Selection ordinal over the current state's selectable text elements.
  const std::vector<I2DTextElement*> selectable{
      selectable_text_elements(*instance.current_state)};
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
        const float u1{static_cast<float>(bitmap->source.x + bitmap->source.width) /
                       texture_width};
        const float v_top{
            1.0F - (static_cast<float>(bitmap->source.y) / texture_height)};
        const float v_bottom{1.0F -
                             (static_cast<float>(bitmap->source.y + bitmap->source.height) /
                                 texture_height)};

        float x0{static_cast<float>(bitmap->destination.x)};
        float y0{static_cast<float>(bitmap->destination.y)};
        float x1{static_cast<float>(bitmap->destination.x + bitmap->destination.width)};
        float y1{static_cast<float>(bitmap->destination.y + bitmap->destination.height)};

        // Interface-specific presentation hints (e.g. the main-menu logo is
        // top-centred with a small modernization margin). Recovered Runtime
        // coordinates are not mutated; the adjustment is applied here.
        if (element.presentation.anchor_top_center) {
          const I2DTopCenterPlacement placement{compute_top_center_placement(
              bitmap->destination,
              element.presentation.top_margin_reference,
              element.presentation.clamp_width_to_viewport,
              pixel_width,
              pixel_height)};
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
        const bool selected{text->selectable() && selectable_ordinal == instance.selected_element};
        if (text->selectable()) {
          ++selectable_ordinal;
        }

        const std::string_view label{instance.strings.at(text->string_index)};
        if (label.empty()) {
          continue;
        }
        const FontResource* font{fonts.ensure_font(
            text->font_key, transform.pixels_per_reference_unit)};
        if (font == nullptr) {
          App::Log::warn("[I2D] no font loaded for key '{}'", text->font_key);
          continue;
        }

        // Recovered Runtime text-style behaviour (0x004769A0): inactive
        // elements divide the RGB components by two.
        const float red{srgb_to_linear(selected ? text->red : static_cast<std::uint8_t>(text->red / 2U))};
        const float green{srgb_to_linear(selected ? text->green : static_cast<std::uint8_t>(text->green / 2U))};
        const float blue{srgb_to_linear(selected ? text->blue : static_cast<std::uint8_t>(text->blue / 2U))};
        const std::array<float, 4> tint{red, green, blue, 1.0F};

        const float text_width{font->measure(label)};
        const float line_height{font->line_height()};
        const float origin_x{static_cast<float>(text->bounds.x) +
                             ((static_cast<float>(text->bounds.width) - text_width) * 0.5F)};
        const float origin_y{static_cast<float>(text->bounds.y) +
                             ((static_cast<float>(text->bounds.height) - line_height) * 0.5F)};

        float cursor_x{origin_x};
        const char* cursor{label.data()};
        const char* end{label.data() + label.size()};
        while (cursor < end) {
          const auto glyph{font->glyph_for(decode_utf8(cursor, end))};
          if (!glyph.has_value()) {
            continue;
          }
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
          cursor_x += glyph->advance_x;
          counters.glyphs += 1;
          counters.quads += 1;
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
        m_shader->set_uniform_vec3("u_source_colour_key",
            std::span<const GLfloat, 3>{command.key.data(), 3});
      } else {
        m_shader->set_uniform_int("u_source_colour_key_enabled", 0);
      }

      command.texture->bind(0);
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
      const auto* index_offset{reinterpret_cast<const void*>(
          static_cast<std::uintptr_t>(command.first_index) * sizeof(std::uint32_t))};
      glDrawElements(GL_TRIANGLES,
          static_cast<GLsizei>(command.index_count),
          GL_UNSIGNED_INT,
          index_offset);
    }
    Texture2D::unbind();
    VertexArray::unbind();
  }
}

}  // namespace App::Interface

// NOLINTEND(misc-include-cleaner)

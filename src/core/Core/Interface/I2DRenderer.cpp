#include "Core/Interface/I2DRenderer.hpp"

// NOLINTBEGIN(misc-include-cleaner)
// glm follows a single-include convention (see ModelScene.cpp); clang-tidy
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
#include "Core/Interface/FontManager.hpp"
#include "Core/Interface/I2DBumpBackground.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Log.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/Vertex.hpp"
#include "Core/VertexArray.hpp"

namespace App::Interface {

namespace {

constexpr float K_CANVAS_WIDTH{640.0F};
constexpr float K_CANVAS_HEIGHT{480.0F};

// clang-format off
/// I2D vertex shader: same inputs as the default textured shader.
constexpr std::string_view K_I2D_VERTEX_SOURCE = R"glsl(
#version 410 core

layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv;

uniform mat4 u_mvp;

out vec2 v_uv;

void main() {
    v_uv = a_uv;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)glsl";

/// I2D fragment shader: textured + tinted, with an optional per-draw source
/// colour key. Runtime bitmaps perform DirectDraw source-keyed blits, which
/// match the source pixel value exactly in a 16-bit RGB555 surface; the key
/// "pixel value 0" therefore also matches near-black palette entries (e.g.
/// the (4,4,4) bitmap background) whose 5-bit channels all truncate to zero.
constexpr std::string_view K_I2D_FRAGMENT_SOURCE = R"glsl(
#version 410 core

in vec2 v_uv;

uniform sampler2D u_texture0;
uniform vec4 u_tint;

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

    frag_colour = texel * u_tint;
}
)glsl";
// clang-format on

/// Converts an sRGB byte component (0..255) to linear light. The text tint
/// uniform feeds a linear pipeline with a sRGB framebuffer, so recovered
/// sRGB byte values (255 / 127) must be linearized to display faithfully.
float srgb_to_linear(const std::uint8_t byte) {
  const float srgb{static_cast<float>(byte) / 255.0F};
  if (srgb <= 0.04045F) {
    return srgb / 12.92F;
  }
  return std::pow((srgb + 0.055F) / 1.055F, 2.4F);
}

std::vector<std::uint32_t> make_quad_indices() {
  return {0U, 1U, 2U, 0U, 2U, 3U};
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

  // Four zero vertices; the dynamic buffer is re-uploaded per quad.
  const std::vector<Vertex> initial(4);
  m_vertex_buffer = std::make_unique<VertexBuffer>(
      std::as_bytes(std::span<const Vertex>{initial}), GL_DYNAMIC_DRAW);
  const std::vector<std::uint32_t> indices{make_quad_indices()};
  m_index_buffer = std::make_unique<IndexBuffer>(std::span<const std::uint32_t>{indices});
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

  m_initialized = true;
  return {};
}

void I2DRenderer::render(const InterfaceInstance& instance,
    const FontManager& fonts,
    const int pixel_width,
    const int pixel_height) {
  APP_PROFILE_FUNCTION();

  if (!m_initialized || instance.current_state == nullptr) {
    return;
  }

  begin_canvas(pixel_width, pixel_height);

  // 1. Animated background, when the current state has one.
  if (instance.current_state->background != nullptr) {
    const Texture2D& background{instance.current_state->background->texture()};
    // The bump background texture is stored top-down (row 0 = top), unlike
    // the other I2D textures which are stored bottom-up; its quad therefore
    // consumes v=0 at the top of the canvas.
    draw_quad(background,
        0.0F,
        0.0F,
        K_CANVAS_WIDTH,
        K_CANVAS_HEIGHT,
        0.0F,
        0.0F,
        1.0F,
        1.0F,
        std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F},
        I2DBlitOptions{});
  }

  // Selection ordinal over the current state's selectable text elements.
  const std::vector<I2DTextElement*> selectable{
      selectable_text_elements(*instance.current_state)};
  std::size_t selectable_ordinal{0};

  // 2. Interface bitmap artwork, then 3. text elements, in group order.
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

        const float x0{static_cast<float>(bitmap->destination.x)};
        const float y0{static_cast<float>(bitmap->destination.y)};
        const float x1{static_cast<float>(bitmap->destination.x + bitmap->destination.width)};
        const float y1{static_cast<float>(bitmap->destination.y + bitmap->destination.height)};
        draw_quad(texture,
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
      } else if (const auto* text{std::get_if<I2DTextElement>(&element.data)}) {
        const bool selected{text->selectable() && selectable_ordinal == instance.selected_element};
        if (text->selectable()) {
          ++selectable_ordinal;
        }

        const std::string_view label{instance.strings.at(text->string_index)};
        if (label.empty()) {
          continue;
        }
        const FontResource* font{fonts.font_for_key(text->font_key)};
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
          draw_quad(font->texture(),
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
        }
      }
    }
  }

  end_canvas();
}

void I2DRenderer::begin_canvas(const int pixel_width, const int pixel_height) {
  const float scale{std::min(static_cast<float>(pixel_width) / K_CANVAS_WIDTH,
      static_cast<float>(pixel_height) / K_CANVAS_HEIGHT)};
  const int drawable_width{static_cast<int>(K_CANVAS_WIDTH * scale)};
  const int drawable_height{static_cast<int>(K_CANVAS_HEIGHT * scale)};
  const int offset_x{(pixel_width - drawable_width) / 2};
  const int offset_y{(pixel_height - drawable_height) / 2};

  glViewport(offset_x, offset_y, drawable_width, drawable_height);

  // Recovered Runtime text and bitmaps are authored in a 640x480, top-left
  // origin canvas; the ortho projection maps that directly to the viewport.
  m_shader->bind();
  const glm::mat4 projection{
      glm::ortho(0.0F, K_CANVAS_WIDTH, K_CANVAS_HEIGHT, 0.0F, -1.0F, 1.0F)};
  m_shader->set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(projection), 16});
  m_shader->set_uniform_int("u_texture0", 0);

  // The I2D pass is a flat 2D composition: no depth, no culling, alpha blend.
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void I2DRenderer::end_canvas() {
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);
  Shader::unbind();
  Texture2D::unbind();
  VertexArray::unbind();
}

void I2DRenderer::draw_quad(const Texture2D& texture,
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
  std::vector<Vertex> vertices{
      Vertex{.position = {x0, y0, 0.0F}, .uv = {u0, v0}},
      Vertex{.position = {x1, y0, 0.0F}, .uv = {u1, v0}},
      Vertex{.position = {x1, y1, 0.0F}, .uv = {u1, v1}},
      Vertex{.position = {x0, y1, 0.0F}, .uv = {u0, v1}},
  };

  m_vertex_buffer->upload(std::as_bytes(std::span<const Vertex>{vertices}));
  m_vertex_array->bind();
  m_index_buffer->bind();
  m_shader->set_uniform_vec4("u_tint", std::span<const GLfloat, 4>{tint});

  // Every draw explicitly sets the source-colour-key state so a keyed draw
  // never leaks into the next non-keyed draw.
  if (blit_options.source_colour_key.has_value()) {
    m_shader->set_uniform_int("u_source_colour_key_enabled", 1);
    m_shader->set_uniform_vec3("u_source_colour_key",
        std::span<const GLfloat, 3>{blit_options.source_colour_key->data(), 3});
  } else {
    m_shader->set_uniform_int("u_source_colour_key_enabled", 0);
  }

  texture.bind(0);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
  Texture2D::unbind();
  VertexArray::unbind();
}

}  // namespace App::Interface

// NOLINTEND(misc-include-cleaner)

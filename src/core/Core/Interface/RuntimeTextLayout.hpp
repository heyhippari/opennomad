#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "Core/Interface/RuntimeText.hpp"

namespace App::Interface {

struct RuntimeGlyphMetrics {
  float advance_x{0.0F};
  float line_height{0.0F};
};

using RuntimeTextMetrics =
    std::function<RuntimeGlyphMetrics(std::uint8_t font_key, std::uint8_t text_byte)>;

struct RuntimePositionedGlyph {
  std::uint8_t text_byte{0};
  RuntimeTextStyle style;
  float x{0.0F};
  float y{0.0F};
  float advance_x{0.0F};
  std::size_t source_offset{0};
  std::size_t generated_line{0};
};

struct RuntimeTextLayoutLine {
  std::size_t first_glyph{0};
  std::size_t glyph_count{0};
  float x{0.0F};
  float y{0.0F};
  float width{0.0F};
  float height{0.0F};
  bool authored_break_after{false};
};

struct RuntimeTextLayout {
  std::vector<RuntimePositionedGlyph> glyphs;
  std::vector<RuntimeTextLayoutLine> lines;
  std::vector<std::size_t> format_boundary_source_offsets;
};

struct RuntimeTextLayoutOptions {
  RuntimeTextStyle initial_style{.font_key = 'D',
      .horizontal = RuntimeHorizontalMode::k_center,
      .vertical = RuntimeVerticalMode::k_middle};
  RuntimeTextStyle selected_style{.font_key = 'R',
      .color = {255U, 255U, 255U},
      .horizontal = RuntimeHorizontalMode::k_center,
      .vertical = RuntimeVerticalMode::k_middle};
  int selected_span_index{-1};
  float left{32.0F};
  float right{608.0F};
  float top{412.0F};
  float bottom{476.0F};
};

[[nodiscard]] RuntimeTextLayout layout_runtime_text(const RuntimeTextDocument& document,
    const RuntimeTextMetrics& metrics,
    const RuntimeTextLayoutOptions& options = {});

}  // namespace App::Interface
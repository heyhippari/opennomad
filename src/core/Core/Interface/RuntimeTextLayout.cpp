#include "Core/Interface/RuntimeTextLayout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "Core/Interface/RuntimeText.hpp"

namespace App::Interface {
namespace {

struct PendingGlyph {
  std::uint8_t text_byte{0};
  RuntimeTextStyle style;
  float advance_x{0.0F};
  float line_height{0.0F};
  std::size_t source_offset{0};
};

struct PendingLine {
  std::vector<PendingGlyph> glyphs;
  float width{0.0F};
  float height{0.0F};
  float anchor_x{0.0F};
  RuntimeHorizontalMode horizontal{RuntimeHorizontalMode::k_left};
  bool authored_break_after{false};
};

[[nodiscard]] PendingLine make_pending_line(
    const float anchor_x, const RuntimeHorizontalMode horizontal) {
  PendingLine line;
  line.anchor_x = anchor_x;
  line.horizontal = horizontal;
  return line;
}

[[nodiscard]] float default_horizontal_anchor(
    const RuntimeHorizontalMode horizontal, const RuntimeTextLayoutOptions& options) {
  if (horizontal == RuntimeHorizontalMode::k_right) {
    return options.right;
  }
  if (horizontal == RuntimeHorizontalMode::k_left ||
      horizontal == RuntimeHorizontalMode::k_mode_0x10) {
    return options.left;
  }
  return (options.left + options.right) * 0.5F;
}

[[nodiscard]] float default_vertical_anchor(
    const RuntimeVerticalMode vertical, const RuntimeTextLayoutOptions& options) {
  if (vertical == RuntimeVerticalMode::k_top) {
    return options.top;
  }
  if (vertical == RuntimeVerticalMode::k_bottom) {
    return options.bottom;
  }
  return (options.top + options.bottom) * 0.5F;
}

[[nodiscard]] float block_top(
    const RuntimeVerticalMode vertical, const float anchor_y, const float total_height) {
  if (vertical == RuntimeVerticalMode::k_bottom) {
    return anchor_y - total_height;
  }
  if (vertical == RuntimeVerticalMode::k_middle) {
    return anchor_y - (total_height * 0.5F);
  }
  return anchor_y;
}

[[nodiscard]] float horizontal_origin(const PendingLine& line) {
  switch (line.horizontal) {
    case RuntimeHorizontalMode::k_center:
      return line.anchor_x - (line.width * 0.5F);
    case RuntimeHorizontalMode::k_right:
      return line.anchor_x - line.width;
    case RuntimeHorizontalMode::k_left:
    case RuntimeHorizontalMode::k_mode_0x10:
      return line.anchor_x;
  }
  return line.anchor_x;
}

void append_word(PendingLine& line,
    std::vector<PendingGlyph>& word,
    std::vector<PendingLine>& lines,
    const float maximum_width,
    const float anchor_x,
    const RuntimeHorizontalMode horizontal) {
  for (const PendingGlyph& glyph : word) {
    if (!line.glyphs.empty() && line.width + glyph.advance_x > maximum_width) {
      lines.push_back(std::move(line));
      line = make_pending_line(anchor_x, horizontal);
    }
    if (line.glyphs.empty()) {
      line.anchor_x = anchor_x;
      line.horizontal = horizontal;
    }
    line.width += glyph.advance_x;
    line.height = std::max(line.height, glyph.line_height);
    line.glyphs.push_back(glyph);
  }
  word.clear();
}

}  // namespace

RuntimeTextLayout layout_runtime_text(const RuntimeTextDocument& document,
    const RuntimeTextMetrics& metrics,
    const RuntimeTextLayoutOptions& options) {
  RuntimeTextLayout layout;
  RuntimeTextStyle style{options.initial_style};
  std::optional<RuntimeTextStyle> saved_selected_style;
  float anchor_x{default_horizontal_anchor(options.initial_style.horizontal, options)};
  float anchor_y{default_vertical_anchor(options.initial_style.vertical, options)};
  bool has_absolute_position{false};
  RuntimeVerticalMode vertical{options.initial_style.vertical};
  const float maximum_width{std::max(0.0F, options.right - options.left)};
  PendingLine line{make_pending_line(anchor_x, style.horizontal)};
  std::vector<PendingGlyph> word;
  std::vector<PendingLine> lines;

  const auto flush_word{[&]() {
    // clang-analyzer loses track of local lambda reference captures here.
    // NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
    append_word(line, word, lines, maximum_width, anchor_x, style.horizontal);
  }};
  const auto finish_line{[&](const bool authored_break) {
    flush_word();
    line.authored_break_after = authored_break;
    if (!line.glyphs.empty() || authored_break) {
      lines.push_back(std::move(line));
    }
    line = make_pending_line(anchor_x, style.horizontal);
  }};

  for (const RuntimeTextEvent& event : document.events()) {
    switch (event.type) {
      case RuntimeTextEventType::k_text_bytes:
        for (std::size_t index{0}; index < event.text_bytes.size(); ++index) {
          const std::uint8_t text_byte{static_cast<std::uint8_t>(event.text_bytes.at(index))};
          const RuntimeGlyphMetrics glyph_metrics{metrics(style.font_key, text_byte)};
          const PendingGlyph glyph{.text_byte = text_byte,
              .style = style,
              .advance_x = glyph_metrics.advance_x,
              .line_height = glyph_metrics.line_height,
              .source_offset = event.source_offset + index};
          if (text_byte == static_cast<std::uint8_t>(' ')) {
            flush_word();
            if (!line.glyphs.empty() && line.width + glyph.advance_x > maximum_width) {
              lines.push_back(std::move(line));
              line = make_pending_line(anchor_x, style.horizontal);
            } else {
              word.push_back(glyph);
              flush_word();
            }
          } else {
            word.push_back(glyph);
          }
        }
        break;
      case RuntimeTextEventType::k_line_break:
        finish_line(true);
        break;
      case RuntimeTextEventType::k_set_font:
        style.font_key = event.byte_value;
        break;
      case RuntimeTextEventType::k_set_color:
        style.color = event.color;
        break;
      case RuntimeTextEventType::k_set_horizontal_mode:
        style.horizontal = event.horizontal;
        if (!has_absolute_position) {
          anchor_x = default_horizontal_anchor(event.horizontal, options);
        }
        if (line.glyphs.empty() && word.empty()) {
          line.anchor_x = anchor_x;
          line.horizontal = style.horizontal;
        }
        break;
      case RuntimeTextEventType::k_set_vertical_mode:
        vertical = event.vertical;
        anchor_y = default_vertical_anchor(vertical, options);
        break;
      case RuntimeTextEventType::k_toggle_flash_red:
        style.flash_red = !style.flash_red;
        break;
      case RuntimeTextEventType::k_set_auxiliary_e:
        style.auxiliary_e = event.byte_value;
        break;
      case RuntimeTextEventType::k_absolute_position:
        anchor_x = event.position_x;
        anchor_y = event.position_y;
        has_absolute_position = true;
        if (line.glyphs.empty() && word.empty()) {
          line.anchor_x = anchor_x;
        }
        break;
      case RuntimeTextEventType::k_format_boundary:
        layout.format_boundary_source_offsets.push_back(event.source_offset);
        break;
      case RuntimeTextEventType::k_selectable_span_begin:
        if (event.span_ordinal == options.selected_span_index) {
          saved_selected_style = style;
          style = options.selected_style;
        }
        break;
      case RuntimeTextEventType::k_selectable_span_end:
        if (event.span_ordinal == options.selected_span_index && saved_selected_style.has_value()) {
          style = saved_selected_style.value();
          saved_selected_style.reset();
        }
        break;
    }
  }
  finish_line(false);

  float total_height{0.0F};
  for (PendingLine& pending_line : lines) {
    if (pending_line.height <= 0.0F) {
      pending_line.height = metrics(style.font_key, static_cast<std::uint8_t>(' ')).line_height;
    }
    total_height += pending_line.height;
  }
  float cursor_y{block_top(vertical, anchor_y, total_height)};
  for (std::size_t line_index{0}; line_index < lines.size(); ++line_index) {
    const PendingLine& pending_line{lines.at(line_index)};
    float cursor_x{horizontal_origin(pending_line)};
    const std::size_t first_glyph{layout.glyphs.size()};
    for (const PendingGlyph& pending_glyph : pending_line.glyphs) {
      layout.glyphs.push_back(RuntimePositionedGlyph{.text_byte = pending_glyph.text_byte,
          .style = pending_glyph.style,
          .x = cursor_x,
          .y = cursor_y,
          .advance_x = pending_glyph.advance_x,
          .source_offset = pending_glyph.source_offset,
          .generated_line = line_index});
      cursor_x += pending_glyph.advance_x;
    }
    layout.lines.push_back(RuntimeTextLayoutLine{.first_glyph = first_glyph,
        .glyph_count = pending_line.glyphs.size(),
        .x = horizontal_origin(pending_line),
        .y = cursor_y,
        .width = pending_line.width,
        .height = pending_line.height,
        .authored_break_after = pending_line.authored_break_after});
    cursor_y += pending_line.height;
  }
  return layout;
}

}  // namespace App::Interface
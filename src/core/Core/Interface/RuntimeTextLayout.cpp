#include "Core/Interface/RuntimeTextLayout.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "Core/Interface/I2DPresentation.hpp"
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

/// One formatter step. Text events are expanded to one item per byte so a
/// word wrap can rewind to the byte after the remembered space exactly like
/// Runtime rewinds its source pointer.
struct LayoutItem {
  const RuntimeTextEvent* event{nullptr};
  std::size_t byte_index{0};
};

[[nodiscard]] std::vector<LayoutItem> flatten_events(const RuntimeTextDocument& document) {
  std::vector<LayoutItem> items;
  items.reserve(document.events().size() + document.authored_bytes().size());
  for (const RuntimeTextEvent& event : document.events()) {
    if (event.type == RuntimeTextEventType::k_text_bytes) {
      for (std::size_t index{0}; index < event.text_bytes.size(); ++index) {
        items.push_back(LayoutItem{.event = &event, .byte_index = index});
      }
    } else {
      items.push_back(LayoutItem{.event = &event});
    }
  }
  return items;
}

/// Runtime 0x0043FD7B advances by `line_height * 120 / 100` in integer units.
[[nodiscard]] float line_advance(const float line_height) {
  return std::floor(line_height * 120.0F / 100.0F);
}

}  // namespace

RuntimeTextLayout layout_runtime_text(const RuntimeTextDocument& document,
    const RuntimeTextMetrics& metrics,
    const RuntimeTextLayoutOptions& options) {
  RuntimeTextLayout layout;
  RuntimeTextStyle style{options.initial_style};
  std::optional<RuntimeTextStyle> saved_selected_style;
  // Runtime keeps the style flags of the pending buffer separately from the
  // flags mutated by commands; a pending line is placed with the former.
  RuntimeHorizontalMode committed_horizontal{style.horizontal};
  RuntimeVerticalMode committed_vertical{style.vertical};
  const auto font_line_height{[&metrics](const std::uint8_t font_key) {
    return metrics(font_key, static_cast<std::uint8_t>(' ')).line_height;
  }};

  // The wrap width is fixed from the caller's box before any {X} command.
  const float maximum_width{std::max(0.0F, options.right - options.left)};
  float left_bound{options.left};
  float top_bound{options.top};
  float origin_x{options.left};
  float cursor_y{options.top};
  if (style.vertical == RuntimeVerticalMode::k_bottom) {
    cursor_y = options.bottom - font_line_height(style.font_key);
  } else if (style.vertical == RuntimeVerticalMode::k_middle) {
    cursor_y = ((options.bottom - font_line_height(style.font_key) - top_bound) * 0.5F) + top_bound;
  }
  float maximum_y{cursor_y};

  std::vector<PendingGlyph> pending;
  float pending_width{0.0F};
  std::optional<std::size_t> last_space_glyph;
  std::optional<std::size_t> resume_item;

  const auto flush{[&](const bool advance, const bool authored_break) {
    if (!pending.empty()) {
      float width{0.0F};
      float height{0.0F};
      for (const PendingGlyph& glyph : pending) {
        width += glyph.advance_x;
        height = std::max(height, glyph.line_height);
      }
      // Runtime 0x0043FBF5: only left/0x10 placement uses the {X} origin;
      // right and centre placement use the caller's box.
      float line_x{origin_x};
      if (committed_horizontal == RuntimeHorizontalMode::k_right) {
        line_x = options.right - width;
      } else if (committed_horizontal == RuntimeHorizontalMode::k_center) {
        line_x = left_bound + std::trunc((options.right - width - left_bound) * 0.5F);
      }
      const std::size_t first_glyph{layout.glyphs.size()};
      float cursor_x{line_x};
      for (const PendingGlyph& glyph : pending) {
        layout.glyphs.push_back(RuntimePositionedGlyph{.text_byte = glyph.text_byte,
            .style = glyph.style,
            .x = cursor_x,
            .y = cursor_y,
            .advance_x = glyph.advance_x,
            .source_offset = glyph.source_offset,
            .generated_line = layout.lines.size()});
        cursor_x += glyph.advance_x;
      }
      layout.lines.push_back(RuntimeTextLayoutLine{.first_glyph = first_glyph,
          .glyph_count = pending.size(),
          .x = line_x,
          .y = cursor_y,
          .width = width,
          .height = height,
          .authored_break_after = authored_break});
    } else if (authored_break) {
      layout.lines.push_back(RuntimeTextLayoutLine{.first_glyph = layout.glyphs.size(),
          .x = origin_x,
          .y = cursor_y,
          .height = font_line_height(style.font_key),
          .authored_break_after = true});
    }
    pending.clear();
    pending_width = 0.0F;
    last_space_glyph.reset();
    committed_horizontal = style.horizontal;
    committed_vertical = style.vertical;
    if (advance) {
      cursor_y += line_advance(font_line_height(style.font_key));
      maximum_y = std::max(maximum_y, cursor_y);
    }
  }};

  const std::vector<LayoutItem> items{flatten_events(document)};
  for (std::size_t item_index{0}; item_index < items.size(); ++item_index) {
    const RuntimeTextEvent& event{*items.at(item_index).event};
    switch (event.type) {
      case RuntimeTextEventType::k_text_bytes: {
        const std::size_t byte_index{items.at(item_index).byte_index};
        const auto text_byte{static_cast<std::uint8_t>(event.text_bytes.at(byte_index))};
        const RuntimeGlyphMetrics glyph_metrics{metrics(style.font_key, text_byte)};
        const bool is_space{text_byte == static_cast<std::uint8_t>(' ')};
        if (pending_width + glyph_metrics.advance_x > maximum_width) {
          if (last_space_glyph.has_value()) {
            // Wrap at the remembered space: drop it and everything after,
            // then re-read the source from the byte following it.
            pending.resize(last_space_glyph.value());
            const std::size_t resume{resume_item.value()};
            flush(true, false);
            item_index = resume - 1U;
            continue;
          }
          if (is_space) {
            flush(true, false);
            continue;
          }
          // A single word wider than the box overflows rather than splits.
        } else if (is_space) {
          last_space_glyph = pending.size();
          resume_item = item_index + 1U;
        }
        pending_width += glyph_metrics.advance_x;
        pending.push_back(PendingGlyph{.text_byte = text_byte,
            .style = style,
            .advance_x = glyph_metrics.advance_x,
            .line_height = glyph_metrics.line_height,
            .source_offset = event.source_offset + byte_index});
        break;
      }
      case RuntimeTextEventType::k_line_break:
        flush(true, true);
        break;
      case RuntimeTextEventType::k_set_font:
        style.font_key = event.byte_value;
        break;
      case RuntimeTextEventType::k_set_color:
        style.color = event.color;
        break;
      case RuntimeTextEventType::k_set_horizontal_mode:
        style.horizontal = event.horizontal;
        break;
      case RuntimeTextEventType::k_set_vertical_mode:
        style.vertical = event.vertical;
        if (event.vertical == RuntimeVerticalMode::k_top) {
          // Runtime `H` resets both the region top and the cursor to zero.
          top_bound = 0.0F;
          cursor_y = 0.0F;
        } else if (event.vertical == RuntimeVerticalMode::k_bottom) {
          cursor_y = options.bottom - font_line_height(style.font_key);
        } else {
          cursor_y =
              ((options.bottom - font_line_height(style.font_key) - top_bound) * 0.5F) + top_bound;
        }
        break;
      case RuntimeTextEventType::k_toggle_flash_red:
        style.flash_red = !style.flash_red;
        break;
      case RuntimeTextEventType::k_set_auxiliary_e:
        style.auxiliary_e = event.byte_value;
        break;
      case RuntimeTextEventType::k_absolute_position:
        // Runtime 0x0043F90A moves the line origin without flushing pending
        // text; the region bounds only ever grow toward the origin.
        origin_x = event.position_x;
        cursor_y = event.position_y;
        left_bound = std::min(left_bound, origin_x);
        top_bound = std::min(top_bound, cursor_y);
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
    // A changed alignment mode places the pending text on the current line
    // with the previous mode, without advancing to the next line.
    if (style.horizontal != committed_horizontal || style.vertical != committed_vertical) {
      flush(false, false);
    }
  }
  // The terminating NUL resolves the last line like a line break.
  flush(true, false);
  layout.measured_height = maximum_y - top_bound;
  return layout;
}

RuntimeTextLayout layout_runtime_world_text(
    const RuntimeTextDocument& document, const RuntimeTextMetrics& metrics) {
  // Runtime 0x0041E040 measures the text with a zero top, then 0x004208E6
  // draws it so its measured block ends 16 units above the screen bottom.
  constexpr float k_margin{16.0F};
  RuntimeTextLayoutOptions options{.initial_style = {.font_key = 'V',
                                       .horizontal = RuntimeHorizontalMode::k_left,
                                       .vertical = RuntimeVerticalMode::k_top},
      .left = k_margin,
      .right = k_reference_width - k_margin,
      .top = 0.0F,
      .bottom = k_reference_height};
  const RuntimeTextLayout measured{layout_runtime_text(document, metrics, options)};
  options.top = k_reference_height - measured.measured_height - k_margin;
  return layout_runtime_text(document, metrics, options);
}

}  // namespace App::Interface

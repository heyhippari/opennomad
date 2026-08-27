#include "Core/Interface/RuntimeText.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Interface/I2DPresentation.hpp"

namespace App::Interface {
namespace {

[[nodiscard]] bool has_operand(
    const std::string_view bytes, const std::size_t offset, const std::size_t count) {
  return offset <= bytes.size() && count <= bytes.size() - offset &&
         !bytes.substr(offset, count).contains('\0');
}

[[nodiscard]] bool parse_decimal(const std::string_view bytes, int& value) {
  value = 0;
  for (const char byte : bytes) {
    if (byte < '0' || byte > '9') {
      return false;
    }
    value = (value * 10) + (byte - '0');
  }
  return true;
}

void append_text_byte(
    std::vector<RuntimeTextEvent>& events, const char byte, const std::size_t source_offset) {
  if (!events.empty() && events.back().type == RuntimeTextEventType::k_text_bytes &&
      events.back().source_offset + events.back().source_length == source_offset) {
    events.back().text_bytes.push_back(byte);
    ++events.back().source_length;
    return;
  }
  events.push_back(RuntimeTextEvent{.type = RuntimeTextEventType::k_text_bytes,
      .text_bytes = std::string(1, byte),
      .source_offset = source_offset,
      .source_length = 1});
}

void append_simple_event(std::vector<RuntimeTextEvent>& events,
    const RuntimeTextEventType type,
    const std::size_t source_offset,
    const std::size_t source_length = 1) {
  RuntimeTextEvent event;
  event.type = type;
  event.source_offset = source_offset;
  event.source_length = source_length;
  events.push_back(std::move(event));
}

[[nodiscard]] RuntimeHorizontalMode horizontal_mode(const char command) {
  switch (command) {
    case 'C':
      return RuntimeHorizontalMode::k_center;
    case 'D':
      return RuntimeHorizontalMode::k_right;
    case 'F':
      return RuntimeHorizontalMode::k_mode_0x10;
    default:
      return RuntimeHorizontalMode::k_left;
  }
}

[[nodiscard]] RuntimeVerticalMode vertical_mode(const char command) {
  switch (command) {
    case 'L':
      return RuntimeVerticalMode::k_bottom;
    case 'M':
      return RuntimeVerticalMode::k_middle;
    default:
      return RuntimeVerticalMode::k_top;
  }
}

}  // namespace

RuntimeTextDocument::RuntimeTextDocument(
    std::string authored_bytes, std::vector<RuntimeTextEvent> events)
    : m_authored_bytes(std::move(authored_bytes)),
      m_events(std::move(events)) {}

const std::string& RuntimeTextDocument::authored_bytes() const {
  return m_authored_bytes;
}

const std::vector<RuntimeTextEvent>& RuntimeTextDocument::events() const {
  return m_events;
}

RuntimeTextDocument parse_runtime_text(
    std::string authored_bytes, const RuntimeTextParseOptions options) {
  const std::string_view bytes{authored_bytes};
  std::vector<RuntimeTextEvent> events;
  bool command_mode{false};
  int span_ordinal{-1};

  for (std::size_t offset = 0; offset < bytes.size();) {
    const char byte{bytes.at(offset)};
    if (byte == '\0') {
      break;
    }
    if (options.raw_mode) {
      if (byte == '\r') {
        ++offset;
      } else if (byte == '\n') {
        append_simple_event(events, RuntimeTextEventType::k_line_break, offset++);
      } else {
        append_text_byte(events, byte, offset++);
      }
      continue;
    }
    if (!command_mode) {
      if (byte == '{') {
        command_mode = true;
        ++offset;
      } else if (byte == '[') {
        ++span_ordinal;
        RuntimeTextEvent event;
        event.type = RuntimeTextEventType::k_selectable_span_begin;
        event.source_offset = offset++;
        event.source_length = 1;
        event.span_ordinal = span_ordinal;
        events.push_back(std::move(event));
      } else if (byte == ']') {
        RuntimeTextEvent event;
        event.type = RuntimeTextEventType::k_selectable_span_end;
        event.source_offset = offset++;
        event.source_length = 1;
        event.span_ordinal = span_ordinal;
        events.push_back(std::move(event));
      } else if (byte == '\r') {
        ++offset;
      } else if (byte == '\n') {
        append_simple_event(events, RuntimeTextEventType::k_line_break, offset++);
      } else {
        append_text_byte(events, byte, offset++);
      }
      continue;
    }

    if (byte == '}') {
      command_mode = false;
      ++offset;
      continue;
    }

    RuntimeTextEvent event;
    event.source_offset = offset;
    event.source_length = 1;
    switch (byte) {
      case 'B':
        event.type = RuntimeTextEventType::k_toggle_flash_red;
        events.push_back(std::move(event));
        ++offset;
        break;
      case 'C':
      case 'D':
      case 'G':
      case 'F':
        event.type = RuntimeTextEventType::k_set_horizontal_mode;
        event.horizontal = horizontal_mode(byte);
        events.push_back(std::move(event));
        ++offset;
        if (byte == 'F') {
          append_simple_event(events, RuntimeTextEventType::k_format_boundary, offset - 1);
        }
        break;
      case 'H':
      case 'L':
      case 'M':
        event.type = RuntimeTextEventType::k_set_vertical_mode;
        event.vertical = vertical_mode(byte);
        events.push_back(std::move(event));
        ++offset;
        break;
      case 'f':
      case 'E':
        if (has_operand(bytes, offset + 1, 1)) {
          event.type = byte == 'f' ? RuntimeTextEventType::k_set_font
                                   : RuntimeTextEventType::k_set_auxiliary_e;
          event.byte_value = static_cast<std::uint8_t>(bytes.at(offset + 1));
          event.source_length = 2;
          events.push_back(std::move(event));
          offset += 2;
        } else {
          offset = bytes.size();
        }
        break;
      case 'I': {
        constexpr std::size_t k_color_operand_size{9};
        if (!has_operand(bytes, offset + 1, k_color_operand_size)) {
          offset = bytes.size();
          break;
        }
        std::array<int, 3> components{};
        const bool valid{parse_decimal(bytes.substr(offset + 1, 3), components.at(0)) &&
                         parse_decimal(bytes.substr(offset + 4, 3), components.at(1)) &&
                         parse_decimal(bytes.substr(offset + 7, 3), components.at(2))};
        if (valid) {
          event.type = RuntimeTextEventType::k_set_color;
          event.source_length = 10;
          std::ranges::transform(components, event.color.begin(), [](const int component) {
            return static_cast<std::uint8_t>(std::clamp(component, 0, 255));
          });
          events.push_back(std::move(event));
        }
        offset += 10;
        break;
      }
      case 'X': {
        constexpr std::size_t k_position_operand_size{6};
        if (!has_operand(bytes, offset + 1, k_position_operand_size)) {
          offset = bytes.size();
          break;
        }
        int percent_x{0};
        int percent_y{0};
        if (parse_decimal(bytes.substr(offset + 1, 3), percent_x) &&
            parse_decimal(bytes.substr(offset + 4, 3), percent_y)) {
          event.type = RuntimeTextEventType::k_absolute_position;
          event.source_length = 7;
          event.position_x = static_cast<float>(percent_x) * k_reference_width / 100.0F;
          event.position_y = static_cast<float>(percent_y) * k_reference_height / 100.0F;
          events.push_back(std::move(event));
        }
        offset += 7;
        break;
      }
      case 'g':
      default:
        ++offset;
        break;
    }
  }

  return RuntimeTextDocument{std::move(authored_bytes), std::move(events)};
}

std::string runtime_text_plain_bytes(const RuntimeTextDocument& document) {
  std::string plain_text;
  for (const RuntimeTextEvent& event : document.events()) {
    if (event.type == RuntimeTextEventType::k_text_bytes) {
      plain_text += event.text_bytes;
    } else if (event.type == RuntimeTextEventType::k_line_break) {
      plain_text.push_back('\n');
    }
  }
  return plain_text;
}

std::array<std::uint8_t, 3> resolve_runtime_text_color(
    const RuntimeTextStyle& style, const std::uint64_t presentation_time_ms) {
  constexpr std::uint64_t k_flash_period_ms{500};
  if (style.flash_red && ((presentation_time_ms / k_flash_period_ms) % 2U) != 0U) {
    return {255U, 0U, 0U};
  }
  return style.color;
}

}  // namespace App::Interface
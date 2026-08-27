#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace App::Interface {

enum class RuntimeHorizontalMode : std::uint16_t {
  k_left = 0x02,
  k_right = 0x04,
  k_center = 0x08,
  k_mode_0x10 = 0x10,
};

enum class RuntimeVerticalMode : std::uint16_t {
  k_top = 0x0400,
  k_bottom = 0x0800,
  k_middle = 0x1000,
};

struct RuntimeTextStyle {
  std::uint8_t font_key{'D'};
  std::array<std::uint8_t, 3> color{255U, 255U, 255U};
  RuntimeHorizontalMode horizontal{RuntimeHorizontalMode::k_left};
  RuntimeVerticalMode vertical{RuntimeVerticalMode::k_top};
  bool flash_red{false};
  std::uint8_t auxiliary_e{0};

  bool operator==(const RuntimeTextStyle&) const = default;
};

enum class RuntimeTextEventType : std::uint8_t {
  k_text_bytes,
  k_line_break,
  k_set_font,
  k_set_color,
  k_set_horizontal_mode,
  k_set_vertical_mode,
  k_toggle_flash_red,
  k_set_auxiliary_e,
  k_absolute_position,
  k_format_boundary,
  k_selectable_span_begin,
  k_selectable_span_end,
};

struct RuntimeTextEvent {
  RuntimeTextEventType type{RuntimeTextEventType::k_text_bytes};
  std::string text_bytes;
  std::size_t source_offset{0};
  std::size_t source_length{0};
  std::uint8_t byte_value{0};
  std::array<std::uint8_t, 3> color{0U, 0U, 0U};
  RuntimeHorizontalMode horizontal{RuntimeHorizontalMode::k_left};
  RuntimeVerticalMode vertical{RuntimeVerticalMode::k_top};
  float position_x{0.0F};
  float position_y{0.0F};
  int span_ordinal{-1};
};

struct RuntimeTextParseOptions {
  bool raw_mode{false};
};

class RuntimeTextDocument {
 public:
  RuntimeTextDocument(std::string authored_bytes, std::vector<RuntimeTextEvent> events);

  [[nodiscard]] const std::string& authored_bytes() const;
  [[nodiscard]] const std::vector<RuntimeTextEvent>& events() const;

 private:
  std::string m_authored_bytes;
  std::vector<RuntimeTextEvent> m_events;
};

[[nodiscard]] RuntimeTextDocument parse_runtime_text(
    std::string authored_bytes, RuntimeTextParseOptions options = {});

/// Returns visible authored bytes and explicit line breaks without decoding,
/// wrapping, shortening, or otherwise rewriting retail text.
[[nodiscard]] std::string runtime_text_plain_bytes(const RuntimeTextDocument& document);

/// Resolves Runtime's 500 ms flash phase without consulting a system clock.
[[nodiscard]] std::array<std::uint8_t, 3> resolve_runtime_text_color(
    const RuntimeTextStyle& style, std::uint64_t presentation_time_ms);

}  // namespace App::Interface
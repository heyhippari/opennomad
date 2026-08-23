#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Dialog/DialogRuntime.hpp"

namespace App::Interface {

inline constexpr float k_dialog_text_left{32.0F};
inline constexpr float k_dialog_text_width{576.0F};
inline constexpr float k_dialog_main_viewport_top{412.0F};
inline constexpr float k_dialog_main_viewport_height{64.0F};
inline constexpr float k_dialog_main_viewport_bottom{
    k_dialog_main_viewport_top + k_dialog_main_viewport_height};
inline constexpr float k_dialog_response_bottom{448.0F};
inline constexpr float k_dialog_response_max_height{96.0F};
inline constexpr float k_dialog_scroll_speed{30.0F};

inline constexpr std::array<float, 4> k_dialog_white{1.0F, 1.0F, 1.0F, 1.0F};
inline constexpr std::array<float, 4> k_dialog_automatic_tint{
    128.0F / 255.0F, 128.0F / 255.0F, 192.0F / 255.0F, 1.0F};
inline constexpr std::array<float, 4> k_dialog_unselected_tint{
    128.0F / 255.0F, 128.0F / 255.0F, 128.0F / 255.0F, 1.0F};

using TextMeasure = std::function<float(std::string_view)>;

struct DialogTextLine {
  std::string text;
  float width{0.0F};
};

struct DialogTextLayout {
  std::vector<DialogTextLine> lines;
  float formatted_height{0.0F};
  float viewport_height{0.0F};

  [[nodiscard]] float max_scroll() const;
};

/// Word-wraps dialog bytes using exact backend advances and explicit newlines.
[[nodiscard]] DialogTextLayout format_dialog_text(std::string_view text,
    float maximum_width,
    float viewport_height,
    float line_height,
    const TextMeasure& measure);

struct DialogResponseBlockLayout {
  std::vector<float> response_tops;
  float total_height{0.0F};
  float top{k_dialog_response_bottom};
  float bottom{k_dialog_response_bottom};
};

/// Bottom-aligns authored responses while preserving visible response order.
[[nodiscard]] DialogResponseBlockLayout layout_dialog_responses(
    std::span<const float> response_heights);

/// Every dialog string in this presenter uses the retail D font. R remains a
/// valid registry key for other Runtime consumers.
[[nodiscard]] constexpr char dialog_font_key() {
  return 'D';
}

[[nodiscard]] std::array<float, 4> dialog_main_tint(Dialog::DialogState state);
[[nodiscard]] std::array<float, 4> dialog_response_tint(bool selected);

/// Presentation-owned held-scroll state, reset by DialogRuntime generation.
class DialogScrollState {
 public:
  /// Observes a generation and resets when it differs from the prior value.
  /// Returns true when a reset occurred.
  bool observe_generation(std::uint64_t generation);
  void reset();
  void update(float delta_seconds, bool up_held, bool down_held);
  void set_maximum(float maximum);

  [[nodiscard]] float offset() const {
    return m_offset;
  }

  [[nodiscard]] float clamped(float maximum) const;

 private:
  std::uint64_t m_generation{0};
  float m_offset{0.0F};
  float m_maximum{0.0F};
  bool m_observed{false};
};

}  // namespace App::Interface

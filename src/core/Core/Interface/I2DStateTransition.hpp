#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "Core/Interface/InterfacePresentation.hpp"

namespace App::Interface {

enum class I2DStateTransitionDirection : std::uint8_t {
  k_forward,
  k_back,
  k_replace,
};

enum class I2DMenuTransitionStyle : std::uint8_t {
  k_modern,
  k_classic,
  k_reduced_motion,
};

enum class I2DTransitionContext : std::uint8_t {
  k_start_menu,
  k_options,
  k_cross_interface,
};

struct I2DStateVisual {
  float offset_x{0.0F};
  float offset_y{0.0F};
  float alpha{1.0F};
};

struct I2DTransitionSample {
  I2DStateVisual outgoing;
  I2DStateVisual incoming;
};

[[nodiscard]] constexpr std::size_t next_selection(
    const std::size_t current, const std::size_t count) {
  return count == 0U ? current : (current + 1U) % count;
}

[[nodiscard]] constexpr std::size_t previous_selection(
    const std::size_t current, const std::size_t count) {
  if (count == 0U) {
    return current;
  }
  return current == 0U ? count - 1U : current - 1U;
}

[[nodiscard]] constexpr I2DMenuTransitionStyle menu_transition_style_from_raw(
    const std::int32_t raw) {
  if (raw == 1) {
    return I2DMenuTransitionStyle::k_classic;
  }
  if (raw == 2) {
    return I2DMenuTransitionStyle::k_reduced_motion;
  }
  return I2DMenuTransitionStyle::k_modern;
}

[[nodiscard]] constexpr float transition_duration(
    const I2DMenuTransitionStyle style, const I2DTransitionContext context) {
  if (style == I2DMenuTransitionStyle::k_reduced_motion) {
    return 0.10F;
  }
  if (style == I2DMenuTransitionStyle::k_classic) {
    return context == I2DTransitionContext::k_options ? 0.0F : 0.50F;
  }
  return 0.20F;
}

[[nodiscard]] constexpr I2DTransitionSample sample_transition(const I2DMenuTransitionStyle style,
    const I2DStateTransitionDirection direction,
    const I2DTransitionContext context,
    const float normalized_progress) {
  const float progress{std::clamp(normalized_progress, 0.0F, 1.0F)};
  if (style == I2DMenuTransitionStyle::k_classic && context == I2DTransitionContext::k_options) {
    return I2DTransitionSample{};
  }

  if (style == I2DMenuTransitionStyle::k_reduced_motion) {
    return I2DTransitionSample{
        .outgoing = I2DStateVisual{.offset_x = 0.0F, .offset_y = 0.0F, .alpha = 1.0F - progress},
        .incoming = I2DStateVisual{.offset_x = 0.0F, .offset_y = 0.0F, .alpha = progress}};
  }

  const bool classic{style == I2DMenuTransitionStyle::k_classic};
  const float distance{classic ? 640.0F : 24.0F};
  const float eased{
      classic ? progress
              : evaluate_presentation_easing(InterfacePresentationEasing::k_smoothstep, progress)};
  float outgoing_sign{-1.0F};
  if (direction == I2DStateTransitionDirection::k_back) {
    outgoing_sign = 1.0F;
  }
  const float outgoing_x{direction == I2DStateTransitionDirection::k_replace
                             ? 0.0F
                             : outgoing_sign * distance * eased};
  const float incoming_x{direction == I2DStateTransitionDirection::k_replace
                             ? 0.0F
                             : -outgoing_sign * distance * (1.0F - eased)};
  const float alpha{classic ? 1.0F : 1.0F - eased};
  const float incoming_alpha{classic ? 1.0F : eased};
  return I2DTransitionSample{
      .outgoing = I2DStateVisual{.offset_x = outgoing_x, .offset_y = 0.0F, .alpha = alpha},
      .incoming =
          I2DStateVisual{.offset_x = incoming_x, .offset_y = 0.0F, .alpha = incoming_alpha}};
}

using MenuTransitionStyle = I2DMenuTransitionStyle;

}  // namespace App::Interface

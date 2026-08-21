#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace App::Interface {

/// Easing applied only to OpenNomad presentation-policy transitions. These
/// values never alter Runtime/AREA/I2D timing or recovered state.
enum class InterfacePresentationEasing : std::uint8_t {
  k_linear,
  k_smoothstep,
  k_quadratic_in,
  k_quadratic_out,
  k_quadratic_in_out,
};

/// Evaluates a presentation easing curve over a clamped 0..1 interval.
[[nodiscard]] constexpr float evaluate_presentation_easing(
    const InterfacePresentationEasing easing, const float progress) {
  const float t{progress < 0.0F ? 0.0F : (progress > 1.0F ? 1.0F : progress)};
  switch (easing) {
    case InterfacePresentationEasing::k_smoothstep:
      return t * t * (3.0F - (2.0F * t));
    case InterfacePresentationEasing::k_quadratic_in:
      return t * t;
    case InterfacePresentationEasing::k_quadratic_out: {
      const float remaining{1.0F - t};
      return 1.0F - (remaining * remaining);
    }
    case InterfacePresentationEasing::k_quadratic_in_out: {
      if (t < 0.5F) {
        return 2.0F * t * t;
      }
      const float remaining{1.0F - t};
      return 1.0F - (2.0F * remaining * remaining);
    }
    case InterfacePresentationEasing::k_linear:
    default:
      return t;
  }
}

/// One full-screen colour fade used as an interface presentation hint.
struct InterfaceFadePresentationHint {
  std::array<float, 3> color{0.0F, 0.0F, 0.0F};
  float duration_seconds{0.0F};
  InterfacePresentationEasing easing{InterfacePresentationEasing::k_linear};
};

/// Result-specific transition performed before the real interface completion
/// is queued. The interface remains alive and its normal animation continues
/// during pre_delay_seconds and the fade; navigation input is locked.
struct InterfaceCompletionPresentationHint {
  std::int16_t result{0};
  float pre_delay_seconds{0.0F};
  InterfaceFadePresentationHint fade;
};

/// OpenNomad-only lifecycle presentation policy for one interface descriptor.
///
/// This deliberately sits beside, rather than inside, recovered I2D state.
/// Interfaces opt in individually; an empty/default instance preserves the
/// original instantaneous Runtime presentation.
struct InterfacePresentationHints {
  /// Fade from this colour after the interface has been fully constructed.
  std::optional<InterfaceFadePresentationHint> enter_fade;
  /// Completion transitions keyed by the Runtime result value.
  std::span<const InterfaceCompletionPresentationHint> completion_transitions{};
};

/// Current full-screen overlay requested by interface presentation policy.
struct InterfacePresentationOverlay {
  std::array<float, 3> color{0.0F, 0.0F, 0.0F};
  float alpha{0.0F};
};

}  // namespace App::Interface
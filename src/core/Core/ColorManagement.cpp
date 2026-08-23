#include "Core/ColorManagement.hpp"

#include <algorithm>
#include <cmath>

namespace App::ColorManagement {

float srgb_to_linear(const float encoded) {
  if (encoded <= 0.04045F) {
    return encoded / 12.92F;
  }
  return std::pow((encoded + 0.055F) / 1.055F, 2.4F);
}

float linear_to_srgb(const float linear) {
  if (linear <= 0.0031308F) {
    return 12.92F * linear;
  }
  return (1.055F * std::pow(linear, 1.0F / 2.4F)) - 0.055F;
}

SdrBaseAndHdrExcess split_sdr_base_and_hdr_excess(const float linear) {
  const float base{std::clamp(linear, 0.0F, 1.0F)};
  return {.base = base, .excess = std::max(linear - base, 0.0F)};
}

float legacy_alpha_over(const float destination_linear,
    const float accumulated_premultiplied_encoded,
    const float coverage) {
  const SdrBaseAndHdrExcess split{split_sdr_base_and_hdr_excess(destination_linear)};
  const float transmittance{1.0F - std::clamp(coverage, 0.0F, 1.0F)};
  const float encoded{
      std::clamp(accumulated_premultiplied_encoded + (linear_to_srgb(split.base) * transmittance),
          0.0F,
          1.0F)};
  return srgb_to_linear(encoded) + (split.excess * transmittance);
}

float legacy_additive(const float destination_linear, const float accumulated_encoded) {
  const SdrBaseAndHdrExcess split{split_sdr_base_and_hdr_excess(destination_linear)};
  const float encoded{std::clamp(linear_to_srgb(split.base) + accumulated_encoded, 0.0F, 1.0F)};
  return srgb_to_linear(encoded) + split.excess;
}

float legacy_darken(const float destination_linear, const float accumulated_factor) {
  const SdrBaseAndHdrExcess split{split_sdr_base_and_hdr_excess(destination_linear)};
  const float factor{std::clamp(accumulated_factor, 0.0F, 1.0F)};
  return srgb_to_linear(linear_to_srgb(split.base) * factor) + (split.excess * factor);
}

float legacy_subtractive(const float destination_linear, const float accumulated_encoded) {
  const SdrBaseAndHdrExcess split{split_sdr_base_and_hdr_excess(destination_linear)};
  const float encoded{std::max(linear_to_srgb(split.base) - accumulated_encoded, 0.0F)};
  return srgb_to_linear(encoded) + split.excess;
}

float linear_scene_to_sdr(const float linear) {
  return linear_to_srgb(std::clamp(linear, 0.0F, 1.0F));
}

}  // namespace App::ColorManagement

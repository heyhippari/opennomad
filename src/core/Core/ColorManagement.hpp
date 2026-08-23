#pragma once

namespace App::ColorManagement {

struct SdrBaseAndHdrExcess {
  float base{0.0F};
  float excess{0.0F};
};

/// Standard sRGB electro-optical transfer function (encoded to linear).
[[nodiscard]] float srgb_to_linear(float encoded);

/// Standard sRGB opto-electronic transfer function (linear to encoded).
[[nodiscard]] float linear_to_srgb(float linear);

[[nodiscard]] SdrBaseAndHdrExcess split_sdr_base_and_hdr_excess(float linear);
[[nodiscard]] float legacy_alpha_over(
    float destination_linear, float accumulated_premultiplied_encoded, float coverage);
[[nodiscard]] float legacy_additive(float destination_linear, float accumulated_encoded);
[[nodiscard]] float legacy_darken(float destination_linear, float accumulated_factor);
[[nodiscard]] float legacy_subtractive(float destination_linear, float accumulated_encoded);
[[nodiscard]] float linear_scene_to_sdr(float linear);

}  // namespace App::ColorManagement

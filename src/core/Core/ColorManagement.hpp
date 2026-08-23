#pragma once

namespace App::ColorManagement {

/// Standard sRGB electro-optical transfer function (encoded to linear).
[[nodiscard]] float srgb_to_linear(float encoded);

/// Standard sRGB opto-electronic transfer function (linear to encoded).
[[nodiscard]] float linear_to_srgb(float linear);

}  // namespace App::ColorManagement

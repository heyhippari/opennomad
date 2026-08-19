#pragma once

#include <cstdint>

namespace App::Sprite {

/// Runtime sprite render modes. Numeric values match the original
/// `SpriteInstance.renderMode` field exactly; modes 1 and 8 share renderer
/// behaviour but remain distinct values for gameplay compatibility.
enum class SpriteRenderMode : std::uint16_t {
  k_default = 0,           ///< Baseline material behaviour.
  k_cutout = 1,            ///< Color-key/cutout (bucket bit 0x0400).
  k_alpha = 2,             ///< Standard alpha transparency (0x2000).
  k_alpha_cutout = 3,      ///< Alpha transparency plus cutout (0x2400).
  k_additive = 4,          ///< Additive blending (0x2100).
  k_additive_cutout = 5,   ///< Additive plus cutout (0x2500).
  k_darken = 6,            ///< Inverse-source-color darkening (0x2200).
  k_darken_cutout = 7,     ///< Darkening plus cutout (0x2600).
  k_alternate_cutout = 8,  ///< Renders identically to k_cutout (0x0400).
};

/// Render-bucket state bits added by each render mode in the original
/// runtime (RENDER_BUCKET_* flags). Kept for documentation and diagnostics;
/// the modern renderer maps modes through render_state() instead.
[[nodiscard]] constexpr std::uint16_t bucket_bits(const SpriteRenderMode mode) {
  switch (mode) {
    case SpriteRenderMode::k_default:         return 0x0000U;
    case SpriteRenderMode::k_cutout:          return 0x0400U;
    case SpriteRenderMode::k_alpha:           return 0x2000U;
    case SpriteRenderMode::k_alpha_cutout:    return 0x2400U;
    case SpriteRenderMode::k_additive:        return 0x2100U;
    case SpriteRenderMode::k_additive_cutout: return 0x2500U;
    case SpriteRenderMode::k_darken:          return 0x2200U;
    case SpriteRenderMode::k_darken_cutout:   return 0x2600U;
    case SpriteRenderMode::k_alternate_cutout: return 0x0400U;
    default:                                  return 0x0000U;
  }
}

/// OpenGL blend factors, kept GL-free so the mode table is unit-testable.
enum class BlendFactor : std::uint8_t {
  k_zero,
  k_one,
  k_source_alpha,
  k_one_minus_source_alpha,
  k_one_minus_source_color,
};

/// Decoded renderer behaviour of one sprite render mode.
struct SpriteRenderState {
  bool blend_enabled{false};
  BlendFactor source_factor{BlendFactor::k_one};
  BlendFactor destination_factor{BlendFactor::k_zero};
  bool depth_write{true};
  bool cutout{false};
  /// True when the sprite participates in scene fog. The original runtime
  /// never fogged translucent (0x2000) sprites.
  bool fogged{true};
};

/// Maps a render mode to its proven renderer behaviour. The depth test
/// stays enabled in every mode; only the depth write mask changes.
[[nodiscard]] constexpr SpriteRenderState render_state(const SpriteRenderMode mode) {
  switch (mode) {
    case SpriteRenderMode::k_default:
      return {.blend_enabled = false,
          .source_factor = BlendFactor::k_one,
          .destination_factor = BlendFactor::k_zero,
          .depth_write = true,
          .cutout = false,
          .fogged = true};
    case SpriteRenderMode::k_cutout:
      return {.blend_enabled = false,
          .source_factor = BlendFactor::k_one,
          .destination_factor = BlendFactor::k_zero,
          .depth_write = true,
          .cutout = true,
          .fogged = true};
    case SpriteRenderMode::k_alpha:
      return {.blend_enabled = true,
          .source_factor = BlendFactor::k_source_alpha,
          .destination_factor = BlendFactor::k_one_minus_source_alpha,
          .depth_write = false,
          .cutout = false,
          .fogged = false};
    case SpriteRenderMode::k_alpha_cutout:
      return {.blend_enabled = true,
          .source_factor = BlendFactor::k_source_alpha,
          .destination_factor = BlendFactor::k_one_minus_source_alpha,
          .depth_write = false,
          .cutout = true,
          .fogged = false};
    case SpriteRenderMode::k_additive:
      return {.blend_enabled = true,
          .source_factor = BlendFactor::k_one,
          .destination_factor = BlendFactor::k_one,
          .depth_write = false,
          .cutout = false,
          .fogged = false};
    case SpriteRenderMode::k_additive_cutout:
      return {.blend_enabled = true,
          .source_factor = BlendFactor::k_one,
          .destination_factor = BlendFactor::k_one,
          .depth_write = false,
          .cutout = true,
          .fogged = false};
    case SpriteRenderMode::k_darken:
      return {.blend_enabled = true,
          .source_factor = BlendFactor::k_zero,
          .destination_factor = BlendFactor::k_one_minus_source_color,
          .depth_write = false,
          .cutout = false,
          .fogged = false};
    case SpriteRenderMode::k_darken_cutout:
      return {.blend_enabled = true,
          .source_factor = BlendFactor::k_zero,
          .destination_factor = BlendFactor::k_one_minus_source_color,
          .depth_write = false,
          .cutout = true,
          .fogged = false};
    case SpriteRenderMode::k_alternate_cutout:
      return {.blend_enabled = false,
          .source_factor = BlendFactor::k_one,
          .destination_factor = BlendFactor::k_zero,
          .depth_write = true,
          .cutout = true,
          .fogged = true};
    default:
      return {.blend_enabled = false,
          .source_factor = BlendFactor::k_one,
          .destination_factor = BlendFactor::k_zero,
          .depth_write = true,
          .cutout = false,
          .fogged = true};
  }
}

}  // namespace App::Sprite

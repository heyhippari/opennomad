#pragma once

#include <cstdint>
#include <expected>
#include <memory>

#include "Core/Interface/I2DBumpEffect.hpp"
#include "Core/Texture.hpp"

namespace App::Interface {

/// Animated main-menu background reproducing Runtime's I2D bump effect
/// (source file C:\Omikron\Sources\omikron\I2D_Bump.c).
///
/// Runtime addresses:
///   init            ~0x004B19C0
///   per-frame pass  ~0x004B1B00
///   warp tables     ~0x004B1F40
///   CLOUD load/ramp ~0x004B2220
///
/// IMAGES/CLOUD.BMP is loaded as a raw 256x256 height map (its 8-bit pixel
/// indices, not its RGB appearance). The pure-CPU I2DBumpEffect owns the
/// recovered math: a moving bump-light position, signed height gradients, a
/// 64-entry Runtime colour ramp, and animated 480/640-entry cosine warp
/// tables producing a 640x480 generated surface.
class I2DBumpBackground {
 public:
  /// Loads IMAGES/CLOUD.BMP as an indexed height map and allocates the
  /// 640x480 canvas texture. Requires a current GL context. A missing,
  /// unreadable or unsupported (non-256x256, non-8-bit indexed) source is an
  /// error so the caller can degrade explicitly rather than showing an
  /// invented asset.
  [[nodiscard]] static std::expected<std::unique_ptr<I2DBumpBackground>, std::string> create();

  I2DBumpBackground(const I2DBumpBackground&) = delete;
  I2DBumpBackground(I2DBumpBackground&&) = delete;
  I2DBumpBackground& operator=(const I2DBumpBackground&) = delete;
  I2DBumpBackground& operator=(I2DBumpBackground&&) = delete;
  ~I2DBumpBackground() = default;

  /// Advances the effect in fixed 30 Hz original-effect ticks (the recovered
  /// Runtime constants are per effect update, not per second) and re-uploads
  /// the generated frame to the canvas texture.
  void update(float delta_time);

  [[nodiscard]] const Texture2D& texture() const {
    return m_canvas;
  }

 private:
  explicit I2DBumpBackground(Texture2D canvas, I2DBumpEffect effect);

  Texture2D m_canvas;
  I2DBumpEffect m_effect;
  /// Accumulates host seconds and is drained in fixed 30 Hz effect ticks so
  /// the animation speed is independent of the host frame rate.
  float m_tick_accumulator{0.0F};
};

}  // namespace App::Interface

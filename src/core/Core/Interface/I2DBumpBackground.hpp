#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <vector>

#include "Core/Texture.hpp"

namespace App::Interface {

/// Animated main-menu background derived from IMAGES/CLOUD.BMP.
///
/// The recovered Runtime effect (I2D_Bump.c, init ~0x004B19C0, per-frame
/// ~0x004B1B00) computes a per-frame bump/light-map distortion from a
/// 256x256 cloud source. That exact pipeline is not yet reproduced: this
/// class implements a clearly-documented first-pass approximation that
/// scrolls the cloud source with a sine-based distortion into an animated
/// 640x480 surface. The abstraction isolates the internals so the exact
/// algorithm can replace them without touching callers.
class I2DBumpBackground {
 public:
  /// Loads IMAGES/CLOUD.BMP and allocates the 640x480 canvas texture.
  /// Requires a current GL context. Missing/unreadable source is an error so
  /// the caller can degrade explicitly rather than showing an invented asset.
  [[nodiscard]] static std::expected<std::unique_ptr<I2DBumpBackground>, std::string> create();

  I2DBumpBackground(const I2DBumpBackground&) = delete;
  I2DBumpBackground(I2DBumpBackground&&) = delete;
  I2DBumpBackground& operator=(const I2DBumpBackground&) = delete;
  I2DBumpBackground& operator=(I2DBumpBackground&&) = delete;
  ~I2DBumpBackground() = default;

  /// Advances the animation by delta_time seconds and re-uploads the canvas.
  void update(float delta_time);

  [[nodiscard]] const Texture2D& texture() const {
    return m_canvas;
  }

 private:
  explicit I2DBumpBackground(Texture2D canvas,
      std::vector<std::uint8_t> source_rgba8_top_down,
      int source_size);

  Texture2D m_canvas;
  /// Top-down RGBA8 copy of the 256x256 cloud source (row 0 = top).
  std::vector<std::uint8_t> m_source;
  int m_source_size{0};
  /// Reused 640x480 RGBA8 frame buffer.
  std::vector<std::uint8_t> m_frame;
  float m_time{0.0F};
};

}  // namespace App::Interface

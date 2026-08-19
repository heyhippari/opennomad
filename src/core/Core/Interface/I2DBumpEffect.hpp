#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Omikron/IndexedBmp8.hpp"

namespace App::Interface {

/// Pure-CPU reproduction of Runtime's I2D bump background effect
/// (source file C:\Omikron\Sources\omikron\I2D_Bump.c).
///
/// Runtime addresses:
///   init            ~0x004B19C0
///   per-frame pass  ~0x004B1B00
///   warp tables     ~0x004B1F40
///   CLOUD load/ramp ~0x004B2220
///
/// CLOUD.BMP's raw 8-bit pixel indices are treated as a 256x256 height map.
/// Each effect tick:
///   1. computes a moving bump-light position on a circle around (128,128);
///   2. derives signed 8-bit X/Y height gradients;
///   3. maps each pixel to an intensity index 0..63;
///   4. resolves that index through a recovered 64-entry colour ramp;
///   5. warps the 256x256 lit map into a 640x480 RGBA frame through two
///      animated cosine lookup tables (480 rows, 640 columns), consumed in
///      reverse order.
///
/// The object owns all buffers and never allocates during advance_one_tick().
/// It requires no OpenGL context, which keeps the recovered math testable.
class I2DBumpEffect {
 public:
  /// Builds the effect from a decoded indexed BMP. The source must be a
  /// 256x256 height map (the validated CLOUD.BMP format); anything else is
  /// an error rather than a silent approximation.
  [[nodiscard]] static std::expected<I2DBumpEffect, std::string> create(
      Omikron::IndexedBmp8 source);

  I2DBumpEffect(const I2DBumpEffect&) = delete;
  I2DBumpEffect(I2DBumpEffect&&) = default;
  I2DBumpEffect& operator=(const I2DBumpEffect&) = delete;
  I2DBumpEffect& operator=(I2DBumpEffect&&) = default;
  ~I2DBumpEffect() = default;

  /// Advances the effect by exactly one original effect update.
  void advance_one_tick();

  /// The generated 640x480 RGBA8 frame (row 0 = top). RGB components are
  /// recovered palette bytes; alpha is always 255.
  [[nodiscard]] std::span<const std::uint8_t> rgba_frame() const {
    return m_frame;
  }

  /// The recovered 64-entry colour ramp, exposed for formula-level tests.
  [[nodiscard]] static const std::array<std::array<std::uint8_t, 4>, 64>& palette();

  // --- Observability accessors (recovered-math tests, no GL required) ---

  [[nodiscard]] int light_x() const { return m_light_x; }
  [[nodiscard]] int light_y() const { return m_light_y; }

  [[nodiscard]] double phase_a() const { return m_phase_a; }
  [[nodiscard]] double phase_b() const { return m_phase_b; }
  [[nodiscard]] double phase_c() const { return m_phase_c; }
  [[nodiscard]] double phase_d() const { return m_phase_d; }

  [[nodiscard]] std::uint8_t row_warp(const std::size_t index) const {
    return m_row_warp.at(index);
  }
  [[nodiscard]] std::uint8_t column_warp(const std::size_t index) const {
    return m_column_warp.at(index);
  }

  /// The intensity index written into the 256x256 lit map for a cell.
  [[nodiscard]] std::uint8_t lit_intensity(const std::size_t x,
                                           const std::size_t y) const {
    return m_lit.at((y * K_HEIGHT_SIZE) + x);
  }

  /// The wrapped source coordinate the final warp reads for the given output
  /// pixel (reverse table order + 8-bit wrapping).
  [[nodiscard]] std::pair<int, int> warp_source_coordinates(const int output_x,
                                                            const int output_y) const {
    const int row_offset{m_row_warp.at(
        static_cast<std::size_t>(K_ROW_WARP_SIZE - 1 - output_y))};
    const int column_offset{m_column_warp.at(
        static_cast<std::size_t>(K_COLUMN_WARP_SIZE - 1 - output_x))};
    return {(output_x + row_offset) & 0xFF, (output_y + column_offset) & 0xFF};
  }

  /// Signed 8-bit wrap of an integer difference (Runtime's signed-byte
  /// gradient semantics): the value is wrapped into [-128, 127].
  [[nodiscard]] static std::int8_t signed_byte(const int value) {
    return static_cast<std::int8_t>(static_cast<std::uint8_t>(value));
  }

  /// Arithmetic right shift by 5, matching x86 SAR (negative values round
  /// toward negative infinity, not toward zero).
  [[nodiscard]] static int arithmetic_shift_right_5(const int value) {
    return value >> 5;
  }

 private:
  I2DBumpEffect() = default;

  static constexpr int K_HEIGHT_SIZE{256};
  static constexpr int K_ROW_WARP_SIZE{480};
  static constexpr int K_COLUMN_WARP_SIZE{640};
  static constexpr int K_FRAME_WIDTH{640};
  static constexpr int K_FRAME_HEIGHT{480};

  /// 256x256 raw height values (row 0 = top).
  std::vector<std::uint8_t> m_height;
  /// 64-entry recovered colour ramp, RGBA8.
  std::array<std::array<std::uint8_t, 4>, 64> m_palette{};
  /// 256x256 intensity map (0..63).
  std::vector<std::uint8_t> m_lit;
  /// Animated 480-entry row distortion table.
  std::array<std::uint8_t, K_ROW_WARP_SIZE> m_row_warp{};
  /// Animated 640-entry column distortion table.
  std::array<std::uint8_t, K_COLUMN_WARP_SIZE> m_column_warp{};
  /// 640x480 RGBA8 output (row 0 = top).
  std::vector<std::uint8_t> m_frame;

  double m_light_angle{10.0};
  int m_light_x{128};
  int m_light_y{128};

  double m_phase_a{0.0};
  double m_phase_b{1.0};
  double m_phase_c{2.0};
  double m_phase_d{0.5};

  /// Runtime also maintains a second double, initially 2.0 and decremented by
  /// 0.0815 inside the same routine, but the current trace shows no other
  /// reference that affects the generated background. It is intentionally not
  /// modelled so the effect does not invent behavior Runtime does not have.
};

}  // namespace App::Interface

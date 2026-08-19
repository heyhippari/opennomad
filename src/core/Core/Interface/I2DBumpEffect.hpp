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

  /// Advances the effect's logical time by exactly one original effect update
  /// (the moving light position and the four warp phases). It performs no
  /// image generation: a host frame that has to catch up several missed
  /// 30 Hz ticks can advance the state by all of them and then generate only
  /// one current presentation frame (see regenerate_lighting()).
  void advance_one_tick();

  /// Advances the logical time by `ticks` original effect updates without
  /// generating any intermediate image. Equivalent to calling
  /// advance_one_tick() `ticks` times for state purposes, but the caller is
  /// expected to follow it with a single regenerate_lighting() /
  /// regenerate_frame() instead of one per tick.
  void advance_ticks(std::uint32_t ticks);

  /// Number of original effect updates applied so far (0 at construction).
  [[nodiscard]] std::uint64_t tick_index() const {
    return m_tick_index;
  }

  /// Rebuilds the animated warp tables and the 256x256 lit intensity field
  /// from the current logical state. This is the CPU work performed once per
  /// 30 Hz effect update in the production path; the final full-resolution
  /// warp lives on the GPU.
  void regenerate_lighting();

  /// Regenerates the 640x480 RGBA8 reference frame from the current lit field
  /// and warp tables (the recovered Runtime CPU output). Used by tests and a
  /// debug/reference mode; the production renderer does not call this.
  void regenerate_frame();

  /// The generated 640x480 RGBA8 frame (row 0 = top). RGB components are
  /// recovered palette bytes; alpha is always 255. Valid only after
  /// advance_one_tick() or regenerate_frame().
  [[nodiscard]] std::span<const std::uint8_t> rgba_frame() const {
    return m_frame;
  }

  /// The 256x256 lit intensity field (row 0 = top), one byte 0..63 per cell.
  /// Uploaded as the small dynamic source texture for the GPU warp.
  [[nodiscard]] std::span<const std::uint8_t> lit_field() const {
    return m_lit;
  }

  /// Signed 8-bit X gradient at cell (x, y), precomputed at construction.
  [[nodiscard]] std::int8_t gradient_x(const std::size_t x,
                                       const std::size_t y) const {
    return m_gradient_x.at((y * K_HEIGHT_SIZE) + x);
  }

  /// Signed 8-bit Y gradient at cell (x, y), precomputed at construction.
  [[nodiscard]] std::int8_t gradient_y(const std::size_t x,
                                       const std::size_t y) const {
    return m_gradient_y.at((y * K_HEIGHT_SIZE) + x);
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

  /// The recovered row-warp offset for an arbitrary logical y coordinate
  /// (continuous extension of Runtime's 480-entry table). Used by the GPU
  /// path to build per-physical-row lookup values for widescreen viewports.
  [[nodiscard]] static std::uint8_t row_warp_offset(double phase_a,
      double phase_b,
      double logical_y);

  /// The recovered column-warp offset for an arbitrary logical x coordinate
  /// (continuous extension of Runtime's 640-entry table).
  [[nodiscard]] static std::uint8_t column_warp_offset(double phase_c,
      double phase_d,
      double logical_x);

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
  /// 256 * 256 height-field cells.
  static constexpr std::size_t K_HEIGHT_CELLS{65536};

  /// 256x256 raw height values (row 0 = top).
  std::vector<std::uint8_t> m_height;
  /// Precomputed signed 8-bit X gradients (Runtime's wrapped-neighbour
  /// height difference). Never changes after construction, so the lighting
  /// hot loop no longer recomputes it per tick.
  std::array<std::int8_t, K_HEIGHT_CELLS> m_gradient_x{};
  /// Precomputed signed 8-bit Y gradients; see m_gradient_x.
  std::array<std::int8_t, K_HEIGHT_CELLS> m_gradient_y{};
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

  /// Number of original effect updates applied so far. The derived light
  /// position/phases are linear functions of this index, but the iterative
  /// accumulation in advance_ticks() is kept so the floating-point state is
  /// bit-identical to the recovered Runtime frame sequence.
  std::uint64_t m_tick_index{0};

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

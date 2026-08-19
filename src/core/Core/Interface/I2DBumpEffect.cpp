#include "Core/Interface/I2DBumpEffect.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Interface/I2DBumpEndpoint.hpp"
#include "Core/Omikron/IndexedBmp8.hpp"

// This file transcribes the recovered Runtime math (I2D_Bump.c) as literally
// as possible for reverse-engineering traceability: single-letter loop
// variables mirror the disassembly, formulas keep their mixed operator
// precedence, and index arithmetic widens int to size_t at the access sites.
// NOLINTBEGIN(readability-identifier-length,
//             readability-math-missing-parentheses,
//             bugprone-misplaced-widening-cast,
//             bugprone-implicit-widening-of-multiplication-result)

namespace App::Interface {

namespace {

/// MSVCRT `_ftol` semantics: convert a double to int by truncating toward
/// zero. C++ double→int conversion already truncates toward zero.
int trunc_toward_zero(const double value) {
  return static_cast<int>(value);
}

/// Builds the recovered 64-entry Runtime colour ramp.
///
/// Entries 0..31 (burnt orange → dark brown), local i = 0..31:
///   R = (4049 - 111*i) / 32
///   G = ( 947 -  13*i) / 32
///   B = (  13 +  13*i) / 32
/// Entries 32..63 (dark brown → blue-green/teal), local i = 0..31:
///   R = (608 + 19*i) / 32
///   G = (544 + 94*i) / 32
///   B = (416 + 92*i) / 32
std::array<std::array<std::uint8_t, 4>, 64> build_palette() {
  std::array<std::array<std::uint8_t, 4>, 64> palette{};
  for (int i{0}; i < 32; ++i) {
    const int red{(4049 - 111 * i) / 32};
    const int green{(947 - 13 * i) / 32};
    const int blue{(13 + 13 * i) / 32};
    palette.at(static_cast<std::size_t>(i)) = {
        static_cast<std::uint8_t>(red),
        static_cast<std::uint8_t>(green),
        static_cast<std::uint8_t>(blue),
        255U};
    const int red2{(608 + 19 * i) / 32};
    const int green2{(544 + 94 * i) / 32};
    const int blue2{(416 + 92 * i) / 32};
    palette.at(static_cast<std::size_t>(i + 32)) = {
        static_cast<std::uint8_t>(red2),
        static_cast<std::uint8_t>(green2),
        static_cast<std::uint8_t>(blue2),
        255U};
  }
  return palette;
}

}  // namespace

std::uint8_t I2DBumpEffect::row_warp_offset(const double phase_a,
    const double phase_b,
    const double logical_y) {
  // i = 479 - y; a = phase_a - 0.0043*i; b = phase_b + 0.0067*i;
  // offset = trunc(32.0*cos(b) + 64.0*cos(a)) & 0xFF.
  const double i{479.0 - logical_y};
  const double a{phase_a - 0.0043 * i};
  const double b{phase_b + 0.0067 * i};
  const int value{trunc_toward_zero(32.0 * std::cos(b) + 64.0 * std::cos(a))};
  return static_cast<std::uint8_t>(value & 0xFF);
}

std::uint8_t I2DBumpEffect::column_warp_offset(const double phase_c,
    const double phase_d,
    const double logical_x) {
  // i = 639 - x; c = phase_c + 0.0057*i; d = phase_d - 0.0099*i;
  // offset = trunc(48.0*(cos(c)+cos(d))) & 0xFF.
  const double i{639.0 - logical_x};
  const double c{phase_c + 0.0057 * i};
  const double d{phase_d - 0.0099 * i};
  const int value{trunc_toward_zero(48.0 * (std::cos(c) + std::cos(d)))};
  return static_cast<std::uint8_t>(value & 0xFF);
}

const std::array<std::array<std::uint8_t, 4>, 64>& I2DBumpEffect::palette() {
  static const std::array<std::array<std::uint8_t, 4>, 64> k_palette{build_palette()};
  return k_palette;
}

std::expected<I2DBumpEffect, std::string> I2DBumpEffect::create(Omikron::IndexedBmp8 source) {
  APP_PROFILE_FUNCTION();

  // Runtime treats CLOUD.BMP's raw 8-bit pixel indices as height values and
  // relies on exactly a 256x256 source.
  if (source.width != K_HEIGHT_SIZE || source.height != K_HEIGHT_SIZE) {
    return std::expected<I2DBumpEffect, std::string>{std::unexpect,
        fmt::format("I2DBumpEffect: expected 256x256 height map, got {}x{}",
            source.width,
            source.height)};
  }
  if (source.indices.size() !=
      static_cast<std::size_t>(K_HEIGHT_SIZE) * static_cast<std::size_t>(K_HEIGHT_SIZE)) {
    return std::expected<I2DBumpEffect, std::string>{
        std::unexpect, "I2DBumpEffect: height map pixel count mismatch"};
  }

  I2DBumpEffect effect;
  effect.m_height = std::move(source.indices);
  effect.m_palette = palette();
  effect.m_lit.assign(K_HEIGHT_CELLS, 0U);
  effect.m_frame.assign(static_cast<std::size_t>(K_FRAME_WIDTH) *
                            static_cast<std::size_t>(K_FRAME_HEIGHT) * 4U,
      0U);

  // The height field never changes, so the signed 8-bit X/Y gradients used by
  // the lighting pass are precomputed once here. They preserve Runtime's
  // wrapped-neighbour semantics: right_x = (x + 1) & 0xFF, below_y =
  // (y + 1) & 0xFF.
  for (int y{0}; y < K_HEIGHT_SIZE; ++y) {
    const int below_y{(y + 1) & 0xFF};
    for (int x{0}; x < K_HEIGHT_SIZE; ++x) {
      const int right_x{(x + 1) & 0xFF};
      const std::size_t index{static_cast<std::size_t>(y * K_HEIGHT_SIZE + x)};
      effect.m_gradient_x.at(index) = signed_byte(
          static_cast<int>(effect.m_height.at(index)) -
          static_cast<int>(effect.m_height.at(
              static_cast<std::size_t>(y * K_HEIGHT_SIZE + right_x))));
      effect.m_gradient_y.at(index) = signed_byte(
          static_cast<int>(effect.m_height.at(index)) -
          static_cast<int>(effect.m_height.at(
              static_cast<std::size_t>(below_y * K_HEIGHT_SIZE + x))));
    }
  }
  return effect;
}

void I2DBumpEffect::advance_one_tick() {
  APP_PROFILE_FUNCTION();

  advance_ticks(1);
  regenerate_lighting();
  regenerate_frame();
}

void I2DBumpEffect::advance_ticks(const std::uint32_t ticks) {
  APP_PROFILE_FUNCTION();

  // Advance only the logical state. Each update performs the moving-light
  // derivation (two trig calls) and the four phase increments, but does NOT
  // rebuild the warp tables, light the height field or warp a final image.
  // A slow host frame that has to catch up several 30 Hz ticks therefore
  // costs a handful of floating-point operations per missed tick rather than
  // several complete (and invisible) effect frames.
  //
  // The scalar math is shared with the production GPU renderer via
  // advance_endpoint() so both paths stay bit-identical (Runtime derives the
  // light position from the current angle, then advances the angle).
  advance_endpoint(m_endpoint, ticks);
}

void I2DBumpEffect::regenerate_lighting() {
  APP_PROFILE_FUNCTION();

  // --- Build the 480-entry row warp table ---
  double a{m_endpoint.phase_a};
  double b{m_endpoint.phase_b};
  for (int i{0}; i < K_ROW_WARP_SIZE; ++i) {
    const int value{trunc_toward_zero(32.0 * std::cos(b) + 64.0 * std::cos(a))};
    m_row_warp.at(static_cast<std::size_t>(i)) = static_cast<std::uint8_t>(value & 0xFF);
    a -= 0.0043;
    b += 0.0067;
  }

  // --- Build the 640-entry column warp table ---
  double c{m_endpoint.phase_c};
  double d{m_endpoint.phase_d};
  for (int i{0}; i < K_COLUMN_WARP_SIZE; ++i) {
    const int value{trunc_toward_zero(48.0 * (std::cos(c) + std::cos(d)))};
    m_column_warp.at(static_cast<std::size_t>(i)) = static_cast<std::uint8_t>(value & 0xFF);
    c += 0.0057;
    d -= 0.0099;
  }

  // --- Light the 256x256 height field ---
  // Runtime intensity:
  //   clamp((((dx * (x - light_x)) + (dy * (y - light_y))) >> 5) + 32, 0, 63)
  // with the precomputed signed 8-bit gradients. The inner loop uses raw
  // contiguous pointers (the dimensions are validated once at construction),
  // deliberately avoiding repeated checked indexing on this hot path.
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  const std::int8_t* grad_x{m_gradient_x.data()};
  const std::int8_t* grad_y{m_gradient_y.data()};
  std::uint8_t* lit{m_lit.data()};
  const int light_x{m_endpoint.light_x};
  const int light_y{m_endpoint.light_y};
  for (int y{0}; y < K_HEIGHT_SIZE; ++y) {
    const int y_minus_light_y{y - light_y};
    const std::size_t row_base{static_cast<std::size_t>(y) * K_HEIGHT_SIZE};
    for (int x{0}; x < K_HEIGHT_SIZE; ++x) {
      const std::size_t index{row_base + static_cast<std::size_t>(x)};
      int intensity{arithmetic_shift_right_5(
                        static_cast<int>(grad_x[index]) * (x - light_x) +
                        static_cast<int>(grad_y[index]) * y_minus_light_y) +
                    32};
      intensity = std::clamp(intensity, 0, 63);
      lit[index] = static_cast<std::uint8_t>(intensity);
    }
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

void I2DBumpEffect::regenerate_frame() {
  APP_PROFILE_FUNCTION();

  // --- Warp the lit map into the final 640x480 frame ---
  // Runtime generates both tables in increasing order but consumes them in
  // reverse (row offset = row_warp[479 - y], column offset =
  // column_warp[639 - x]).
  for (int y{0}; y < K_FRAME_HEIGHT; ++y) {
    const int row_offset{m_row_warp.at(static_cast<std::size_t>(K_ROW_WARP_SIZE - 1 - y))};
    for (int x{0}; x < K_FRAME_WIDTH; ++x) {
      const int source_x{(x + row_offset) & 0xFF};
      const int source_y{(y + m_column_warp.at(
                                  static_cast<std::size_t>(K_COLUMN_WARP_SIZE - 1 - x))) &
                         0xFF};

      const std::uint8_t intensity{
          m_lit.at(static_cast<std::size_t>(source_y * K_HEIGHT_SIZE + source_x))};
      const std::array<std::uint8_t, 4>& colour{m_palette.at(intensity)};
      const std::size_t out{((static_cast<std::size_t>(y) * static_cast<std::size_t>(K_FRAME_WIDTH)) +
                             static_cast<std::size_t>(x)) *
                            4U};
      m_frame.at(out + 0U) = colour.at(0);
      m_frame.at(out + 1U) = colour.at(1);
      m_frame.at(out + 2U) = colour.at(2);
      m_frame.at(out + 3U) = 255U;
    }
  }
}

}  // namespace App::Interface

// NOLINTEND(readability-identifier-length,
//           readability-math-missing-parentheses,
//           bugprone-misplaced-widening-cast,
//           bugprone-implicit-widening-of-multiplication-result)

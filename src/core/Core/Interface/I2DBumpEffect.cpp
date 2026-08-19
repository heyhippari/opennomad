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
  effect.m_lit.assign(
      static_cast<std::size_t>(K_HEIGHT_SIZE) * static_cast<std::size_t>(K_HEIGHT_SIZE), 0U);
  effect.m_frame.assign(static_cast<std::size_t>(K_FRAME_WIDTH) *
                            static_cast<std::size_t>(K_FRAME_HEIGHT) * 4U,
      0U);
  return effect;
}

void I2DBumpEffect::advance_one_tick() {
  APP_PROFILE_FUNCTION();

  // --- Moving light ---
  // Runtime: light_x = _ftol(cos(light_angle) * 64.0) + 128; the same for
  // light_y with sin. light_angle then advances by 0.0785 (FSUB against
  // -0.0785 in the assembly, so the effective sign is +).
  m_light_x = trunc_toward_zero(std::cos(m_light_angle) * 64.0) + 128;
  m_light_y = trunc_toward_zero(std::sin(m_light_angle) * 64.0) + 128;
  m_light_angle += 0.0785;

  // --- Warp-frame phases ---
  m_phase_a += 0.009925;
  m_phase_b -= 0.013915;
  m_phase_c -= 0.007685;
  m_phase_d += 0.015635;

  // --- Build the 480-entry row warp table ---
  double a{m_phase_a};
  double b{m_phase_b};
  for (int i{0}; i < K_ROW_WARP_SIZE; ++i) {
    const int value{trunc_toward_zero(32.0 * std::cos(b) + 64.0 * std::cos(a))};
    m_row_warp.at(static_cast<std::size_t>(i)) = static_cast<std::uint8_t>(value & 0xFF);
    a -= 0.0043;
    b += 0.0067;
  }

  // --- Build the 640-entry column warp table ---
  double c{m_phase_c};
  double d{m_phase_d};
  for (int i{0}; i < K_COLUMN_WARP_SIZE; ++i) {
    const int value{trunc_toward_zero(48.0 * (std::cos(c) + std::cos(d)))};
    m_column_warp.at(static_cast<std::size_t>(i)) = static_cast<std::uint8_t>(value & 0xFF);
    c += 0.0057;
    d -= 0.0099;
  }

  // --- Light the 256x256 height field ---
  // Runtime intensity:
  //   clamp((((dx * (x - light_x)) + (dy * (y - light_y))) >> 5) + 32, 0, 63)
  // with signed 8-bit gradients and wrapped neighbour coordinates.
  for (int y{0}; y < K_HEIGHT_SIZE; ++y) {
    for (int x{0}; x < K_HEIGHT_SIZE; ++x) {
      const int right_x{(x + 1) & 0xFF};
      const int below_y{(y + 1) & 0xFF};

      const std::int8_t dx{signed_byte(
          static_cast<int>(m_height.at(static_cast<std::size_t>(y * K_HEIGHT_SIZE + x))) -
          static_cast<int>(m_height.at(static_cast<std::size_t>(y * K_HEIGHT_SIZE + right_x))))};
      const std::int8_t dy{signed_byte(
          static_cast<int>(m_height.at(static_cast<std::size_t>(y * K_HEIGHT_SIZE + x))) -
          static_cast<int>(m_height.at(static_cast<std::size_t>(below_y * K_HEIGHT_SIZE + x))))};

      int intensity{arithmetic_shift_right_5(
                        static_cast<int>(dx) * (x - m_light_x) +
                        static_cast<int>(dy) * (y - m_light_y)) +
                    32};
      intensity = std::clamp(intensity, 0, 63);
      m_lit.at(static_cast<std::size_t>(y * K_HEIGHT_SIZE + x)) =
          static_cast<std::uint8_t>(intensity);
    }
  }

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

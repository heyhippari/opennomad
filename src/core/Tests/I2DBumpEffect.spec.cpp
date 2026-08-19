#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "Core/Interface/I2DBumpEffect.hpp"
#include "Core/Omikron/IndexedBmp8.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace,
//             cppcoreguidelines-avoid-do-while,
//             cert-err33-c,
//             readability-identifier-length,
//             readability-math-missing-parentheses,
//             bugprone-implicit-widening-of-multiplication-result,
//             cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

namespace {

/// Builds a 256x256 height map whose every cell is `value`.
App::Omikron::IndexedBmp8 flat_height_map(const std::uint8_t value) {
  return App::Omikron::IndexedBmp8{
      .width = 256,
      .height = 256,
      .indices = std::vector<std::uint8_t>(256U * 256U, value)};
}

/// Builds a 256x256 height map with every cell 0 except the top-left (row 0,
/// column 0) set to `value`. Used to exercise neighbour wrapping at x=255 /
/// y=255 together with signed-byte gradient wrap.
App::Omikron::IndexedBmp8 corner_height_map(const std::uint8_t value) {
  std::vector<std::uint8_t> indices(256U * 256U, 0U);
  indices.at(0) = value;
  return App::Omikron::IndexedBmp8{
      .width = 256, .height = 256, .indices = std::move(indices)};
}

}  // namespace

TEST_SUITE("Core::Interface::I2DBumpEffect") {
  TEST_CASE("Colour ramp matches the recovered integer formulas") {
    const auto& palette{App::Interface::I2DBumpEffect::palette()};

    const auto check_entry{[](const std::array<std::uint8_t, 4>& entry,
                               const std::uint8_t red,
                               const std::uint8_t green,
                               const std::uint8_t blue) {
      CHECK_EQ(entry.at(0), red);
      CHECK_EQ(entry.at(1), green);
      CHECK_EQ(entry.at(2), blue);
      CHECK_EQ(entry.at(3), 255);
    }};

    check_entry(palette.at(0), 126, 29, 0);
    check_entry(palette.at(31), 19, 17, 13);
    check_entry(palette.at(32), 19, 17, 13);
    check_entry(palette.at(63), 37, 108, 102);
  }

  TEST_CASE("First tick produces the recovered light position") {
    auto effect{App::Interface::I2DBumpEffect::create(flat_height_map(0))};
    REQUIRE(effect.has_value());

    effect->advance_one_tick();

    CHECK_EQ(effect->light_x(), 75);
    CHECK_EQ(effect->light_y(), 94);
  }

  TEST_CASE("First tick advances the four warp phases") {
    auto effect{App::Interface::I2DBumpEffect::create(flat_height_map(0))};
    REQUIRE(effect.has_value());

    effect->advance_one_tick();

    CHECK(effect->phase_a() == doctest::Approx(0.009925).epsilon(1e-12));
    CHECK(effect->phase_b() == doctest::Approx(0.986085).epsilon(1e-12));
    CHECK(effect->phase_c() == doctest::Approx(1.992315).epsilon(1e-12));
    CHECK(effect->phase_d() == doctest::Approx(0.515635).epsilon(1e-12));
  }

  TEST_CASE("First tick builds the recovered warp-table anchors") {
    auto effect{App::Interface::I2DBumpEffect::create(flat_height_map(0))};
    REQUIRE(effect.has_value());

    effect->advance_one_tick();

    CHECK_EQ(effect->row_warp(0), 81);
    CHECK_EQ(effect->row_warp(479), 211);
    CHECK_EQ(effect->column_warp(0), 22);
    // column_warp[639] sits ~0.01 from the integer boundary between 80 and
    // 81; platform libm precision decides which side it lands on.
    const auto column_last{effect->column_warp(639)};
    CHECK((column_last == 80U || column_last == 81U));
  }

  TEST_CASE("Final warp consumes the lookup tables in reverse order") {
    auto effect{App::Interface::I2DBumpEffect::create(flat_height_map(0))};
    REQUIRE(effect.has_value());

    effect->advance_one_tick();

    const auto [source_x_origin, source_y_origin]{effect->warp_source_coordinates(0, 0)};
    CHECK_EQ(source_x_origin, 211);
    CHECK((source_y_origin == 80 || source_y_origin == 81));

    const auto [source_x_bottom, source_y_bottom]{effect->warp_source_coordinates(639, 479)};
    CHECK_EQ(source_x_bottom, 208);
    CHECK_EQ(source_y_bottom, 245);
  }

  TEST_CASE("Flat height map yields intensity 32 and a uniform frame") {
    auto effect{App::Interface::I2DBumpEffect::create(flat_height_map(7))};
    REQUIRE(effect.has_value());

    effect->advance_one_tick();

    // dx = dy = 0 for every cell, so intensity = sar5(0) + 32 = 32.
    for (int y{0}; y < 256; ++y) {
      for (int x{0}; x < 256; ++x) {
        CHECK_EQ(effect->lit_intensity(static_cast<std::size_t>(x),
                      static_cast<std::size_t>(y)),
            32);
      }
    }

    // The generated frame is uniformly palette[32] = (19, 17, 13, 255),
    // regardless of the warp.
    const auto frame{effect->rgba_frame()};
    REQUIRE_EQ(frame.size(), 640U * 480U * 4U);
    const std::array<std::uint8_t, 4> expected{19, 17, 13, 255};
    for (const std::size_t pixel : {std::size_t{0},
             std::size_t{(320U * 640U + 240U) * 4U},
             frame.size() - 4U}) {
      for (std::size_t channel{0}; channel < 4U; ++channel) {
        CHECK_EQ(frame[pixel + channel], expected.at(channel));
      }
    }
  }

  TEST_CASE("Signed 8-bit gradient wrap matches Runtime") {
    using App::Interface::I2DBumpEffect;
    CHECK_EQ(I2DBumpEffect::signed_byte(0 - 200), 56);  // -200 wraps to +56.
    CHECK_EQ(I2DBumpEffect::signed_byte(127), 127);
    CHECK_EQ(I2DBumpEffect::signed_byte(128), -128);    // crosses 127.
    CHECK_EQ(I2DBumpEffect::signed_byte(-128), -128);
    CHECK_EQ(I2DBumpEffect::signed_byte(-129), 127);    // crosses -128.
    CHECK_EQ(I2DBumpEffect::signed_byte(0), 0);
  }

  TEST_CASE("Arithmetic right shift by 5 rounds toward negative infinity") {
    using App::Interface::I2DBumpEffect;
    CHECK_EQ(I2DBumpEffect::arithmetic_shift_right_5(32), 1);
    CHECK_EQ(I2DBumpEffect::arithmetic_shift_right_5(31), 0);
    CHECK_EQ(I2DBumpEffect::arithmetic_shift_right_5(0), 0);
    CHECK_EQ(I2DBumpEffect::arithmetic_shift_right_5(-1), -1);
    CHECK_EQ(I2DBumpEffect::arithmetic_shift_right_5(-32), -1);
    CHECK_EQ(I2DBumpEffect::arithmetic_shift_right_5(-33), -2);
    CHECK_EQ(I2DBumpEffect::arithmetic_shift_right_5(-64), -2);
    CHECK_EQ(I2DBumpEffect::arithmetic_shift_right_5(-65), -3);
  }

  TEST_CASE("Edge wrapping and signed gradient wrap through the lighting pass") {
    auto effect{App::Interface::I2DBumpEffect::create(corner_height_map(200))};
    REQUIRE(effect.has_value());

    effect->advance_one_tick();

    // At x=255, y=0 the X neighbour wraps to x=0: dx = signed_byte(0 - 200)
    // = 56; dy = 0. intensity = clamp(sar5(56 * (255 - 75)) + 32) = 63.
    CHECK_EQ(effect->lit_intensity(255, 0), 63);
    // At x=0, y=255 the Y neighbour wraps to y=0: dy = signed_byte(0 - 200)
    // = 56; dx = 0. intensity = clamp(sar5(56 * (255 - 94)) + 32) = 63.
    CHECK_EQ(effect->lit_intensity(0, 255), 63);
  }

  TEST_CASE("Rejects a non-256x256 height map") {
    App::Omikron::IndexedBmp8 wrong{
        .width = 256, .height = 128, .indices = std::vector<std::uint8_t>(256U * 128U, 0U)};
    const auto effect{App::Interface::I2DBumpEffect::create(std::move(wrong))};
    CHECK_FALSE(effect.has_value());
  }
}

// NOLINTEND(misc-use-anonymous-namespace,
//           cppcoreguidelines-avoid-do-while,
//           cert-err33-c,
//           readability-identifier-length,
//           readability-math-missing-parentheses,
//           bugprone-implicit-widening-of-multiplication-result,
//           cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

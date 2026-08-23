#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "Core/Omikron/FontFNT.hpp"

namespace {

constexpr std::size_t K_TABLE_SIZE{App::Omikron::FontFNT::k_descriptor_table_size};

void write_u16(std::vector<std::byte>& data, const std::size_t offset, const std::uint16_t value) {
  data.at(offset) = static_cast<std::byte>(value & 0xFFU);
  data.at(offset + 1U) = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void write_glyph(std::vector<std::byte>& data,
    const std::uint8_t index,
    const std::uint16_t block,
    const std::int16_t vertical_offset,
    const std::uint16_t width,
    const std::uint16_t height) {
  const std::size_t descriptor{static_cast<std::size_t>(index) * 8U};
  write_u16(data, descriptor, block);
  write_u16(data, descriptor + 2U, static_cast<std::uint16_t>(vertical_offset));
  write_u16(data, descriptor + 4U, width);
  write_u16(data, descriptor + 6U, height);
}

std::vector<std::byte> make_valid_font() {
  std::vector<std::byte> data(K_TABLE_SIZE + 16U, std::byte{});
  write_glyph(data, 'A', 0x100U, -2, 2U, 2U);
  data.at(K_TABLE_SIZE) = std::byte{0U};
  data.at(K_TABLE_SIZE + 1U) = std::byte{1U};
  data.at(K_TABLE_SIZE + 2U) = std::byte{15U};
  data.at(K_TABLE_SIZE + 3U) = std::byte{31U};
  write_glyph(data, 'B', 0x101U, 1, 2U, 1U);
  data.at(K_TABLE_SIZE + 8U) = std::byte{7U};
  data.at(K_TABLE_SIZE + 9U) = std::byte{8U};
  return data;
}

}  // namespace

TEST_CASE("FNT parses the fixed descriptor table and aligned row-major bitmaps") {
  const std::vector<std::byte> bytes{make_valid_font()};
  const auto parsed{App::Omikron::FontFNT::load(bytes)};
  REQUIRE(parsed.has_value());

  const auto& glyph_a{parsed->glyphs.at(static_cast<std::uint8_t>('A'))};
  CHECK(glyph_a.data_block == 0x100U);
  CHECK(glyph_a.vertical_offset == -2);
  CHECK(glyph_a.width == 2U);
  CHECK(glyph_a.height == 2U);
  CHECK(glyph_a.coverage == std::vector<std::uint8_t>{0U, 1U, 15U, 31U});

  const auto& glyph_b{parsed->glyphs.at(static_cast<std::uint8_t>('B'))};
  CHECK(static_cast<std::size_t>(glyph_b.data_block) * 8U == K_TABLE_SIZE + 8U);
  const std::size_t glyph_a_end{
      (static_cast<std::size_t>(glyph_a.data_block) * 8U) + glyph_a.coverage.size()};
  CHECK(((glyph_a_end + 7U) & ~std::size_t{7U}) ==
      static_cast<std::size_t>(glyph_b.data_block) * 8U);
  CHECK(glyph_b.coverage == std::vector<std::uint8_t>{7U, 8U});
  CHECK(parsed->glyphs.at(static_cast<std::uint8_t>(' ')).coverage.empty());
}

TEST_CASE("FNT rejects truncated descriptors and invalid bitmap locations") {
  const std::vector<std::byte> short_file(K_TABLE_SIZE - 1U, std::byte{});
  CHECK_FALSE(App::Omikron::FontFNT::load(short_file).has_value());

  std::vector<std::byte> overlap(K_TABLE_SIZE, std::byte{});
  write_glyph(overlap, 'A', 1U, 0, 1U, 1U);
  CHECK_FALSE(App::Omikron::FontFNT::load(overlap).has_value());

  std::vector<std::byte> truncated(K_TABLE_SIZE, std::byte{});
  write_glyph(truncated, 'A', 0x100U, 0, 2U, 2U);
  CHECK_FALSE(App::Omikron::FontFNT::load(truncated).has_value());
}

TEST_CASE("FNT checked products and five-bit intensities are validated") {
  const auto overflow{App::Omikron::FontFNT::checked_product(
      std::numeric_limits<std::size_t>::max(), 2U)};
  CHECK_FALSE(overflow.has_value());

  std::vector<std::byte> invalid{K_TABLE_SIZE + 8U, std::byte{}};
  write_glyph(invalid, 'A', 0x100U, 0, 1U, 1U);
  invalid.at(K_TABLE_SIZE) = std::byte{32U};
  CHECK_FALSE(App::Omikron::FontFNT::load(invalid).has_value());
}

TEST_CASE("Retail FNT metrics use registry spacing and byte indices") {
  App::Omikron::FontFntData font;
  auto& glyph_a{font.glyphs.at(static_cast<std::uint8_t>('A'))};
  glyph_a.width = 13U;
  glyph_a.height = 13U;
  glyph_a.vertical_offset = -2;
  glyph_a.coverage.resize(169U);

  auto& high_byte{font.glyphs.at(0xE9U)};
  high_byte.width = 10U;
  high_byte.height = 12U;
  high_byte.coverage.resize(120U);

  CHECK(App::Omikron::fnt_glyph_advance(glyph_a, 1, 6) == doctest::Approx(14.0F));
  CHECK(App::Omikron::fnt_glyph_advance(
            font.glyphs.at(static_cast<std::uint8_t>(' ')), 1, 6) ==
      doctest::Approx(7.0F));
  CHECK(App::Omikron::measure_fnt_bytes(font, "A A", 1, 6) == doctest::Approx(35.0F));
  const std::string encoded(1U, static_cast<char>(0xE9U));
  CHECK(App::Omikron::measure_fnt_bytes(font, encoded, 1, 6) == doctest::Approx(11.0F));
  CHECK(App::Omikron::fnt_glyph_top(glyph_a, 0.0F, 17) == doctest::Approx(2.0F));
}

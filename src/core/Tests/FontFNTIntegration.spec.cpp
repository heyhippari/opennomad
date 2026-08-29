#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
// misc-include-cleaner)

#include <cstddef>
#include <cstdint>

#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/FontFNT.hpp"

namespace {

std::size_t non_empty_count(const App::Omikron::FontFntData& font) {
  std::size_t count{0};
  for (const App::Omikron::FontFntGlyph& glyph : font.glyphs) {
    count += static_cast<std::size_t>(!glyph.empty());
  }
  return count;
}

}  // namespace

TEST_CASE("[RETAIL] DIALOGUE.FNT matches recovered facts") {
  const auto file{App::load_game_file("FONTS/DIALOGUE.FNT")};
  REQUIRE_MESSAGE(file.has_value(), file.error());
  REQUIRE(file->bytes.size() == 0x6FA0U);
  const auto font{App::Omikron::FontFNT::load(file->bytes)};
  REQUIRE_MESSAGE(font.has_value(), font.error());
  CHECK(non_empty_count(font.value()) == 223U);
  const auto& glyph_a{font->glyphs.at(65U)};
  CHECK(glyph_a.data_block == 619U);
  CHECK(glyph_a.vertical_offset == -2);
  CHECK(glyph_a.width == 13U);
  CHECK(glyph_a.height == 13U);
  const auto& last{font->glyphs.at(255U)};
  CHECK((static_cast<std::size_t>(last.data_block) * 8U) + last.coverage.size() ==
        file->bytes.size());
}

TEST_CASE("[RETAIL] DIALSELE.FNT matches recovered facts") {
  const auto file{App::load_game_file("FONTS/DIALSELE.FNT")};
  REQUIRE_MESSAGE(file.has_value(), file.error());
  REQUIRE(file->bytes.size() == 0x8608U);
  const auto font{App::Omikron::FontFNT::load(file->bytes)};
  REQUIRE_MESSAGE(font.has_value(), font.error());
  CHECK(non_empty_count(font.value()) == 223U);
  const auto& glyph_a{font->glyphs.at(65U)};
  CHECK(glyph_a.data_block == 661U);
  CHECK(glyph_a.vertical_offset == -2);
  CHECK(glyph_a.width == 11U);
  CHECK(glyph_a.height == 13U);
  const auto& last{font->glyphs.at(255U)};
  CHECK((static_cast<std::size_t>(last.data_block) * 8U) + last.coverage.size() ==
        file->bytes.size());
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
// misc-include-cleaner)

#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
// misc-include-cleaner, concurrency-mt-unsafe)

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "Core/Omikron/FontFNT.hpp"
#include "Core/Resources.hpp"

namespace {

std::optional<std::vector<std::byte>> load_font(const std::string_view name) {
  const char* root{std::getenv("OPENNOMAD_GAME_DATA_ROOT")};
  if (root == nullptr) {
    return std::nullopt;
  }
  const std::filesystem::path path{App::Resources::resolve_case_insensitive(
      std::filesystem::path{root} / "FONTS" / std::filesystem::path{name})};
  std::size_t size{0};
  void* raw{SDL_LoadFile(path.string().c_str(), &size)};
  if (raw == nullptr) {
    return std::nullopt;
  }
  std::vector<std::byte> bytes(size);
  if (size > 0U) {
    std::memcpy(bytes.data(), raw, size);
  }
  SDL_free(raw);
  return bytes;
}

std::size_t non_empty_count(const App::Omikron::FontFntData& font) {
  std::size_t count{0};
  for (const App::Omikron::FontFntGlyph& glyph : font.glyphs) {
    count += static_cast<std::size_t>(!glyph.empty());
  }
  return count;
}

}  // namespace

TEST_CASE("Retail DIALOGUE.FNT matches recovered facts") {
  const auto bytes{load_font("DIALOGUE.FNT")};
  if (!bytes.has_value()) {
    WARN("OPENNOMAD_GAME_DATA_ROOT is unset or DIALOGUE.FNT is missing; test skipped");
    return;
  }
  REQUIRE(bytes->size() == 0x6FA0U);
  const auto font{App::Omikron::FontFNT::load(bytes.value())};
  REQUIRE(font.has_value());
  CHECK(non_empty_count(font.value()) == 223U);
  const auto& glyph_a{font->glyphs.at(65U)};
  CHECK(glyph_a.data_block == 619U);
  CHECK(glyph_a.vertical_offset == -2);
  CHECK(glyph_a.width == 13U);
  CHECK(glyph_a.height == 13U);
  const auto& last{font->glyphs.at(255U)};
  CHECK((static_cast<std::size_t>(last.data_block) * 8U) + last.coverage.size() == bytes->size());
}

TEST_CASE("Retail DIALSELE.FNT matches recovered facts") {
  const auto bytes{load_font("DIALSELE.FNT")};
  if (!bytes.has_value()) {
    WARN("OPENNOMAD_GAME_DATA_ROOT is unset or DIALSELE.FNT is missing; test skipped");
    return;
  }
  REQUIRE(bytes->size() == 0x8608U);
  const auto font{App::Omikron::FontFNT::load(bytes.value())};
  REQUIRE(font.has_value());
  CHECK(non_empty_count(font.value()) == 223U);
  const auto& glyph_a{font->glyphs.at(65U)};
  CHECK(glyph_a.data_block == 661U);
  CHECK(glyph_a.vertical_offset == -2);
  CHECK(glyph_a.width == 11U);
  CHECK(glyph_a.height == 13U);
  const auto& last{font->glyphs.at(255U)};
  CHECK((static_cast<std::size_t>(last.data_block) * 8U) + last.coverage.size() == bytes->size());
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
// misc-include-cleaner, concurrency-mt-unsafe)

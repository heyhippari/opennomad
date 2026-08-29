#include "Core/Omikron/FontFNT.hpp"

#include <fmt/format.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>

#include "Core/Omikron/BinaryReader.hpp"

namespace App::Omikron {

std::expected<std::size_t, std::string> FontFNT::checked_product(
    const std::size_t left, const std::size_t right) {
  if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "FNT glyph dimensions overflow host size"};
  }
  return left * right;
}

std::expected<FontFntData, std::string> FontFNT::load(const std::span<const std::byte> data) {
  if (data.size() < k_descriptor_table_size) {
    return std::expected<FontFntData, std::string>{std::unexpect,
        fmt::format("FNT descriptor table requires {} bytes, file has {}",
            k_descriptor_table_size,
            data.size())};
  }

  FontFntData font;
  BinaryReader reader{data.first(k_descriptor_table_size)};
  for (std::size_t index{0}; index < font.glyphs.size(); ++index) {
    FontFntGlyph& glyph{font.glyphs.at(index)};
    glyph.data_block = reader.read_u16();
    glyph.vertical_offset = std::bit_cast<std::int16_t>(reader.read_u16());
    glyph.width = reader.read_u16();
    glyph.height = reader.read_u16();
  }
  if (reader.has_error()) {
    return std::expected<FontFntData, std::string>{
        std::unexpect, fmt::format("FNT descriptor table: {}", reader.error())};
  }

  for (std::size_t index{0}; index < font.glyphs.size(); ++index) {
    FontFntGlyph& glyph{font.glyphs.at(index)};
    if (glyph.empty()) {
      continue;
    }

    auto bitmap_size{checked_product(
        static_cast<std::size_t>(glyph.width), static_cast<std::size_t>(glyph.height))};
    if (!bitmap_size) {
      return std::expected<FontFntData, std::string>{
          std::unexpect, fmt::format("FNT glyph {}: {}", index, bitmap_size.error())};
    }
    auto bitmap_offset{checked_product(static_cast<std::size_t>(glyph.data_block), 8U)};
    if (!bitmap_offset) {
      return std::expected<FontFntData, std::string>{
          std::unexpect, fmt::format("FNT glyph {}: {}", index, bitmap_offset.error())};
    }
    if (bitmap_offset.value() < k_descriptor_table_size) {
      return std::expected<FontFntData, std::string>{std::unexpect,
          fmt::format("FNT glyph {} bitmap offset {} overlaps descriptor table",
              index,
              bitmap_offset.value())};
    }
    if (bitmap_offset.value() > data.size() ||
        bitmap_size.value() > data.size() - bitmap_offset.value()) {
      return std::expected<FontFntData, std::string>{std::unexpect,
          fmt::format("FNT glyph {} bitmap [{}..{}) exceeds file size {}",
              index,
              bitmap_offset.value(),
              bitmap_offset.value() + bitmap_size.value(),
              data.size())};
    }

    const std::span<const std::byte> bitmap{
        data.subspan(bitmap_offset.value(), bitmap_size.value())};
    glyph.coverage.reserve(bitmap.size());
    for (const std::byte value : bitmap) {
      const std::uint8_t intensity{std::to_integer<std::uint8_t>(value)};
      if (intensity > 31U) {
        return std::expected<FontFntData, std::string>{std::unexpect,
            fmt::format("FNT glyph {} has intensity {} outside 0..31", index, intensity)};
      }
      glyph.coverage.push_back(intensity);
    }
  }
  return font;
}

float fnt_glyph_advance(
    const FontFntGlyph& glyph, const int letter_spacing, const int blank_width) {
  const int base{glyph.empty() ? blank_width : static_cast<int>(glyph.width)};
  return static_cast<float>(base + letter_spacing);
}

float measure_fnt_bytes(const FontFntData& font,
    const std::string_view text,
    const int letter_spacing,
    const int blank_width) {
  float width{0.0F};
  for (const char character : text) {
    const auto index{static_cast<unsigned char>(character)};
    width += fnt_glyph_advance(font.glyphs.at(index), letter_spacing, blank_width);
  }
  return width;
}

float fnt_glyph_top(const FontFntGlyph& glyph, const float line_y, const int line_height) {
  const float baseline{line_y + static_cast<float>(line_height)};
  return baseline + static_cast<float>(glyph.vertical_offset) - static_cast<float>(glyph.height);
}

}  // namespace App::Omikron

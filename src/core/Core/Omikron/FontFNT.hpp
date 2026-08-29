#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace App::Omikron {

/// One byte-indexed glyph from a retail Omikron .FNT resource.
struct FontFntGlyph {
  std::uint16_t data_block{0};
  std::int16_t vertical_offset{0};
  std::uint16_t width{0};
  std::uint16_t height{0};
  std::vector<std::uint8_t> coverage;

  [[nodiscard]] bool empty() const {
    return width == 0U || height == 0U;
  }
};

/// Complete 256-entry retail font, including immutable glyph coverage.
struct FontFntData {
  std::array<FontFntGlyph, 256> glyphs;
};

/// Parser and checked arithmetic for the headerless retail .FNT format.
class FontFNT {
 public:
  static constexpr std::size_t k_descriptor_table_size{0x800U};

  /// Parses all descriptors and bitmap payloads from an in-memory file.
  [[nodiscard]] static std::expected<FontFntData, std::string> load(
      std::span<const std::byte> data);

  /// Multiplies two sizes while reporting overflow. Public so parser boundary
  /// behaviour can be covered without allocating an impossible-size fixture.
  [[nodiscard]] static std::expected<std::size_t, std::string> checked_product(
      std::size_t left, std::size_t right);
};

/// Runtime horizontal advance for a retail glyph and registry metrics.
[[nodiscard]] float fnt_glyph_advance(
    const FontFntGlyph& glyph, int letter_spacing, int blank_width);

/// Measures byte-indexed retail text. Bytes >= 0x80 are glyph indices, not
/// UTF-8 lead bytes.
[[nodiscard]] float measure_fnt_bytes(
    const FontFntData& font, std::string_view text, int letter_spacing, int blank_width);

/// Runtime top edge for a glyph on a logical line whose top is `line_y`.
[[nodiscard]] float fnt_glyph_top(const FontFntGlyph& glyph, float line_y, int line_height);

}  // namespace App::Omikron

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace App::Omikron {

/// One decoded Windows BMP image, converted to tightly packed RGBA8.
struct BmpImage {
  std::int32_t width{0};
  std::int32_t height{0};
  /// width * height * 4 bytes RGBA8. Rows are stored bottom-up so the first
  /// row in the buffer is the bottom of the image, matching glTexImage2D's
  /// row order (the decoder flips SDL's top-down surfaces accordingly).
  std::vector<std::uint8_t> rgba8;
};

/// Decoder for the Windows BMP loading screens shipped in the game's
/// IMAGES/ folder (OMIKRON.BMP and friends).
class BmpImageDecoder {
 public:
  /// Decodes a BMP from memory through SDL's loader (8/24/32 bpp, palettes
  /// and RLE variants included) and converts the pixels to RGBA8.
  [[nodiscard]] static std::expected<BmpImage, std::string> load(std::span<const std::byte> data);
};

}  // namespace App::Omikron

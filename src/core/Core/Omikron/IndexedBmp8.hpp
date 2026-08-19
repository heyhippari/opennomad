#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace App::Omikron {

/// One decoded 8-bit indexed Windows BMP, preserving the raw palette indices.
///
/// Unlike BmpImage (which resolves the palette into RGBA8), this keeps the
/// original pixel bytes. The I2D bump effect needs exactly this: Runtime
/// treats CLOUD.BMP's raw 8-bit pixel values as height-map samples, not as
/// resolved display colours.
struct IndexedBmp8 {
  std::int32_t width{0};
  std::int32_t height{0};
  /// width * height palette indices. Row 0 = logical top of the image.
  std::vector<std::uint8_t> indices;
};

/// Decoder for uncompressed (BI_RGB) 8-bit indexed BMPs.
///
/// Handles bottom-up and top-down storage (positive/negative DIB height) and
/// 4-byte row padding. The colour palette is validated for presence but is
/// not resolved; only the raw pixel indices are preserved.
class IndexedBmp8Decoder {
 public:
  [[nodiscard]] static std::expected<IndexedBmp8, std::string> load(
      std::span<const std::byte> data);
};

}  // namespace App::Omikron

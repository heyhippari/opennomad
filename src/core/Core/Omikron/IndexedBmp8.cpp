#include "Core/Omikron/IndexedBmp8.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Omikron/BinaryReader.hpp"

namespace App::Omikron {

namespace {

/// 'BM' little-endian signature.
constexpr std::uint16_t K_BMP_SIGNATURE{0x4D42};
/// Minimum DIB header size we understand (BITMAPINFOHEADER).
constexpr std::uint32_t K_BITMAPINFOHEADER_SIZE{40};
constexpr std::uint16_t K_BITS_PER_PIXEL_8{8};
constexpr std::uint16_t K_PLANES_1{1};
constexpr std::uint32_t K_BI_RGB{0};
/// Full palette size for an 8-bit bitmap.
constexpr std::uint32_t K_PALETTE_ENTRIES{256};
constexpr std::uint32_t K_PALETTE_ENTRY_BYTES{4};

/// Copies one row of `width` index bytes (ignoring the padded stride) into
/// `indices` at `destination_row`, converting std::byte to the raw index
/// value.
void copy_row(std::vector<std::uint8_t>& indices,
    const std::size_t width,
    const std::size_t destination_row,
    const std::span<const std::byte> source) {
  const std::size_t offset{destination_row * width};
  for (std::size_t column{0}; column < width; ++column) {
    // std::span has no checked element access; the caller validated the span
    // length against `width`.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    indices.at(offset + column) = std::to_integer<std::uint8_t>(source[column]);
  }
}

}  // namespace

std::expected<IndexedBmp8, std::string> IndexedBmp8Decoder::load(
    const std::span<const std::byte> data) {
  APP_PROFILE_FUNCTION();

  if (data.empty()) {
    return std::expected<IndexedBmp8, std::string>{std::unexpect, "IndexedBmp8: no data"};
  }

  BinaryReader reader{data};

  // BITMAPFILEHEADER.
  const std::uint16_t signature{reader.read_u16()};
  if (signature != K_BMP_SIGNATURE) {
    return std::expected<IndexedBmp8, std::string>{std::unexpect, "IndexedBmp8: not a BMP"};
  }
  reader.skip(4);  // file size.
  reader.skip(2);  // reserved.
  reader.skip(2);  // reserved.
  const std::uint32_t pixel_offset{reader.read_u32()};

  // BITMAPINFOHEADER (the only header size this loader understands).
  const std::uint32_t dib_size{reader.read_u32()};
  if (dib_size < K_BITMAPINFOHEADER_SIZE) {
    return std::expected<IndexedBmp8, std::string>{std::unexpect,
        fmt::format("IndexedBmp8: unsupported DIB header size {}", dib_size)};
  }
  const std::int32_t width{reader.read_i32()};
  const std::int32_t height{reader.read_i32()};
  const std::uint16_t planes{reader.read_u16()};
  const std::uint16_t bits_per_pixel{reader.read_u16()};
  const std::uint32_t compression{reader.read_u32()};
  reader.skip(4);  // size image.
  reader.skip(4);  // x pixels per metre.
  reader.skip(4);  // y pixels per metre.
  const std::uint32_t colours_used{reader.read_u32()};
  reader.skip(4);  // colours important.

  if (reader.has_error()) {
    return std::expected<IndexedBmp8, std::string>{std::unexpect,
        fmt::format("IndexedBmp8: truncated header: {}", reader.error())};
  }

  // Larger (V4/V5) headers append fields before the palette; skip them.
  if (dib_size > K_BITMAPINFOHEADER_SIZE) {
    reader.skip(static_cast<std::size_t>(dib_size - K_BITMAPINFOHEADER_SIZE));
  }

  if (width <= 0) {
    return std::expected<IndexedBmp8, std::string>{
        std::unexpect, "IndexedBmp8: non-positive width"};
  }
  if (height == 0) {
    return std::expected<IndexedBmp8, std::string>{
        std::unexpect, "IndexedBmp8: zero height"};
  }
  if (planes != K_PLANES_1) {
    return std::expected<IndexedBmp8, std::string>{std::unexpect,
        fmt::format("IndexedBmp8: expected 1 plane, got {}", planes)};
  }
  if (bits_per_pixel != K_BITS_PER_PIXEL_8) {
    return std::expected<IndexedBmp8, std::string>{std::unexpect,
        fmt::format("IndexedBmp8: expected 8 bits per pixel, got {}", bits_per_pixel)};
  }
  if (compression != K_BI_RGB) {
    return std::expected<IndexedBmp8, std::string>{std::unexpect,
        fmt::format("IndexedBmp8: expected uncompressed BI_RGB, got {}", compression)};
  }

  // 8-bit BI_RGB bitmaps have a 256-entry palette between the DIB header and
  // the pixel array (colours_used may legitimately be 0, meaning 256).
  const std::uint32_t palette_entries{colours_used != 0U ? colours_used : K_PALETTE_ENTRIES};
  if (palette_entries > K_PALETTE_ENTRIES) {
    return std::expected<IndexedBmp8, std::string>{std::unexpect,
        fmt::format("IndexedBmp8: implausible palette size {}", palette_entries)};
  }
  reader.skip(static_cast<std::size_t>(palette_entries) * K_PALETTE_ENTRY_BYTES);

  const std::size_t absolute_height{
      height < 0 ? static_cast<std::size_t>(-height) : static_cast<std::size_t>(height)};
  const std::size_t row_stride{
      ((static_cast<std::size_t>(width) + 3U) / 4U) * 4U};

  reader.seek(pixel_offset);
  if (reader.has_error()) {
    return std::expected<IndexedBmp8, std::string>{std::unexpect,
        fmt::format("IndexedBmp8: pixel data offset {} out of range", pixel_offset)};
  }

  std::vector<std::uint8_t> indices(static_cast<std::size_t>(width) * absolute_height);
  for (std::size_t row{0}; row < absolute_height; ++row) {
    // Row 0 in the output is the logical top of the image. Bottom-up storage
    // (positive height) stores row 0 of the file as the bottom; top-down
    // storage (negative height) stores row 0 of the file as the top.
    const std::size_t file_row{height > 0 ? absolute_height - row - 1U : row};
    reader.seek(static_cast<std::size_t>(pixel_offset) + (file_row * row_stride));
    const std::span<const std::byte> row_bytes{
        reader.read_bytes(static_cast<std::size_t>(width))};
    if (reader.has_error()) {
      return std::expected<IndexedBmp8, std::string>{std::unexpect,
          fmt::format("IndexedBmp8: truncated pixel data: {}", reader.error())};
    }
    copy_row(indices, static_cast<std::size_t>(width), row, row_bytes);
  }

  return IndexedBmp8{
      .width = width, .height = height, .indices = std::move(indices)};
}

}  // namespace App::Omikron

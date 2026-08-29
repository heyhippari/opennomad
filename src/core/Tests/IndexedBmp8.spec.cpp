#include "Core/Omikron/IndexedBmp8.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "OmikronTestBuffer.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

namespace {

constexpr std::uint32_t K_PIXEL_OFFSET{14U + 40U + (256U * 4U)};

/// Builds an uncompressed 8-bit indexed BMP with a full 256-entry palette.
/// Rows are given in file order: positive height = bottom-up, negative =
/// top-down. The palette entries are all zero (the decoder skips them).
Buffer make_indexed_bmp(const std::int32_t width,
    const std::int32_t height,
    const std::vector<std::vector<std::uint8_t>>& rows) {
  const std::size_t row_bytes{static_cast<std::size_t>(width)};
  const std::size_t row_stride{((row_bytes + 3U) / 4U) * 4U};
  const std::size_t absolute_height{
      height < 0 ? static_cast<std::size_t>(-height) : static_cast<std::size_t>(height)};
  const std::uint32_t pixel_data_size{static_cast<std::uint32_t>(row_stride * absolute_height)};

  Buffer file;
  file.chars("BM", 2)
      .u32(K_PIXEL_OFFSET + pixel_data_size)
      .u16(0)
      .u16(0)
      .u32(K_PIXEL_OFFSET)
      .u32(40)  // BITMAPINFOHEADER size.
      .i32(width)
      .i32(height)  // Positive = bottom-up, negative = top-down.
      .u16(1)       // Planes.
      .u16(8)       // Bits per pixel.
      .u32(0)       // BI_RGB.
      .u32(pixel_data_size)
      .i32(0)    // x pixels per metre.
      .i32(0)    // y pixels per metre.
      .u32(256)  // Colours used.
      .u32(0);   // Colours important.
  for (std::size_t entry{0}; entry < 256U; ++entry) {
    file.u8(0).u8(0).u8(0).u8(0);
  }
  for (const auto& row : rows) {
    for (const std::uint8_t index : row) {
      file.u8(index);
    }
    file.zeros(row_stride - row_bytes);
  }
  return file;
}

}  // namespace

TEST_SUITE("Core::Omikron::IndexedBmp8") {
  TEST_CASE("Decodes a bottom-up 8-bit indexed bitmap") {
    // File order: bottom row first.
    const std::vector<std::vector<std::uint8_t>> rows{{0, 1}, {2, 3}};
    const auto image{App::Omikron::IndexedBmp8Decoder::load(make_indexed_bmp(2, 2, rows).data())};
    REQUIRE(image.has_value());

    CHECK_EQ(image->width, 2);
    CHECK_EQ(image->height, 2);
    // Row 0 = top, so the top file row comes first.
    const std::vector<std::uint8_t> expected{2, 3, 0, 1};
    CHECK_EQ(image->indices, expected);
  }

  TEST_CASE("Decodes a top-down 8-bit indexed bitmap") {
    // Negative height: file order is already top-down.
    const std::vector<std::vector<std::uint8_t>> rows{{2, 3}, {0, 1}};
    const auto image{App::Omikron::IndexedBmp8Decoder::load(make_indexed_bmp(2, -2, rows).data())};
    REQUIRE(image.has_value());

    CHECK_EQ(image->width, 2);
    CHECK_EQ(image->height, -2);
    const std::vector<std::uint8_t> expected{2, 3, 0, 1};
    CHECK_EQ(image->indices, expected);
  }

  TEST_CASE("Handles row padding and preserves raw indices") {
    // Width 3 => stride 4 (one padding byte per row), spanning the full index
    // range so palette resolution would be observable if it happened.
    const std::vector<std::vector<std::uint8_t>> rows{{0, 127, 255}, {200, 100, 1}};
    const auto image{App::Omikron::IndexedBmp8Decoder::load(make_indexed_bmp(3, 2, rows).data())};
    REQUIRE(image.has_value());

    const std::vector<std::uint8_t> expected{200, 100, 1, 0, 127, 255};
    CHECK_EQ(image->indices, expected);
  }

  TEST_CASE("Rejects a truncated header") {
    Buffer file;
    file.chars("BM", 2).zeros(20);
    const auto image{App::Omikron::IndexedBmp8Decoder::load(file.data())};
    CHECK_FALSE(image.has_value());
  }

  TEST_CASE("Rejects data that is not a BMP") {
    Buffer file;
    file.chars("XX", 2).zeros(70);
    const auto image{App::Omikron::IndexedBmp8Decoder::load(file.data())};
    CHECK_FALSE(image.has_value());
  }

  TEST_CASE("Rejects an unsupported bit depth") {
    // Build a valid 24-bit BMP header and pixel payload by hand; the decoder
    // must reject it as not 8-bit rather than misreading indices.
    Buffer file;
    file.chars("BM", 2)
        .u32(54U + 4U)  // file size.
        .u16(0)
        .u16(0)
        .u32(54U)  // pixel offset (no palette).
        .u32(40U)
        .i32(2)
        .i32(2)
        .u16(1)
        .u16(24)  // bits per pixel.
        .u32(0)   // BI_RGB.
        .u32(4U)
        .i32(0)
        .i32(0)
        .u32(0)
        .u32(0);
    file.zeros(4);
    const auto image{App::Omikron::IndexedBmp8Decoder::load(file.data())};
    REQUIRE_FALSE(image.has_value());
  }

  TEST_CASE("Rejects unsupported compression") {
    // Same 8-bit fixture but with BI_RLE8 compression.
    Buffer file;
    file.chars("BM", 2)
        .u32(K_PIXEL_OFFSET + 4U)
        .u16(0)
        .u16(0)
        .u32(K_PIXEL_OFFSET)
        .u32(40U)
        .i32(2)
        .i32(2)
        .u16(1)
        .u16(8)
        .u32(1)  // BI_RLE8.
        .u32(4U)
        .i32(0)
        .i32(0)
        .u32(256)
        .u32(0);
    for (std::size_t entry{0}; entry < 256U; ++entry) {
      file.u8(0).u8(0).u8(0).u8(0);
    }
    file.zeros(4);
    const auto image{App::Omikron::IndexedBmp8Decoder::load(file.data())};
    REQUIRE_FALSE(image.has_value());
  }

  TEST_CASE("Rejects an out-of-range pixel data offset") {
    Buffer file;
    file.chars("BM", 2)
        .u32(K_PIXEL_OFFSET)
        .u16(0)
        .u16(0)
        .u32(K_PIXEL_OFFSET + 1000U)  // Past the end of the buffer.
        .u32(40U)
        .i32(2)
        .i32(2)
        .u16(1)
        .u16(8)
        .u32(0)
        .u32(4U)
        .i32(0)
        .i32(0)
        .u32(256)
        .u32(0);
    for (std::size_t entry{0}; entry < 256U; ++entry) {
      file.u8(0).u8(0).u8(0).u8(0);
    }
    const auto image{App::Omikron::IndexedBmp8Decoder::load(file.data())};
    REQUIRE_FALSE(image.has_value());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Core/Omikron/BmpImage.hpp"
#include "OmikronTestBuffer.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

namespace {

/// BGR byte order as stored in a 24-bit BMP.
constexpr std::array<std::uint8_t, 3> K_RED{0, 0, 255};
constexpr std::array<std::uint8_t, 3> K_GREEN{0, 255, 0};
constexpr std::array<std::uint8_t, 3> K_BLUE{255, 0, 0};
constexpr std::array<std::uint8_t, 3> K_WHITE{255, 255, 255};

/// Builds an uncompressed 24-bit BMP. Rows are given in file order: with a
/// positive height the first row is the bottom row of the image (bottom-up
/// storage), with a negative height it is the top row (top-down storage).
Buffer make_bmp(const std::int32_t width,
                const std::int32_t height,
                const std::vector<std::vector<std::array<std::uint8_t, 3>>>& rows) {
  const std::size_t row_bytes{static_cast<std::size_t>(width) * 3U};
  const std::size_t row_stride{((row_bytes + 3U) / 4U) * 4U};
  const std::size_t absolute_height{
      height < 0 ? static_cast<std::size_t>(-height) : static_cast<std::size_t>(height)};
  const std::uint32_t pixel_data_size{
      static_cast<std::uint32_t>(row_stride * absolute_height)};

  Buffer file;
  file.chars("BM", 2)
      .u32(14U + 40U + pixel_data_size)
      .u16(0)
      .u16(0)
      .u32(54)  // Pixel data offset.
      .u32(40)  // BITMAPINFOHEADER size.
      .i32(width)
      .i32(height)  // Positive = bottom-up, negative = top-down.
      .u16(1)       // Planes.
      .u16(24)      // Bits per pixel.
      .u32(0)       // BI_RGB.
      .u32(pixel_data_size)
      .i32(0)  // x pixels per metre.
      .i32(0)  // y pixels per metre.
      .u32(0)  // Colours used.
      .u32(0); // Colours important.
  for (const auto& row : rows) {
    for (const auto& pixel : row) {
      file.u8(pixel.at(0)).u8(pixel.at(1)).u8(pixel.at(2));
    }
    file.zeros(row_stride - row_bytes);
  }
  return file;
}

/// Expected RGBA8 of the shared 2x2 test image: bottom row red/green, top
/// row blue/white, stored bottom-up to match glTexImage2D's row order.
const std::vector<std::uint8_t>& expected_rgba8() {
  static const std::vector<std::uint8_t> k_expected{255, 0, 0, 255,      // Bottom-left red.
                                                    0, 255, 0, 255,      // Bottom-right green.
                                                    0, 0, 255, 255,      // Top-left blue.
                                                    255, 255, 255, 255}; // Top-right white.
  return k_expected;
}

}  // namespace

TEST_SUITE("Core::Omikron::BmpImage") {
  TEST_CASE("Decodes a bottom-up 24-bit bitmap") {
    const std::vector<std::vector<std::array<std::uint8_t, 3>>> rows{
        {K_RED, K_GREEN}, {K_BLUE, K_WHITE}};
    const auto image{App::Omikron::BmpImageDecoder::load(make_bmp(2, 2, rows).data())};
    REQUIRE(image.has_value());

    CHECK_EQ(image->width, 2);
    CHECK_EQ(image->height, 2);
    CHECK_EQ(image->rgba8, expected_rgba8());
  }

  TEST_CASE("Decodes a top-down 24-bit bitmap") {
    // Negative DIB height: the first file row is the top row of the image.
    const std::vector<std::vector<std::array<std::uint8_t, 3>>> rows{
        {K_BLUE, K_WHITE}, {K_RED, K_GREEN}};
    const auto image{App::Omikron::BmpImageDecoder::load(make_bmp(2, -2, rows).data())};
    REQUIRE(image.has_value());

    CHECK_EQ(image->width, 2);
    CHECK_EQ(image->height, 2);
    CHECK_EQ(image->rgba8, expected_rgba8());
  }

  TEST_CASE("Rejects a truncated pixel payload") {
    const std::vector<std::vector<std::array<std::uint8_t, 3>>> rows{
        {K_RED, K_GREEN}, {K_BLUE, K_WHITE}};
    const Buffer file{make_bmp(2, 2, rows)};
    // Drop the last row (pixels and padding) so SDL's read fails.
    const std::vector<std::byte> truncated{file.data().begin(), file.data().end() - 4};

    const auto image{App::Omikron::BmpImageDecoder::load(truncated)};
    CHECK_FALSE(image.has_value());
  }

  TEST_CASE("Rejects data that is not a BMP") {
    Buffer file;
    file.chars("XX", 2).zeros(70);

    const auto image{App::Omikron::BmpImageDecoder::load(file.data())};
    CHECK_FALSE(image.has_value());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

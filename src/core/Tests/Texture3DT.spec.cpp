#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/Texture3DT.hpp"
#include "OmikronTestBuffer.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

namespace {

App::Omikron::Material make_material(const std::uint32_t data_size,
                                     const std::uint16_t bits_per_pixel,
                                     const std::uint16_t width,
                                     const std::uint16_t height) {
  App::Omikron::Material material;
  material.data_size = data_size;
  material.bits_per_pixel = bits_per_pixel;
  material.width = width;
  material.height = height;
  return material;
}

}  // namespace

TEST_SUITE("Core::Omikron::Texture3DT") {
  TEST_CASE("Decodes a palette, pure black is transparent") {
    Buffer file;
    file.u8(0).u8(0).u8(0)  // Palette: black.
        .u8(0);             // Index 0.

    const std::vector<App::Omikron::Material> materials{make_material(1, 0, 1, 1)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    REQUIRE(images.has_value());
    REQUIRE_EQ(images->size(), std::size_t{1});

    CHECK_EQ(images->at(0).width, 1U);
    CHECK_EQ(images->at(0).height, 1U);
    CHECK_EQ(images->at(0).rgba8, std::vector<std::uint8_t>{0, 0, 0, 0});
  }

  TEST_CASE("Keeps source row order and maps indices through the palette") {
    Buffer file;
    // Palette: four entries (10), (20), (30), (40).
    for (const std::uint8_t channel : std::array<std::uint8_t, 4>{10, 20, 30, 40}) {
      file.u8(channel).u8(channel).u8(channel);
    }
    // Literal 0, then three literals 1, 2, 3.
    file.u8(0).u8(0x00).u8(1).u8(2).u8(3);

    const std::vector<App::Omikron::Material> materials{make_material(5, 2, 2, 2)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    REQUIRE(images.has_value());

    const auto& rgba8{images->at(0).rgba8};
    REQUIRE_EQ(rgba8.size(), std::size_t{16});
    // The game stores the first row at the bottom, so rows are kept in
    // source order: [0, 1] / [2, 3].
    CHECK_EQ(std::array<std::uint8_t, 4>{rgba8.at(0), rgba8.at(1), rgba8.at(2), rgba8.at(3)},
             std::array<std::uint8_t, 4>{10, 10, 10, 255});
    CHECK_EQ(std::array<std::uint8_t, 4>{rgba8.at(4), rgba8.at(5), rgba8.at(6), rgba8.at(7)},
             std::array<std::uint8_t, 4>{20, 20, 20, 255});
    CHECK_EQ(std::array<std::uint8_t, 4>{rgba8.at(8), rgba8.at(9), rgba8.at(10), rgba8.at(11)},
             std::array<std::uint8_t, 4>{30, 30, 30, 255});
    CHECK_EQ(std::array<std::uint8_t, 4>{rgba8.at(12), rgba8.at(13), rgba8.at(14), rgba8.at(15)},
             std::array<std::uint8_t, 4>{40, 40, 40, 255});
  }

  TEST_CASE("Decompresses all four LZ sequence types") {
    Buffer file;
    // Palette: entry 0 transparent black, entry 1 (10, 20, 30).
    file.u8(0).u8(0).u8(0).u8(10).u8(20).u8(30);
    // Literal 1, then one flag byte (0xF0) holding four sequences:
    //  bit 0: type 0, repeat previous x3
    //  bit 1: type 1, copy offset 2 x3
    //  bit 2: type 2, copy offset 2 x3
    //  bit 3: type 3, offset 256 -> zeros x3
    // Bits 4-7 are literals: four trailing index-0 bytes.
    file.u8(1)
        .u8(0xF0).u8(4)
        .u8(1).u8(1)
        .u8(2).u8(0).u8(1)
        .u8(3).u8(1)
        .u8(0).u8(0).u8(0).u8(0);

    const std::vector<App::Omikron::Material> materials{make_material(14, 1, 17, 1)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    REQUIRE(images.has_value());

    const auto& rgba8{images->at(0).rgba8};
    REQUIRE_EQ(rgba8.size(), std::size_t{17U} * 4U);
    // Pixels 0-9 map to palette entry 1, pixels 10-16 come from the type 3
    // sequence and the trailing index-0 literals.
    const std::array<std::uint8_t, 4> lit{10, 20, 30, 255};
    const std::array<std::uint8_t, 4> clear{0, 0, 0, 0};
    for (std::size_t pixel{0}; pixel < 10; ++pixel) {
      const std::size_t offset{pixel * 4U};
      CHECK_EQ(std::array<std::uint8_t, 4>{rgba8.at(offset),
                                           rgba8.at(offset + 1U),
                                           rgba8.at(offset + 2U),
                                           rgba8.at(offset + 3U)},
               lit);
    }
    for (std::size_t pixel{10}; pixel < 17; ++pixel) {
      const std::size_t offset{pixel * 4U};
      CHECK_EQ(std::array<std::uint8_t, 4>{rgba8.at(offset),
                                           rgba8.at(offset + 1U),
                                           rgba8.at(offset + 2U),
                                           rgba8.at(offset + 3U)},
               clear);
    }
  }

  TEST_CASE("Rejects palette indices beyond the palette") {
    Buffer file;
    // Palette: entry 0 transparent black, entry 1 (10, 20, 30).
    file.u8(0).u8(0).u8(0).u8(10).u8(20).u8(30);
    // Literal index 2, then a type-0 sequence covering the last two pixels.
    file.u8(2).u8(0x80).u8(4);

    const std::vector<App::Omikron::Material> materials{make_material(3, 1, 3, 1)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    CHECK_FALSE(images.has_value());
  }

  TEST_CASE("A 65536-byte payload is stored uncompressed") {
    Buffer file;
    // 256 palette entries (256 * 3 bytes).
    for (std::uint32_t entry{0}; entry < 256U; ++entry) {
      const std::uint8_t channel{static_cast<std::uint8_t>(entry)};
      file.u8(channel).u8(channel).u8(channel);
    }
    // Raw index payload.
    for (std::uint32_t pixel{0}; pixel < 65536U; ++pixel) {
      file.u8(static_cast<std::uint8_t>(pixel & 0xFFU));
    }

    const std::vector<App::Omikron::Material> materials{make_material(65536, 8, 256, 256)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    REQUIRE(images.has_value());
    REQUIRE_EQ(images->size(), std::size_t{1});

    const auto& image{images->at(0)};
    CHECK_EQ(image.width, 256U);
    CHECK_EQ(image.height, 256U);
    // Rows stay in source order (the first stored row is the bottom row).
    // Index 0 is transparent black, index 1 is the grey (1, 1, 1).
    const std::size_t bottom_row{static_cast<std::size_t>(255U) * 256U * 4U};
    CHECK_EQ(image.rgba8.at(bottom_row), std::uint8_t{0});
    CHECK_EQ(image.rgba8.at(bottom_row + 3U), std::uint8_t{0});
    CHECK_EQ(image.rgba8.at(bottom_row + 4U), std::uint8_t{1});
    CHECK_EQ(image.rgba8.at(bottom_row + 7U), std::uint8_t{255});
  }

  TEST_CASE("Multiple materials decode in file order") {
    Buffer file;
    file.u8(1).u8(2).u8(3).u8(0);  // Material 0.
    file.u8(4).u8(5).u8(6).u8(0);  // Material 1.

    const std::vector<App::Omikron::Material> materials{
        make_material(1, 0, 1, 1), make_material(1, 0, 1, 1)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    REQUIRE(images.has_value());
    REQUIRE_EQ(images->size(), std::size_t{2});

    CHECK_EQ(images->at(0).rgba8, std::vector<std::uint8_t>{1, 2, 3, 255});
    CHECK_EQ(images->at(1).rgba8, std::vector<std::uint8_t>{4, 5, 6, 255});
  }

  TEST_CASE("Rejects unsupported palette depths") {
    const std::vector<App::Omikron::Material> materials{make_material(0, 9, 1, 1)};
    const auto images{App::Omikron::Texture3DT::load(Buffer{}.data(), materials)};
    CHECK_FALSE(images.has_value());
  }

  TEST_CASE("Rejects truncated palettes") {
    Buffer file;
    file.u8(1).u8(2);  // A 2-colour palette needs 6 bytes.

    const std::vector<App::Omikron::Material> materials{make_material(1, 1, 1, 1)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    CHECK_FALSE(images.has_value());
  }

  TEST_CASE("A payload the size of width times height is stored uncompressed") {
    Buffer file;
    // 256 palette entries (256 * 3 bytes).
    for (std::uint32_t entry{0}; entry < 256U; ++entry) {
      const std::uint8_t channel{static_cast<std::uint8_t>(entry)};
      file.u8(channel).u8(channel).u8(channel);
    }
    // Raw 64x64 index payload.
    for (std::uint32_t pixel{0}; pixel < 64U * 64U; ++pixel) {
      file.u8(static_cast<std::uint8_t>(pixel & 0xFFU));
    }

    const std::vector<App::Omikron::Material> materials{make_material(64U * 64U, 8, 64, 64)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    REQUIRE(images.has_value());
    REQUIRE_EQ(images->size(), std::size_t{1});

    const auto& image{images->at(0)};
    CHECK_EQ(image.width, 64U);
    CHECK_EQ(image.height, 64U);
    // The raw payload is copied verbatim: index 0 is transparent black,
    // index 1 is the grey (1, 1, 1).
    CHECK_EQ(image.rgba8.at(0), std::uint8_t{0});
    CHECK_EQ(image.rgba8.at(3), std::uint8_t{0});
    CHECK_EQ(image.rgba8.at(4), std::uint8_t{1});
    CHECK_EQ(image.rgba8.at(7), std::uint8_t{255});
  }

  TEST_CASE("Rejects a raw payload shorter than width times height") {
    Buffer file;
    // 1-bit palette (2 entries).
    file.u8(0).u8(0).u8(0).u8(10).u8(20).u8(30);
    // A 2x2 raw payload is promised, but only two bytes are present.
    file.u8(0).u8(1);

    const std::vector<App::Omikron::Material> materials{make_material(4, 1, 2, 2)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    CHECK_FALSE(images.has_value());
  }

  TEST_CASE("Decompresses a fully covered 256x256 texture") {
    Buffer file;
    // 256 palette entries; index 1 is the grey (1, 1, 1).
    for (std::uint32_t entry{0}; entry < 256U; ++entry) {
      const std::uint8_t channel{static_cast<std::uint8_t>(entry)};
      file.u8(channel).u8(channel).u8(channel);
    }
    // Literal index 1, then type-0 runs (repeat previous byte, 65 pixels
    // each) until all 65536 pixels are produced: 126 full flag bytes (one
    // flag byte + eight description bytes each) and a final flag whose
    // first bit completes the image (its remaining bits are never read).
    file.u8(1);
    for (std::uint32_t run{0}; run < 126U; ++run) {
      file.u8(0xFF)
          .u8(0xFC).u8(0xFC).u8(0xFC).u8(0xFC).u8(0xFC).u8(0xFC).u8(0xFC).u8(0xFC);
    }
    file.u8(0x80).u8(0xFC);

    const std::vector<App::Omikron::Material> materials{
        make_material(1U + (126U * 9U) + 2U, 8, 256, 256)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    REQUIRE(images.has_value());

    const auto& image{images->at(0)};
    REQUIRE_EQ(image.rgba8.size(), std::size_t{256U} * 256U * 4U);
    // Sampled pixels are grey index 1 everywhere.
    for (const std::size_t pixel : std::array<std::size_t, 4>{0U, 12345U, 32768U, 65535U}) {
      const std::size_t offset{pixel * 4U};
      CHECK_EQ(image.rgba8.at(offset), std::uint8_t{1});
      CHECK_EQ(image.rgba8.at(offset + 3U), std::uint8_t{255});
    }
  }

  TEST_CASE("Decompresses a compressed texture smaller than 256x256") {
    Buffer file;
    // 1-bit palette: entry 0 transparent black, entry 1 (10, 20, 30).
    file.u8(0).u8(0).u8(0).u8(10).u8(20).u8(30);
    // Literal index 1, then two full type-0 run flags (eight sequences each)
    // covering 32x32.
    file.u8(1);
    for (std::uint32_t run{0}; run < 2U; ++run) {
      file.u8(0xFF)
          .u8(0xFC).u8(0xFC).u8(0xFC).u8(0xFC).u8(0xFC).u8(0xFC).u8(0xFC).u8(0xFC);
    }

    const std::vector<App::Omikron::Material> materials{make_material(19, 1, 32, 32)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    REQUIRE(images.has_value());

    const auto& image{images->at(0)};
    REQUIRE_EQ(image.rgba8.size(), std::size_t{32U} * 32U * 4U);
    const std::size_t last_pixel{((std::size_t{32U} * 32U) - 1U) * 4U};
    CHECK_EQ(image.rgba8.at(0), std::uint8_t{10});
    CHECK_EQ(image.rgba8.at(3), std::uint8_t{255});
    CHECK_EQ(image.rgba8.at(last_pixel), std::uint8_t{10});
    CHECK_EQ(image.rgba8.at(last_pixel + 3U), std::uint8_t{255});
  }

  TEST_CASE("Decodes a 4-bit palette") {
    Buffer file;
    // 16 palette entries (48 bytes); entry E is (3E, 3E, 3E).
    for (std::uint32_t entry{0}; entry < 16U; ++entry) {
      const std::uint8_t channel{static_cast<std::uint8_t>(entry * 3U)};
      file.u8(channel).u8(channel).u8(channel);
    }
    // 2x2 raw payload (data_size == width * height): indices 15, 0, 7, 15.
    file.u8(15).u8(0).u8(7).u8(15);

    const std::vector<App::Omikron::Material> materials{make_material(4, 4, 2, 2)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    REQUIRE(images.has_value());

    const auto& image{images->at(0)};
    REQUIRE_EQ(image.rgba8.size(), std::size_t{16});
    // Entry 15 is (45, 45, 45); entry 7 is (21, 21, 21); entry 0 is
    // transparent black.
    CHECK_EQ(image.rgba8.at(0), std::uint8_t{45});
    CHECK_EQ(image.rgba8.at(3), std::uint8_t{255});
    CHECK_EQ(image.rgba8.at(4), std::uint8_t{0});
    CHECK_EQ(image.rgba8.at(7), std::uint8_t{0});
    CHECK_EQ(image.rgba8.at(8), std::uint8_t{21});
    CHECK_EQ(image.rgba8.at(11), std::uint8_t{255});
    CHECK_EQ(image.rgba8.at(12), std::uint8_t{45});
    CHECK_EQ(image.rgba8.at(15), std::uint8_t{255});
  }

  TEST_CASE("Pads a compressed payload that ends early with transparent pixels") {
    Buffer file;
    // 256 palette entries; index 1 is the grey (1, 1, 1).
    for (std::uint32_t entry{0}; entry < 256U; ++entry) {
      const std::uint8_t channel{static_cast<std::uint8_t>(entry)};
      file.u8(channel).u8(channel).u8(channel);
    }
    // Literal index 1, then one flag byte with eight literal index-0 pixels;
    // the input ends cleanly after 9 of 256x256 pixels.
    file.u8(1).u8(0x00)
        .u8(0).u8(0).u8(0).u8(0).u8(0).u8(0).u8(0).u8(0);

    const std::vector<App::Omikron::Material> materials{make_material(10, 8, 256, 256)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    REQUIRE(images.has_value());

    const auto& image{images->at(0)};
    REQUIRE_EQ(image.rgba8.size(), std::size_t{256U} * 256U * 4U);
    CHECK_EQ(image.rgba8.at(0), std::uint8_t{1});
    CHECK_EQ(image.rgba8.at(3), std::uint8_t{255});
    CHECK_EQ(image.rgba8.at(4), std::uint8_t{0});
    CHECK_EQ(image.rgba8.at(7), std::uint8_t{0});
    // The padded tail maps to palette index 0 (transparent black).
    const std::size_t last_pixel{((std::size_t{256U} * 256U) - 1U) * 4U};
    CHECK_EQ(image.rgba8.at(last_pixel), std::uint8_t{0});
    CHECK_EQ(image.rgba8.at(last_pixel + 3U), std::uint8_t{0});
  }

  TEST_CASE("Rejects a compressed payload truncated inside a sequence") {
    Buffer file;
    // 1-bit palette.
    file.u8(0).u8(0).u8(0).u8(10).u8(20).u8(30);
    // Literal 0, then a flag byte whose second bit begins a type-1 sequence
    // whose offset byte is missing.
    file.u8(0).u8(0x40).u8(4);

    const std::vector<App::Omikron::Material> materials{make_material(3, 1, 16, 1)};
    const auto images{App::Omikron::Texture3DT::load(file.data(), materials)};
    CHECK_FALSE(images.has_value());
  }

  TEST_CASE("Computes the encoded size of 4-bit and 8-bit materials") {
    const std::vector<App::Omikron::Material> materials{
        make_material(16510, 8, 256, 256), make_material(10, 4, 16, 16)};
    const auto size{App::Omikron::Texture3DT::encoded_size(materials)};
    REQUIRE(size.has_value());
    // 8-bit palette (768 bytes) + payload, then 4-bit palette (48 bytes)
    // + payload.
    CHECK_EQ(*size, std::size_t{768U + 16510U + 48U + 10U});
  }

  TEST_CASE("Rejects unsupported bit depths in encoded size") {
    const std::vector<App::Omikron::Material> materials{make_material(1, 9, 1, 1)};
    const auto size{App::Omikron::Texture3DT::encoded_size(materials)};
    REQUIRE_FALSE(size.has_value());
    CHECK(size.error().find("bits per pixel") != std::string::npos);
  }

  TEST_CASE("Accumulates large data sizes without overflow") {
    const std::vector<App::Omikron::Material> materials{
        make_material(0xFFFFFFF0U, 8, 256, 256), make_material(0xFFFFFFF0U, 8, 256, 256)};
    const auto size{App::Omikron::Texture3DT::encoded_size(materials)};
    REQUIRE(size.has_value());
    const std::uint64_t expected{
        2U * (static_cast<std::uint64_t>(0xFFFFFFF0U) + static_cast<std::uint64_t>(768U))};
    CHECK_EQ(*size, static_cast<std::size_t>(expected));
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

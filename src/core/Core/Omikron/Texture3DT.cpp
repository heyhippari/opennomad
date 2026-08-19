#include "Core/Omikron/Texture3DT.hpp"

#include <fmt/format.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Omikron/BinaryReader.hpp"
#include "Core/Omikron/Model3DO.hpp"

namespace App::Omikron {

namespace {

/// Pixel capacity of one original-runtime texture page (256x256).
constexpr std::size_t K_TEXTURE_PAGE_SIZE{65536};
constexpr std::uint32_t K_MAX_BITS_PER_PIXEL{8};
constexpr std::size_t K_PALETTE_ENTRY_SIZE{3};

}  // namespace

std::expected<std::vector<Texture3DTImage>, std::string> Texture3DT::load(
    const std::span<const std::byte> data, const std::vector<Material>& materials) {
  APP_PROFILE_FUNCTION();

  BinaryReader reader{data};
  std::vector<Texture3DTImage> images;
  images.reserve(materials.size());

  std::size_t offset{0};
  for (const Material& material : materials) {
    if (material.bits_per_pixel > K_MAX_BITS_PER_PIXEL) {
      return std::expected<std::vector<Texture3DTImage>, std::string>{std::unexpect,
          fmt::format("material '{}' uses unsupported {} bits per pixel",
              material.name,
              material.bits_per_pixel)};
    }

    const std::size_t colour_count{std::size_t{1} << material.bits_per_pixel};

    // Palette: colour_count entries of three bytes (R, G, B). Pure black is
    // the transparency key of the engine.
    reader.seek(offset);
    std::vector<std::array<std::uint8_t, 4>> palette(colour_count);
    for (std::array<std::uint8_t, 4>& colour : palette) {
      const std::uint8_t red{reader.read_u8()};
      const std::uint8_t green{reader.read_u8()};
      const std::uint8_t blue{reader.read_u8()};
      const std::uint8_t alpha{
          (red == 0U && green == 0U && blue == 0U) ? std::uint8_t{0} : std::uint8_t{255}};
      colour = {red, green, blue, alpha};
    }
    if (reader.has_error()) {
      return std::expected<std::vector<Texture3DTImage>, std::string>{std::unexpect,
          fmt::format("palette of material '{}': {}", material.name, reader.error())};
    }

    const std::size_t pixel_count{
        static_cast<std::size_t>(material.width) * static_cast<std::size_t>(material.height)};
    auto indices{decompress(reader, material.data_size, pixel_count)};
    if (!indices) {
      return std::expected<std::vector<Texture3DTImage>, std::string>{std::unexpect,
          fmt::format("texture of material '{}': {}", material.name, indices.error())};
    }

    // Resolve indices through the palette. UV coordinates use the same
    // unflipped convention as the source data.
    Texture3DTImage image{.width = material.width, .height = material.height, .rgba8 = {}};
    image.rgba8.resize(pixel_count * 4U, 0);
    for (std::size_t pixel{0}; pixel < pixel_count; ++pixel) {
      const std::uint8_t palette_index{indices->at(pixel)};
      if (static_cast<std::size_t>(palette_index) >= palette.size()) {
        return std::expected<std::vector<Texture3DTImage>, std::string>{std::unexpect,
            fmt::format("texture of material '{}' uses palette index {} with a {}-entry palette",
                material.name,
                palette_index,
                palette.size())};
      }
      const std::array<std::uint8_t, 4>& colour{palette.at(palette_index)};
      image.rgba8.at(pixel * 4U) = colour.at(0);
      image.rgba8.at((pixel * 4U) + 1U) = colour.at(1);
      image.rgba8.at((pixel * 4U) + 2U) = colour.at(2);
      image.rgba8.at((pixel * 4U) + 3U) = colour.at(3);
    }
    images.push_back(std::move(image));

    offset += material.data_size + (colour_count * K_PALETTE_ENTRY_SIZE);
  }

  // A lying data_size misaligns every later material; catch a final material
  // whose sections overrun the file.
  if (offset > data.size()) {
    return std::expected<std::vector<Texture3DTImage>, std::string>{std::unexpect,
        fmt::format(
            "texture data ends at byte {} beyond the {} byte .3DT file", offset, data.size())};
  }
  return images;
}

std::expected<std::size_t, std::string> Texture3DT::encoded_size(
    const std::vector<Material>& materials) {
  std::uint64_t total{0};
  for (const Material& material : materials) {
    if (material.bits_per_pixel > K_MAX_BITS_PER_PIXEL) {
      return std::expected<std::size_t, std::string>{std::unexpect,
          fmt::format("material '{}' uses unsupported {} bits per pixel",
              material.name,
              material.bits_per_pixel)};
    }

    const std::uint64_t palette_size{static_cast<std::uint64_t>(K_PALETTE_ENTRY_SIZE) *
                                     (std::uint64_t{1} << material.bits_per_pixel)};
    const std::uint64_t addition{palette_size + material.data_size};
    if (addition > (std::numeric_limits<std::uint64_t>::max() - total)) {
      return std::expected<std::size_t, std::string>{std::unexpect,
          fmt::format("auxiliary size overflow at material '{}'", material.name)};
    }
    total += addition;
  }
  return static_cast<std::size_t>(total);
}

std::expected<std::vector<std::uint8_t>, std::string> Texture3DT::decompress(
    BinaryReader& reader, const std::size_t compressed_size, const std::size_t uncompressed_size) {
  if (uncompressed_size == 0U) {
    return std::vector<std::uint8_t>{};
  }

  const std::span<const std::byte> payload{reader.read_bytes(compressed_size)};
  if (reader.has_error()) {
    return std::expected<std::vector<std::uint8_t>, std::string>{std::unexpect, reader.error()};
  }
  BinaryReader input{payload};

  // The payload is stored raw exactly when its size equals width * height
  // (the original loader's raw detection: dataSize == expectedSize). For
  // 256x256 8-bit textures this coincides with 65536, but smaller textures
  // are stored raw as well.
  if (compressed_size == uncompressed_size) {
    std::vector<std::uint8_t> result;
    result.reserve(payload.size());
    for (const std::byte byte : payload) {
      result.push_back(std::to_integer<std::uint8_t>(byte));
    }
    return result;
  }

  std::vector<std::uint8_t> result;
  result.reserve(uncompressed_size);

  // The first output byte is always stored literally.
  result.push_back(input.read_u8());
  std::size_t current{1U};

  while (current < uncompressed_size && input.tell() < payload.size() && !input.has_error()) {
    const std::uint8_t flag_byte{input.read_u8()};
    for (std::size_t bit{0}; bit < 8U; ++bit) {
      if (current >= uncompressed_size) {
        break;
      }
      const bool is_sequence{((flag_byte >> (7U - bit)) & 1U) != 0U};
      if (is_sequence) {
        const std::uint8_t description{input.read_u8()};
        const std::size_t sequence_type{description & 0x3U};
        std::size_t sequence_size{static_cast<std::size_t>(description >> 2U) + 3U};
        std::size_t offset{0U};
        switch (sequence_type) {
          case 0U:  // Repeat the previous byte.
            offset = 1U;
            --sequence_size;
            break;
          case 1U:  // Copy from 1..256 bytes back.
            offset = 1U + input.read_u8();
            break;
          case 2U:  // Copy from 1..65536 bytes back (16-bit big-endian offset).
            offset = 1U + (static_cast<std::size_t>(input.read_u8()) << 8U) + input.read_u8();
            break;
          case 3U: {  // Copy from a multiple of 256 bytes back; zero encodes 65536.
            const std::uint8_t encoded_offset{input.read_u8()};
            offset = encoded_offset == 0U ? K_TEXTURE_PAGE_SIZE
                                          : static_cast<std::size_t>(encoded_offset) * 256U;
            break;
          }
          default:
            return std::expected<std::vector<std::uint8_t>, std::string>{
                std::unexpect, fmt::format("invalid texture LZ sequence type {}", sequence_type)};
        }
        for (std::size_t index{0}; index < sequence_size; ++index) {
          if (current >= uncompressed_size) {
            break;
          }
          result.push_back(offset > current ? std::uint8_t{0} : result.at(current - offset));
          ++current;
        }
      } else {
        result.push_back(input.read_u8());
        ++current;
      }
    }
  }

  if (input.has_error()) {
    return std::expected<std::vector<std::uint8_t>, std::string>{std::unexpect,
        fmt::format("compressed payload ended after producing {} of {} pixels",
            result.size(),
            uncompressed_size)};
  }
  // A compressed payload may end before every pixel is represented; the
  // omitted tail is implicitly palette index zero. This padding is inherited
  // from the reference Blender importer: the original executable's upload
  // routine assumes a complete decoded image and shows no explicit
  // completion check or padding step, so whether early termination is an
  // intentional format rule is unconfirmed. Keep the padding so previously
  // working textures (e.g. KIEDE2-style streams) continue to decode.
  result.resize(uncompressed_size, std::uint8_t{0});
  return result;
}

}  // namespace App::Omikron

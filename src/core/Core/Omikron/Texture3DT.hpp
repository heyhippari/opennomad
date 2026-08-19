#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "Core/Omikron/Model3DO.hpp"

namespace App::Omikron {

/// One decoded texture from a .3DT file.
struct Texture3DTImage {
  std::uint16_t width{0};
  std::uint16_t height{0};
  /// width * height * 4 bytes RGBA8 in source row order (the game stores
  /// the first row at the bottom, matching glTexImage2D's expectation).
  std::vector<std::uint8_t> rgba8;
};

/// Decoder for the .3DT texture sidecar that accompanies every .3DO model.
class Texture3DT {
 public:
  /// Decodes the texture of every material, in material order. The first
  /// material's texture starts at the beginning of the file; each following
  /// one starts after the previous palette and payload.
  [[nodiscard]] static std::expected<std::vector<Texture3DTImage>, std::string> load(
      std::span<const std::byte> data, const std::vector<Material>& materials);

  /// Total encoded bytes the .3DT layout consumes for the given materials:
  /// for each material, a palette of 3 bytes per 2^bits_per_pixel entry
  /// followed by its data_size payload, back to back with no padding.
  /// Mirrors the section arithmetic of load() so callers can validate a
  /// declared stream size (e.g. an SCX auxiliary block) before decoding.
  [[nodiscard]] static std::expected<std::size_t, std::string> encoded_size(
      const std::vector<Material>& materials);

 private:
  /// LZ-style decompression used by the engine (reference importer port).
  /// A payload whose size equals the decoded pixel count (width * height) is
  /// returned raw.
  static std::expected<std::vector<std::uint8_t>, std::string> decompress(
      BinaryReader& reader, std::size_t compressed_size, std::size_t uncompressed_size);
};

}  // namespace App::Omikron

#pragma once

#include <glad/glad.h>

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace App {

/// RAII cube-map texture with a fixed 8-bit RGBA face format.
class TextureCube {
 public:
  /// Uploads six RGBA8 faces in the order +X, -X, +Y, -Y, +Z, -Z; each face
  /// holds size * size * 4 bytes.
  static std::expected<TextureCube, std::string> create(
      int size, const std::array<std::vector<std::uint8_t>, 6>& faces, bool srgb = true);

  TextureCube(TextureCube&& other) noexcept;
  TextureCube& operator=(TextureCube&& other) noexcept;
  ~TextureCube();

  TextureCube(const TextureCube&) = delete;
  TextureCube& operator=(const TextureCube&) = delete;

  /// Binds the cube map to the given texture image unit.
  void bind(std::uint32_t unit) const;
  static void unbind();

  [[nodiscard]] GLuint id() const;

 private:
  /// Assumes ownership of an already uploaded cube map.
  explicit TextureCube(GLuint id);

  GLuint m_id{0};
};

/// Generates six RGBA8 faces of a procedural sky: a bright zenith fading
/// through a grey horizon into a dark nadir. Pure function, unit-tested.
std::array<std::vector<std::uint8_t>, 6> generate_sky_cubemap(int size);

}  // namespace App

#pragma once

#include <glad/glad.h>

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace App {

/// RAII 2D texture with a fixed 8-bit RGBA upload format.
class Texture2D {
 public:
  /// Uploads RGBA8 pixel data (width * height * 4 bytes).
  ///
  /// When srgb is true the storage uses GL_SRGB8_ALPHA8 so sampling decodes
  /// sRGB-encoded values into linear space (matching GL_FRAMEBUFFER_SRGB).
  static std::expected<Texture2D, std::string> create(
      int width, int height, std::span<const std::uint8_t> rgba8, bool srgb = true);

  /// Allocates an uninitialised texture (render-target storage) with linear
  /// filtering and clamp-to-edge wrapping.
  static std::expected<Texture2D, std::string> create(int width, int height, bool srgb = true);

  Texture2D(Texture2D&& other) noexcept;
  Texture2D& operator=(Texture2D&& other) noexcept;
  ~Texture2D();

  Texture2D(const Texture2D&) = delete;
  Texture2D& operator=(const Texture2D&) = delete;

  /// Binds the texture to the given texture image unit (unit 0 == GL_TEXTURE0).
  void bind(std::uint32_t unit) const;
  static void unbind();

  /// Replaces the stored RGBA8 pixels in place (glTexSubImage2D). The input
  /// must provide at least width * height * 4 bytes. No-op when the texture
  /// has not been allocated.
  void update(std::span<const std::uint8_t> rgba8) const;

  [[nodiscard]] GLuint id() const;
  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;

  /// Generates RGBA8 checkerboard pixels with squares_per_side cells per edge.
  ///
  /// Colors are given in sRGB space in [0, 1]. Returns an error for
  /// non-positive dimensions.
  static std::expected<std::vector<std::uint8_t>, std::string> generate_checkerboard(
      int width,
      int height,
      int squares_per_side,
      std::array<float, 3> color_a,
      std::array<float, 3> color_b);

 private:
  /// Assumes ownership of an already uploaded texture.
  Texture2D(int width, int height, GLuint id);

  GLuint m_id{0};
  int m_width{0};
  int m_height{0};
};

}  // namespace App

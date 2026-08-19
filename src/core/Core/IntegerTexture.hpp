#pragma once

#include <glad/glad.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace App {

/// Integer texture internal format selector.
enum class IntegerFormat : std::uint8_t {
  /// Single-channel unsigned byte (GL_R8UI): the CLOUD.BMP height indices.
  k_r8ui,
  /// Two-channel signed byte (GL_RG8I): the X/Y height gradients.
  k_rg8i,
  /// Two-channel unsigned byte (GL_RG8UI): row/column warp endpoint pairs.
  k_rg8ui,
};

/// RAII integer 2D texture.
///
/// Integer textures preserve exact byte values through the pipeline (no
/// normalization, no sRGB decode, no filtering) and are colour-renderable
/// under the OpenGL 4.1 core requirement, which keeps the bump effect free of
/// compute shaders. Filtering is nearest-only (linear filtering is invalid for
/// integer formats).
class IntegerTexture {
 public:
  /// Default-constructs an empty (unallocated) texture.
  IntegerTexture() = default;

  /// Allocates uninitialised integer storage (render-target usage).
  static std::expected<IntegerTexture, std::string> create(
      int width, int height, IntegerFormat format);

  /// Allocates and uploads CPU pixel data in the texture's native layout.
  ///
  /// `data` holds the raw texel bytes in memory order:
  ///   k_r8ui  — width * height bytes (one uint8 per texel)
  ///   k_rg8i  — width * height * 2 bytes (two two's-complement int8 per texel)
  ///   k_rg8ui — width * height * 2 bytes (two uint8 per texel)
  /// For k_rg8i the caller stores the signed values as raw bytes; they are
  /// uploaded as GL_BYTE so the GPU sees the exact signed values.
  static std::expected<IntegerTexture, std::string> create_with_data(
      int width, int height, IntegerFormat format, std::span<const std::uint8_t> data);

  IntegerTexture(IntegerTexture&& other) noexcept;
  IntegerTexture& operator=(IntegerTexture&& other) noexcept;
  ~IntegerTexture();

  IntegerTexture(const IntegerTexture&) = delete;
  IntegerTexture& operator=(const IntegerTexture&) = delete;

  /// Binds the texture to the given image unit (unit 0 == GL_TEXTURE0).
  void bind(std::uint32_t unit) const;
  static void unbind();

  [[nodiscard]] GLuint id() const;
  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] IntegerFormat format() const;

 private:
  IntegerTexture(int width, int height, IntegerFormat format, GLuint id);

  GLuint m_id{0};
  int m_width{0};
  int m_height{0};
  IntegerFormat m_format{IntegerFormat::k_r8ui};
};

}  // namespace App

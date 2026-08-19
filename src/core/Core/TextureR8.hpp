#pragma once

#include <glad/glad.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace App {

/// RAII single-channel 8-bit texture (GL_R8). Used for the bump effect's
/// small dynamic source data: the 256x256 lit intensity field and the 1D row/
/// column warp lookup tables.
class TextureR8 {
 public:
  /// Allocates an uninitialised R8 texture with linear filtering and
  /// clamp-to-edge wrapping.
  static std::expected<TextureR8, std::string> create(int width, int height);

  TextureR8(TextureR8&& other) noexcept;
  TextureR8& operator=(TextureR8&& other) noexcept;
  ~TextureR8();

  TextureR8(const TextureR8&) = delete;
  TextureR8& operator=(const TextureR8&) = delete;

  void bind(std::uint32_t unit) const;
  static void unbind();

  /// Replaces the stored R8 pixels in place (glTexSubImage2D). The input must
  /// provide at least width * height bytes. No-op when unallocated.
  void update(std::span<const std::uint8_t> r8) const;

  [[nodiscard]] GLuint id() const;
  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;

 private:
  TextureR8(int width, int height, GLuint id);

  GLuint m_id{0};
  int m_width{0};
  int m_height{0};
};

}  // namespace App

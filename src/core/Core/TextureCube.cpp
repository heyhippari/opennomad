#include "TextureCube.hpp"

#include <glad/glad.h>

// NOLINTBEGIN(misc-include-cleaner)
// glm follows a "single-include" convention — the umbrella header is the
// canonical way to pull in the library, even though clang-tidy cannot trace
// individual symbols back to a direct sub-header.
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <glm/glm.hpp>
#include <string>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"

namespace App {

std::expected<TextureCube, std::string> TextureCube::create(
    const int size, const std::array<std::vector<std::uint8_t>, 6>& faces, const bool srgb) {
  APP_PROFILE_FUNCTION();

  if (size <= 0) {
    return std::expected<TextureCube, std::string>{
        std::unexpect, "TextureCube size must be positive"};
  }

  const std::size_t expected_bytes{
      static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4U};
  for (std::size_t face{0}; face < faces.size(); ++face) {
    if (faces.at(face).size() < expected_bytes) {
      return std::expected<TextureCube, std::string>{std::unexpect,
          fmt::format("TextureCube: face {} has {} bytes, expected {}",
              face,
              faces.at(face).size(),
              expected_bytes)};
    }
  }

  GLuint id{0};
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_CUBE_MAP, id);
  for (std::size_t face{0}; face < faces.size(); ++face) {
    glTexImage2D(static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face),
        0,
        srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
        size,
        size,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        faces.at(face).data());
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

  return TextureCube{id};
}

TextureCube::TextureCube(const GLuint id) : m_id(id) {}

TextureCube::TextureCube(TextureCube&& other) noexcept : m_id(std::exchange(other.m_id, 0)) {}

TextureCube& TextureCube::operator=(TextureCube&& other) noexcept {
  if (this != &other) {
    if (m_id != 0) {
      glDeleteTextures(1, &m_id);
    }
    m_id = std::exchange(other.m_id, 0);
  }
  return *this;
}

TextureCube::~TextureCube() {
  APP_PROFILE_FUNCTION();

  if (m_id != 0) {
    glDeleteTextures(1, &m_id);
  }
}

void TextureCube::bind(const std::uint32_t unit) const {
  APP_PROFILE_FUNCTION();

  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_CUBE_MAP, m_id);
}

void TextureCube::unbind() {
  glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

GLuint TextureCube::id() const {
  return m_id;
}

std::array<std::vector<std::uint8_t>, 6> generate_sky_cubemap(const int size) {
  std::array<std::vector<std::uint8_t>, 6> faces;
  for (std::vector<std::uint8_t>& face : faces) {
    face.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4U);
  }

  // Direction of a texel for the six cube faces (+X, -X, +Y, -Y, +Z, -Z),
  // following the canonical OpenGL cube-map unwrap.
  const auto direction_at{[](const int face, const int px, const int py, const int side) {
    const float texel_u{
        ((((static_cast<float>(px) + 0.5F) / static_cast<float>(side)) * 2.0F) - 1.0F)};
    const float texel_v{
        ((((static_cast<float>(py) + 0.5F) / static_cast<float>(side)) * 2.0F) - 1.0F)};
    switch (face) {
      case 0:
        return glm::vec3{1.0F, -texel_v, -texel_u};
      case 1:
        return glm::vec3{-1.0F, -texel_v, texel_u};
      case 2:
        return glm::vec3{texel_u, 1.0F, texel_v};
      case 3:
        return glm::vec3{texel_u, -1.0F, -texel_v};
      case 4:
        return glm::vec3{texel_u, -texel_v, 1.0F};
      default:
        return glm::vec3{-texel_u, -texel_v, -1.0F};
    }
  }};

  const std::array<std::uint8_t, 3> zenith{160, 190, 225};
  const std::array<std::uint8_t, 3> nadir{30, 30, 40};
  const glm::vec3 up{0.0F, 1.0F, 0.0F};

  for (std::size_t face_index{0}; face_index < faces.size(); ++face_index) {
    std::vector<std::uint8_t>& pixels{faces.at(face_index)};
    for (int py{0}; py < size; ++py) {
      for (int px{0}; px < size; ++px) {
        const glm::vec3 direction{
            glm::normalize(direction_at(static_cast<int>(face_index), px, py, size))};
        const float height{std::clamp(glm::dot(direction, up), -1.0F, 1.0F)};
        const float blend{(height + 1.0F) * 0.5F};
        const std::size_t pixel{((static_cast<std::size_t>(py) * static_cast<std::size_t>(size)) +
                                    static_cast<std::size_t>(px)) *
                                4U};
        for (std::size_t channel{0}; channel < 3U; ++channel) {
          const float low{static_cast<float>(nadir.at(channel))};
          const float high{static_cast<float>(zenith.at(channel))};
          pixels.at(pixel + channel) = static_cast<std::uint8_t>(low + ((high - low) * blend));
        }
        pixels.at(pixel + 3U) = std::uint8_t{255};
      }
    }
  }

  return faces;
}

// NOLINTEND(misc-include-cleaner)

}  // namespace App

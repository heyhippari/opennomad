#include "Texture.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mdspan>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "Core/Debug/Instrumentor.hpp"

namespace App {

Texture2D::Texture2D(const int width,
    const int height,
    const GLuint id,
    const TextureColorEncoding encoding,
    const TextureFilter filter,
    const TextureStorageFormat storage_format)
    : m_id(id),
      m_width(width),
      m_height(height),
      m_color_encoding(encoding),
      m_filter(filter),
      m_storage_format(storage_format) {}

Texture2D::Texture2D(Texture2D&& other) noexcept
    : m_id(std::exchange(other.m_id, 0)),
      m_width(other.m_width),
      m_height(other.m_height),
      m_color_encoding(other.m_color_encoding),
      m_filter(other.m_filter),
      m_storage_format(other.m_storage_format) {}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
  if (this != &other) {
    if (m_id != 0) {
      glDeleteTextures(1, &m_id);
    }
    m_id = std::exchange(other.m_id, 0);
    m_width = other.m_width;
    m_height = other.m_height;
    m_color_encoding = other.m_color_encoding;
    m_filter = other.m_filter;
    m_storage_format = other.m_storage_format;
  }
  return *this;
}

std::expected<Texture2D, std::string> Texture2D::create(const int width,
                                                        const int height,
                                                        const std::span<const std::uint8_t> rgba8,
                                                        const TextureColorEncoding encoding,
                                                        const TextureFilter filter) {
  APP_PROFILE_FUNCTION();

  if (width <= 0 || height <= 0) {
    return std::expected<Texture2D, std::string>{
        std::unexpect, "Texture2D dimensions must be positive"};
  }

  const std::size_t expected_bytes{
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U};
  if (rgba8.size() < expected_bytes) {
    return std::expected<Texture2D, std::string>{std::unexpect,
        fmt::format(
            "Texture2D: got {} bytes of pixel data, expected {}", rgba8.size(), expected_bytes)};
  }

  GLuint id{0};
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexImage2D(GL_TEXTURE_2D,
               0,
               texture_upload_internal_format(encoding),
               width,
               height,
               0,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               rgba8.data());

  const GLint min_filter{filter == TextureFilter::k_linear ? GL_LINEAR : GL_NEAREST};
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, min_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  glBindTexture(GL_TEXTURE_2D, 0);

  return Texture2D{
      width, height, id, encoding, filter, TextureStorageFormat::k_rgba8_unorm};
}

std::expected<Texture2D, std::string> Texture2D::create_render_target(
    const int width,
    const int height,
    const TextureColorEncoding encoding,
    const TextureStorageFormat storage_format) {
  APP_PROFILE_FUNCTION();

  if (width <= 0 || height <= 0) {
    return std::expected<Texture2D, std::string>{
        std::unexpect, "Texture2D dimensions must be positive"};
  }
  if (encoding == TextureColorEncoding::k_srgb &&
      storage_format != TextureStorageFormat::k_rgba8_unorm) {
    return std::expected<Texture2D, std::string>{
        std::unexpect, "sRGB render-target encoding requires RGBA8 storage"};
  }

  GLuint id{0};
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  const GLint internal_format{storage_format == TextureStorageFormat::k_rgba8_unorm
                                  ? texture_upload_internal_format(encoding)
                                  : texture_storage_internal_format(storage_format)};
  glTexImage2D(GL_TEXTURE_2D,
               0,
               internal_format,
               width,
               height,
               0,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               nullptr);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindTexture(GL_TEXTURE_2D, 0);

  return Texture2D{
      width, height, id, encoding, TextureFilter::k_linear, storage_format};
}

Texture2D::~Texture2D() {
  APP_PROFILE_FUNCTION();

  if (m_id != 0) {
    glDeleteTextures(1, &m_id);
  }
}

void Texture2D::bind(const std::uint32_t unit) const {
  APP_PROFILE_FUNCTION();

  glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
  glBindTexture(GL_TEXTURE_2D, m_id);
}

void Texture2D::update(const std::span<const std::uint8_t> rgba8) const {
  APP_PROFILE_FUNCTION();

  if (m_id == 0) {
    return;
  }
  const std::size_t expected_bytes{
      static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height) * 4U};
  if (rgba8.size() < expected_bytes) {
    return;
  }

  glBindTexture(GL_TEXTURE_2D, m_id);
  glTexSubImage2D(
      GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, rgba8.data());
  glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture2D::unbind() {
  glBindTexture(GL_TEXTURE_2D, 0);
}

GLuint Texture2D::id() const { return m_id; }

int Texture2D::width() const { return m_width; }

int Texture2D::height() const { return m_height; }

TextureColorEncoding Texture2D::color_encoding() const { return m_color_encoding; }

TextureFilter Texture2D::filter() const { return m_filter; }

TextureStorageFormat Texture2D::storage_format() const { return m_storage_format; }

std::expected<std::vector<std::uint8_t>, std::string> Texture2D::generate_checkerboard(
    const int width,
    const int height,
    const int squares_per_side,
    const std::array<float, 3> color_a,
    const std::array<float, 3> color_b) {
  if (width <= 0 || height <= 0 || squares_per_side <= 0) {
    return std::expected<std::vector<std::uint8_t>, std::string>{
        std::unexpect, "Checkerboard dimensions must be positive"};
  }

  const auto to_byte{[](const float channel) {
    const float clamped{std::clamp(channel, 0.0F, 1.0F)};
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
  }};

  const std::array<std::uint8_t, 3> byte_a{to_byte(color_a.at(0)), to_byte(color_a.at(1)),
                                           to_byte(color_a.at(2))};
  const std::array<std::uint8_t, 3> byte_b{to_byte(color_b.at(0)), to_byte(color_b.at(1)),
                                           to_byte(color_b.at(2))};

  const int cell_width{std::max(1, width / squares_per_side)};
  const int cell_height{std::max(1, height / squares_per_side)};

  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) *
                                   static_cast<std::size_t>(height) * 4U);

  // View the flat buffer as height x width x RGBA for index-math-free filling.
  const std::mdspan<std::uint8_t, std::dextents<std::size_t, 3>> view{
      pixels.data(),
      static_cast<std::size_t>(height),
      static_cast<std::size_t>(width),
      std::size_t{4}};

  for (int pixel_y{0}; pixel_y < height; ++pixel_y) {
    for (int pixel_x{0}; pixel_x < width; ++pixel_x) {
      const bool even{((pixel_x / cell_width) + (pixel_y / cell_height)) % 2 == 0};
      const auto& color{even ? byte_a : byte_b};

      const auto py{static_cast<std::size_t>(pixel_y)};
      const auto px{static_cast<std::size_t>(pixel_x)};
      view[py, px, 0] = color.at(0);
      view[py, px, 1] = color.at(1);
      view[py, px, 2] = color.at(2);
      view[py, px, 3] = static_cast<std::uint8_t>(255);
    }
  }

  return pixels;
}

}  // namespace App

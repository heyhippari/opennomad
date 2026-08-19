#include "IntegerTexture.hpp"

#include <glad/glad.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>

#include <fmt/format.h>

#include "Core/Debug/Instrumentor.hpp"

namespace App {

namespace {

/// GL format/type mapping for one integer texture format.
struct IntegerFormatInfo {
  GLenum internal_format;
  GLenum pixel_format;
  GLenum data_type;
  int channels;
};

[[nodiscard]] constexpr IntegerFormatInfo format_info(const IntegerFormat format) {
  switch (format) {
    case IntegerFormat::k_r8ui:
      return IntegerFormatInfo{.internal_format = GL_R8UI,
          .pixel_format = GL_RED_INTEGER,
          .data_type = GL_UNSIGNED_BYTE,
          .channels = 1};
    case IntegerFormat::k_rg8i:
      return IntegerFormatInfo{.internal_format = GL_RG8I,
          .pixel_format = GL_RG_INTEGER,
          .data_type = GL_BYTE,
          .channels = 2};
    case IntegerFormat::k_rg8ui:
      return IntegerFormatInfo{.internal_format = GL_RG8UI,
          .pixel_format = GL_RG_INTEGER,
          .data_type = GL_UNSIGNED_BYTE,
          .channels = 2};
  }
  return IntegerFormatInfo{.internal_format = GL_R8UI,
      .pixel_format = GL_RED_INTEGER,
      .data_type = GL_UNSIGNED_BYTE,
      .channels = 1};
}

}  // namespace

IntegerTexture::IntegerTexture(const int width,
                               const int height,
                               const IntegerFormat format,
                               const GLuint id)
    : m_id(id), m_width(width), m_height(height), m_format(format) {}

IntegerTexture::IntegerTexture(IntegerTexture&& other) noexcept
    : m_id(std::exchange(other.m_id, 0)),
      m_width(other.m_width),
      m_height(other.m_height),
      m_format(other.m_format) {}

IntegerTexture& IntegerTexture::operator=(IntegerTexture&& other) noexcept {
  if (this != &other) {
    if (m_id != 0) {
      glDeleteTextures(1, &m_id);
    }
    m_id = std::exchange(other.m_id, 0);
    m_width = other.m_width;
    m_height = other.m_height;
    m_format = other.m_format;
  }
  return *this;
}

std::expected<IntegerTexture, std::string> IntegerTexture::create(
    const int width,
    const int height,
    const IntegerFormat format) {
  APP_PROFILE_FUNCTION();

  if (width <= 0 || height <= 0) {
    return std::expected<IntegerTexture, std::string>{
        std::unexpect, "IntegerTexture dimensions must be positive"};
  }

  const IntegerFormatInfo info{format_info(format)};
  GLuint id{0};
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexImage2D(GL_TEXTURE_2D,
      0,
      static_cast<GLint>(info.internal_format),
      width,
      height,
      0,
      info.pixel_format,
      info.data_type,
      nullptr);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindTexture(GL_TEXTURE_2D, 0);

  return IntegerTexture{width, height, format, id};
}

std::expected<IntegerTexture, std::string> IntegerTexture::create_with_data(
    const int width,
    const int height,
    const IntegerFormat format,
    const std::span<const std::uint8_t> data) {
  APP_PROFILE_FUNCTION();

  if (width <= 0 || height <= 0) {
    return std::expected<IntegerTexture, std::string>{
        std::unexpect, "IntegerTexture dimensions must be positive"};
  }

  const IntegerFormatInfo info{format_info(format)};
  const std::size_t expected_bytes{
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
      static_cast<std::size_t>(info.channels)};
  if (data.size() < expected_bytes) {
    return std::expected<IntegerTexture, std::string>{
        std::unexpect, fmt::format("IntegerTexture: got {} bytes, expected {}",
                             data.size(),
                             expected_bytes)};
  }

  GLuint id{0};
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexImage2D(GL_TEXTURE_2D,
      0,
      static_cast<GLint>(info.internal_format),
      width,
      height,
      0,
      info.pixel_format,
      info.data_type,
      data.data());

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindTexture(GL_TEXTURE_2D, 0);

  return IntegerTexture{width, height, format, id};
}

IntegerTexture::~IntegerTexture() {
  APP_PROFILE_FUNCTION();

  if (m_id != 0) {
    glDeleteTextures(1, &m_id);
  }
}

void IntegerTexture::bind(const std::uint32_t unit) const {
  APP_PROFILE_FUNCTION();

  glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
  glBindTexture(GL_TEXTURE_2D, m_id);
}

void IntegerTexture::unbind() {
  glBindTexture(GL_TEXTURE_2D, 0);
}

GLuint IntegerTexture::id() const { return m_id; }

int IntegerTexture::width() const { return m_width; }

int IntegerTexture::height() const { return m_height; }

IntegerFormat IntegerTexture::format() const { return m_format; }

}  // namespace App

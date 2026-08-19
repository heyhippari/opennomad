#include "TextureR8.hpp"

#include <glad/glad.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"

namespace App {

TextureR8::TextureR8(const int width, const int height, const GLuint id)
    : m_id(id), m_width(width), m_height(height) {}

TextureR8::TextureR8(TextureR8&& other) noexcept
    : m_id(std::exchange(other.m_id, 0)),
      m_width(other.m_width),
      m_height(other.m_height) {}

TextureR8& TextureR8::operator=(TextureR8&& other) noexcept {
  if (this != &other) {
    if (m_id != 0) {
      glDeleteTextures(1, &m_id);
    }
    m_id = std::exchange(other.m_id, 0);
    m_width = other.m_width;
    m_height = other.m_height;
  }
  return *this;
}

std::expected<TextureR8, std::string> TextureR8::create(const int width, const int height) {
  APP_PROFILE_FUNCTION();

  if (width <= 0 || height <= 0) {
    return std::expected<TextureR8, std::string>{
        std::unexpect, "TextureR8 dimensions must be positive"};
  }

  GLuint id{0};
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexImage2D(
      GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindTexture(GL_TEXTURE_2D, 0);

  return TextureR8{width, height, id};
}

TextureR8::~TextureR8() {
  APP_PROFILE_FUNCTION();

  if (m_id != 0) {
    glDeleteTextures(1, &m_id);
  }
}

void TextureR8::bind(const std::uint32_t unit) const {
  APP_PROFILE_FUNCTION();

  glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
  glBindTexture(GL_TEXTURE_2D, m_id);
}

void TextureR8::update(const std::span<const std::uint8_t> r8) const {
  APP_PROFILE_FUNCTION();

  if (m_id == 0) {
    return;
  }
  const std::size_t expected_bytes{
      static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height)};
  if (r8.size() < expected_bytes) {
    return;
  }

  glBindTexture(GL_TEXTURE_2D, m_id);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RED, GL_UNSIGNED_BYTE, r8.data());
  glBindTexture(GL_TEXTURE_2D, 0);
}

void TextureR8::unbind() {
  glBindTexture(GL_TEXTURE_2D, 0);
}

GLuint TextureR8::id() const { return m_id; }

int TextureR8::width() const { return m_width; }

int TextureR8::height() const { return m_height; }

}  // namespace App

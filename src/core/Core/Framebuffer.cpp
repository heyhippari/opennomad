#include "Framebuffer.hpp"

#include <glad/glad.h>

#include <expected>
#include <string>
#include <utility>

#include <fmt/format.h>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Texture.hpp"

namespace App {

std::expected<Framebuffer, std::string> Framebuffer::create(const int width, const int height) {
  APP_PROFILE_FUNCTION();

  auto color{Texture2D::create(width, height, true)};
  if (!color) {
    return std::expected<Framebuffer, std::string>{std::unexpect, std::move(color).error()};
  }

  GLuint depth_renderbuffer{0};
  glGenRenderbuffers(1, &depth_renderbuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, depth_renderbuffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  GLuint framebuffer{0};
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color->id(), 0);
  glFramebufferRenderbuffer(
      GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_renderbuffer);

  const GLenum status{glCheckFramebufferStatus(GL_FRAMEBUFFER)};
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    glDeleteRenderbuffers(1, &depth_renderbuffer);
    glDeleteFramebuffers(1, &framebuffer);
    return std::expected<Framebuffer, std::string>{
        std::unexpect, fmt::format("Framebuffer incomplete: status 0x{:x}", status)};
  }

  return Framebuffer{std::move(color).value(), depth_renderbuffer, framebuffer, width, height};
}

Framebuffer::Framebuffer(Texture2D color,
                         const GLuint depth_renderbuffer,
                         const GLuint framebuffer,
                         const int width,
                         const int height)
    : m_color(std::move(color)),
      m_depth_renderbuffer(depth_renderbuffer),
      m_framebuffer(framebuffer),
      m_width(width),
      m_height(height) {}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : m_color(std::move(other.m_color)),
      m_depth_renderbuffer(std::exchange(other.m_depth_renderbuffer, 0)),
      m_framebuffer(std::exchange(other.m_framebuffer, 0)),
      m_width(other.m_width),
      m_height(other.m_height) {}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
  if (this != &other) {
    if (m_framebuffer != 0) {
      glDeleteFramebuffers(1, &m_framebuffer);
    }
    if (m_depth_renderbuffer != 0) {
      glDeleteRenderbuffers(1, &m_depth_renderbuffer);
    }
    m_color = std::move(other.m_color);
    m_depth_renderbuffer = std::exchange(other.m_depth_renderbuffer, 0);
    m_framebuffer = std::exchange(other.m_framebuffer, 0);
    m_width = other.m_width;
    m_height = other.m_height;
  }
  return *this;
}

Framebuffer::~Framebuffer() {
  APP_PROFILE_FUNCTION();

  if (m_framebuffer != 0) {
    glDeleteFramebuffers(1, &m_framebuffer);
  }
  if (m_depth_renderbuffer != 0) {
    glDeleteRenderbuffers(1, &m_depth_renderbuffer);
  }
}

void Framebuffer::bind() const {
  glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
}

void Framebuffer::unbind() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

const Texture2D& Framebuffer::color_texture() const { return m_color; }

int Framebuffer::width() const { return m_width; }

int Framebuffer::height() const { return m_height; }

}  // namespace App

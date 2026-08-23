#include "Framebuffer.hpp"

#include <fmt/format.h>
#include <glad/glad.h>

#include <expected>
#include <string>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Texture.hpp"

namespace App {

DepthStencilBuffer::DepthStencilBuffer(
    const GLuint id, const int width, const int height, const DepthStencilFormat format)
    : m_id(id),
      m_width(width),
      m_height(height),
      m_format(format) {}

std::expected<DepthStencilBuffer, std::string> DepthStencilBuffer::create(
    const int width, const int height, const DepthStencilFormat format) {
  if (width <= 0 || height <= 0 || format == DepthStencilFormat::k_none) {
    return std::expected<DepthStencilBuffer, std::string>{
        std::unexpect, "shared depth/stencil requires positive dimensions and a format"};
  }
  GLuint id{0};
  glGenRenderbuffers(1, &id);
  glBindRenderbuffer(GL_RENDERBUFFER, id);
  const GLenum internal_format{
      static_cast<GLenum>(format == DepthStencilFormat::k_depth24_stencil8 ? GL_DEPTH24_STENCIL8
                                                                           : GL_DEPTH_COMPONENT24)};
  glRenderbufferStorage(GL_RENDERBUFFER, internal_format, width, height);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  return DepthStencilBuffer{id, width, height, format};
}

DepthStencilBuffer::DepthStencilBuffer(DepthStencilBuffer&& other) noexcept
    : m_id(std::exchange(other.m_id, 0)),
      m_width(other.m_width),
      m_height(other.m_height),
      m_format(other.m_format) {}

DepthStencilBuffer& DepthStencilBuffer::operator=(DepthStencilBuffer&& other) noexcept {
  if (this != &other) {
    if (m_id != 0U) {
      glDeleteRenderbuffers(1, &m_id);
    }
    m_id = std::exchange(other.m_id, 0);
    m_width = other.m_width;
    m_height = other.m_height;
    m_format = other.m_format;
  }
  return *this;
}

DepthStencilBuffer::~DepthStencilBuffer() {
  if (m_id != 0U) {
    glDeleteRenderbuffers(1, &m_id);
  }
}

GLuint DepthStencilBuffer::id() const {
  return m_id;
}
int DepthStencilBuffer::width() const {
  return m_width;
}
int DepthStencilBuffer::height() const {
  return m_height;
}
DepthStencilFormat DepthStencilBuffer::format() const {
  return m_format;
}

std::expected<Framebuffer, std::string> Framebuffer::create(
    const int width, const int height, const FramebufferDescription description) {
  APP_PROFILE_FUNCTION();

  auto color{Texture2D::create_render_target(
      width, height, description.color_encoding, description.color_storage)};
  if (!color) {
    return std::expected<Framebuffer, std::string>{std::unexpect, std::move(color).error()};
  }

  GLuint depth_stencil_renderbuffer{0};
  if (description.depth_stencil != DepthStencilFormat::k_none) {
    glGenRenderbuffers(1, &depth_stencil_renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_stencil_renderbuffer);
    const GLenum internal_format{
        static_cast<GLenum>(description.depth_stencil == DepthStencilFormat::k_depth24_stencil8
                                ? GL_DEPTH24_STENCIL8
                                : GL_DEPTH_COMPONENT24)};
    glRenderbufferStorage(GL_RENDERBUFFER, internal_format, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
  }

  GLuint framebuffer{0};
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color->id(), 0);
  if (description.depth_stencil != DepthStencilFormat::k_none) {
    const GLenum attachment{
        static_cast<GLenum>(description.depth_stencil == DepthStencilFormat::k_depth24_stencil8
                                ? GL_DEPTH_STENCIL_ATTACHMENT
                                : GL_DEPTH_ATTACHMENT)};
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, depth_stencil_renderbuffer);
  }

  const GLenum status{glCheckFramebufferStatus(GL_FRAMEBUFFER)};
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    if (depth_stencil_renderbuffer != 0U) {
      glDeleteRenderbuffers(1, &depth_stencil_renderbuffer);
    }
    glDeleteFramebuffers(1, &framebuffer);
    return std::expected<Framebuffer, std::string>{
        std::unexpect, fmt::format("Framebuffer incomplete: status 0x{:x}", status)};
  }

  return Framebuffer{std::move(color).value(),
      depth_stencil_renderbuffer,
      framebuffer,
      width,
      height,
      description};
}

std::expected<Framebuffer, std::string> Framebuffer::create_with_shared_depth(const int width,
    const int height,
    const FramebufferDescription description,
    const DepthStencilBuffer& depth_stencil) {
  APP_PROFILE_FUNCTION();
  if (description.depth_stencil == DepthStencilFormat::k_none ||
      description.depth_stencil != depth_stencil.format() || width != depth_stencil.width() ||
      height != depth_stencil.height()) {
    return std::expected<Framebuffer, std::string>{
        std::unexpect, "framebuffer shared depth/stencil description mismatch"};
  }
  auto color{Texture2D::create_render_target(
      width, height, description.color_encoding, description.color_storage)};
  if (!color) {
    return std::expected<Framebuffer, std::string>{std::unexpect, std::move(color).error()};
  }
  GLuint framebuffer{0};
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color->id(), 0);
  const GLenum attachment{
      static_cast<GLenum>(description.depth_stencil == DepthStencilFormat::k_depth24_stencil8
                              ? GL_DEPTH_STENCIL_ATTACHMENT
                              : GL_DEPTH_ATTACHMENT)};
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, depth_stencil.id());
  const GLenum status{glCheckFramebufferStatus(GL_FRAMEBUFFER)};
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    glDeleteFramebuffers(1, &framebuffer);
    return std::expected<Framebuffer, std::string>{
        std::unexpect, fmt::format("Framebuffer incomplete: status 0x{:x}", status)};
  }
  return Framebuffer{std::move(color).value(), 0, framebuffer, width, height, description};
}

Framebuffer::Framebuffer(Texture2D color,
    const GLuint depth_stencil_renderbuffer,
    const GLuint framebuffer,
    const int width,
    const int height,
    const FramebufferDescription description)
    : m_color(std::move(color)),
      m_depth_stencil_renderbuffer(depth_stencil_renderbuffer),
      m_framebuffer(framebuffer),
      m_width(width),
      m_height(height),
      m_description(description) {}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : m_color(std::move(other.m_color)),
      m_depth_stencil_renderbuffer(std::exchange(other.m_depth_stencil_renderbuffer, 0)),
      m_framebuffer(std::exchange(other.m_framebuffer, 0)),
      m_width(other.m_width),
      m_height(other.m_height),
      m_description(other.m_description) {}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
  if (this != &other) {
    if (m_framebuffer != 0) {
      glDeleteFramebuffers(1, &m_framebuffer);
    }
    if (m_depth_stencil_renderbuffer != 0) {
      glDeleteRenderbuffers(1, &m_depth_stencil_renderbuffer);
    }
    m_color = std::move(other.m_color);
    m_depth_stencil_renderbuffer = std::exchange(other.m_depth_stencil_renderbuffer, 0);
    m_framebuffer = std::exchange(other.m_framebuffer, 0);
    m_width = other.m_width;
    m_height = other.m_height;
    m_description = other.m_description;
  }
  return *this;
}

Framebuffer::~Framebuffer() {
  APP_PROFILE_FUNCTION();

  if (m_framebuffer != 0) {
    glDeleteFramebuffers(1, &m_framebuffer);
  }
  if (m_depth_stencil_renderbuffer != 0) {
    glDeleteRenderbuffers(1, &m_depth_stencil_renderbuffer);
  }
}

void Framebuffer::bind() const {
  glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
}

void Framebuffer::unbind() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

const Texture2D& Framebuffer::color_texture() const {
  return m_color;
}

int Framebuffer::width() const {
  return m_width;
}

int Framebuffer::height() const {
  return m_height;
}

const FramebufferDescription& Framebuffer::description() const {
  return m_description;
}

}  // namespace App

#include "UniformBuffer.hpp"

#include <glad/glad.h>

#include <cstddef>
#include <span>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"

namespace App {

UniformBuffer::UniformBuffer(const std::span<const std::byte> data, const GLenum usage) {
  APP_PROFILE_FUNCTION();

  glGenBuffers(1, &m_id);  // NOLINT(cppcoreguidelines-prefer-member-initializer)
  glBindBuffer(GL_UNIFORM_BUFFER, m_id);
  glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(data.size_bytes()), data.data(), usage);
}

UniformBuffer::UniformBuffer(UniformBuffer&& other) noexcept : m_id(std::exchange(other.m_id, 0)) {}

UniformBuffer& UniformBuffer::operator=(UniformBuffer&& other) noexcept {
  if (this != &other) {
    if (m_id != 0) {
      glDeleteBuffers(1, &m_id);
    }
    m_id = std::exchange(other.m_id, 0);
  }
  return *this;
}

UniformBuffer::~UniformBuffer() {
  APP_PROFILE_FUNCTION();

  if (m_id != 0) {
    glDeleteBuffers(1, &m_id);
  }
}

void UniformBuffer::bind() const {
  glBindBuffer(GL_UNIFORM_BUFFER, m_id);
}

void UniformBuffer::unbind() {
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void UniformBuffer::bind_base(const GLuint binding_point) const {
  glBindBufferBase(GL_UNIFORM_BUFFER, binding_point, m_id);
}

}  // namespace App

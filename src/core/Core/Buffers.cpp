#include "Buffers.hpp"

#include <glad/glad.h>

#include <cstddef>
#include <cstdint>
#include <span>

#include "Core/Debug/Instrumentor.hpp"

namespace App {

VertexBuffer::VertexBuffer(const std::span<const std::byte> data, const GLenum usage)
    : m_size(data.size_bytes()) {
  APP_PROFILE_FUNCTION();

  glGenBuffers(1, &m_id);  // NOLINT(cppcoreguidelines-prefer-member-initializer)
  glBindBuffer(GL_ARRAY_BUFFER, m_id);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size_bytes()), data.data(), usage);
}

VertexBuffer::~VertexBuffer() {
  APP_PROFILE_FUNCTION();

  if (m_id != 0) {
    glDeleteBuffers(1, &m_id);
  }
}

void VertexBuffer::bind() const {
  glBindBuffer(GL_ARRAY_BUFFER, m_id);
}

void VertexBuffer::unbind() {
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VertexBuffer::upload(const std::span<const std::byte> data) {
  APP_PROFILE_FUNCTION();

  glBindBuffer(GL_ARRAY_BUFFER, m_id);
  if (data.size_bytes() > m_size) {
    glBufferData(
        GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size_bytes()), data.data(), GL_DYNAMIC_DRAW);
    m_size = data.size_bytes();
  } else {
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(data.size_bytes()), data.data());
  }
}

void VertexBuffer::update(
    const std::size_t offset_bytes, const std::span<const std::byte> data) const {
  APP_PROFILE_FUNCTION();

  if (offset_bytes >= m_size || data.size_bytes() > m_size - offset_bytes) {
    return;  // Out-of-range updates are ignored; callers size the buffer first.
  }
  glBindBuffer(GL_ARRAY_BUFFER, m_id);
  glBufferSubData(GL_ARRAY_BUFFER,
      static_cast<GLintptr>(offset_bytes),
      static_cast<GLsizeiptr>(data.size_bytes()),
      data.data());
}

IndexBuffer::IndexBuffer(const std::span<const std::uint32_t> indices, const GLenum usage)
    : m_count(static_cast<std::uint32_t>(indices.size())),
      m_capacity(indices.size()) {
  APP_PROFILE_FUNCTION();

  glGenBuffers(1, &m_id);  // NOLINT(cppcoreguidelines-prefer-member-initializer)
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_id);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(indices.size_bytes()),
      indices.data(),
      usage);
}

IndexBuffer::~IndexBuffer() {
  APP_PROFILE_FUNCTION();

  if (m_id != 0) {
    glDeleteBuffers(1, &m_id);
  }
}

void IndexBuffer::bind() const {
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_id);
}

void IndexBuffer::unbind() {
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void IndexBuffer::upload(const std::span<const std::uint32_t> indices) {
  APP_PROFILE_FUNCTION();

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_id);
  if (indices.size() > m_capacity) {
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size_bytes()),
        indices.data(),
        GL_DYNAMIC_DRAW);
    m_capacity = indices.size();
  } else {
    glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(indices.size_bytes()), indices.data());
  }
  m_count = static_cast<std::uint32_t>(indices.size());
}

std::uint32_t IndexBuffer::count() const {
  return m_count;
}

}  // namespace App

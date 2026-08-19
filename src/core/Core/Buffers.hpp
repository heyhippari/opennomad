#pragma once

#include <glad/glad.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace App {

/// RAII wrapper around an OpenGL vertex buffer object (GL_ARRAY_BUFFER).
class VertexBuffer {
 public:
  explicit VertexBuffer(std::span<const std::byte> data, GLenum usage = GL_STATIC_DRAW);
  ~VertexBuffer();

  VertexBuffer(const VertexBuffer&) = delete;
  VertexBuffer(VertexBuffer&&) = delete;
  VertexBuffer& operator=(VertexBuffer other) = delete;
  VertexBuffer& operator=(VertexBuffer&& other) = delete;

  void bind() const;
  static void unbind();

  /// Replaces the buffer contents. Grows the storage with GL_DYNAMIC_DRAW
  /// when the data exceeds the current capacity; otherwise overwrites the
  /// prefix in place via glBufferSubData.
  void upload(std::span<const std::byte> data);

  /// Overwrites a byte range of the current storage. The range must fit
  /// the capacity; out-of-range updates are ignored.
  void update(std::size_t offset_bytes, std::span<const std::byte> data) const;

 private:
  GLuint m_id{0};
  /// Current storage capacity in bytes.
  std::size_t m_size{0};
};

/// RAII wrapper around an OpenGL index buffer object (GL_ELEMENT_ARRAY_BUFFER).
class IndexBuffer {
 public:
  explicit IndexBuffer(std::span<const std::uint32_t> indices, GLenum usage = GL_STATIC_DRAW);
  ~IndexBuffer();

  IndexBuffer(const IndexBuffer&) = delete;
  IndexBuffer(IndexBuffer&&) = delete;
  IndexBuffer& operator=(IndexBuffer other) = delete;
  IndexBuffer& operator=(IndexBuffer&& other) = delete;

  void bind() const;
  static void unbind();

  /// Number of 32-bit indices held by the buffer.
  [[nodiscard]] std::uint32_t count() const;

 private:
  GLuint m_id{0};
  std::uint32_t m_count{0};
};

}  // namespace App

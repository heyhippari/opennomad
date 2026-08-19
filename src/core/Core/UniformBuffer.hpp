#pragma once

#include <glad/glad.h>

#include <cstddef>
#include <span>

namespace App {

/// RAII wrapper around an OpenGL uniform buffer object (GL_UNIFORM_BUFFER).
///
/// Holds immutable block data (e.g. the std140 light array) shared with
/// shaders through a uniform-block binding point.
class UniformBuffer {
 public:
  explicit UniformBuffer(std::span<const std::byte> data, GLenum usage = GL_STATIC_DRAW);

  UniformBuffer(UniformBuffer&& other) noexcept;
  UniformBuffer& operator=(UniformBuffer&& other) noexcept;
  ~UniformBuffer();

  UniformBuffer(const UniformBuffer&) = delete;
  UniformBuffer& operator=(const UniformBuffer&) = delete;

  void bind() const;
  static void unbind();

  /// Binds the buffer to a uniform block binding point (glBindBufferBase).
  void bind_base(GLuint binding_point) const;

 private:
  GLuint m_id{0};
};

}  // namespace App

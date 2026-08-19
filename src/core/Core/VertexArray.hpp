#pragma once

#include <glad/glad.h>

namespace App {

/// RAII wrapper around an OpenGL vertex array object.
class VertexArray {
 public:
  VertexArray();
  ~VertexArray();

  VertexArray(const VertexArray&) = delete;
  VertexArray(VertexArray&&) = delete;
  VertexArray& operator=(VertexArray other) = delete;
  VertexArray& operator=(VertexArray&& other) = delete;

  void bind() const;
  static void unbind();

 private:
  GLuint m_id{0};
};

}  // namespace App

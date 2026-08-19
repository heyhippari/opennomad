#include "VertexArray.hpp"

#include <glad/glad.h>

#include "Core/Debug/Instrumentor.hpp"

namespace App {

VertexArray::VertexArray() {
  APP_PROFILE_FUNCTION();

  glGenVertexArrays(1, &m_id);  // NOLINT(cppcoreguidelines-prefer-member-initializer)
}

VertexArray::~VertexArray() {
  APP_PROFILE_FUNCTION();

  if (m_id != 0) {
    glDeleteVertexArrays(1, &m_id);
  }
}

void VertexArray::bind() const {
  glBindVertexArray(m_id);
}

void VertexArray::unbind() {
  glBindVertexArray(0);
}

}  // namespace App

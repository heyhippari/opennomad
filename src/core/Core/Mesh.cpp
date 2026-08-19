#include "Mesh.hpp"

#include <glad/glad.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Vertex.hpp"

namespace App {

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices)
    : m_vertex_buffer(std::as_bytes(std::span{vertices})),
      m_index_buffer(std::span{indices}) {
  APP_PROFILE_FUNCTION();

  m_vertex_array.bind();
  m_vertex_buffer.bind();

  const GLsizei stride{static_cast<GLsizei>(sizeof(Vertex))};

  // Attribute layout mirrors App::Vertex (see Vertex.hpp).
  glEnableVertexAttribArray(Vertex::k_position_location);
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
  // Required by the GL API.
  glVertexAttribPointer(Vertex::k_position_location,
                        3,
                        GL_FLOAT,
                        GL_FALSE,
                        stride,
                        reinterpret_cast<const void*>(offsetof(Vertex, position)));

  glEnableVertexAttribArray(Vertex::k_normal_location);
  glVertexAttribPointer(Vertex::k_normal_location,
                        3,
                        GL_FLOAT,
                        GL_FALSE,
                        stride,
                        reinterpret_cast<const void*>(offsetof(Vertex, normal)));

  glEnableVertexAttribArray(Vertex::k_uv_location);
  glVertexAttribPointer(Vertex::k_uv_location,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        stride,
                        reinterpret_cast<const void*>(offsetof(Vertex, uv)));

  glEnableVertexAttribArray(Vertex::k_color_location);
  glVertexAttribPointer(Vertex::k_color_location,
                        4,
                        GL_FLOAT,
                        GL_FALSE,
                        stride,
                        reinterpret_cast<const void*>(offsetof(Vertex, color)));
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)

  m_index_buffer.bind();

  VertexArray::unbind();
  VertexBuffer::unbind();
  IndexBuffer::unbind();
}

void Mesh::draw() const {
  APP_PROFILE_FUNCTION();

  m_vertex_array.bind();
  glDrawElements(GL_TRIANGLES,
                 static_cast<GLsizei>(m_index_buffer.count()),
                 GL_UNSIGNED_INT,
                 nullptr);
  VertexArray::unbind();
}

}  // namespace App

#pragma once

#include <cstdint>
#include <vector>

#include "Core/Buffers.hpp"
#include "Core/Vertex.hpp"
#include "Core/VertexArray.hpp"

namespace App {

/// CPU-side geometry uploaded into a VAO + vertex/index buffers.
///
/// The attribute layout is fixed by the App::Vertex format.
class Mesh {
 public:
  Mesh(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);
  ~Mesh() = default;

  Mesh(const Mesh&) = delete;
  Mesh(Mesh&&) = delete;
  Mesh& operator=(Mesh other) = delete;
  Mesh& operator=(Mesh&& other) = delete;

  /// Binds the VAO and draws all triangles.
  void draw() const;

 private:
  VertexArray m_vertex_array;
  VertexBuffer m_vertex_buffer;
  IndexBuffer m_index_buffer;
};

}  // namespace App

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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

  /// Overwrites the vertex data in place, keeping the index buffer. Returns
  /// false without touching GL state when the vertex count differs.
  [[nodiscard]] bool update_vertices(std::span<const Vertex> vertices) const;

  /// Attaches or overwrites the VertexColorSlopes stream (locations 4 and 5),
  /// held in a second buffer so pose updates never touch it. Returns false
  /// without touching GL state when the count differs from the vertices.
  [[nodiscard]] bool set_color_slopes(std::span<const VertexColorSlopes> slopes);

  [[nodiscard]] bool has_color_slopes() const {
    return m_color_slope_buffer.has_value();
  }

  [[nodiscard]] std::size_t vertex_count() const {
    return m_vertex_count;
  }

 private:
  VertexArray m_vertex_array;
  VertexBuffer m_vertex_buffer;
  IndexBuffer m_index_buffer;
  std::optional<VertexBuffer> m_color_slope_buffer;
  std::size_t m_vertex_count{0};
};

}  // namespace App

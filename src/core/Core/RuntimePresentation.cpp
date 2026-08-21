#include "Core/RuntimePresentation.hpp"

// NOLINTBEGIN(misc-include-cleaner) -- GLM umbrella headers are canonical.
#include <array>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace App::Runtime::Presentation {

Vec3 to_gl(const Vec3& value) {
  return Vec3{.x = value.x, .y = -value.y, .z = -value.z};
}

std::array<float, 3> to_gl(const std::array<float, 3>& value) {
  return {value.at(0), -value.at(1), -value.at(2)};
}

Vertex to_gl(const Vertex& vertex) {
  Vertex result{vertex};
  result.position = to_gl(vertex.position);
  result.normal = to_gl(vertex.normal);
  return result;
}

glm::mat4 to_gl(const Transform& transform) {
  std::array<float, 16> result{1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F};
  constexpr std::array<float, 3> k_basis{1.0F, -1.0F, -1.0F};
  const std::array<float, 3> scale{transform.scale.x, transform.scale.y, transform.scale.z};
  for (std::size_t runtime_row{0}; runtime_row < 3U; ++runtime_row) {
    for (std::size_t runtime_column{0}; runtime_column < 3U; ++runtime_column) {
      // Flat GL storage is column-major. Transpose(Runtime) places
      // Runtime[row][column] in GL column=row, GL row=column.
      result.at((runtime_row * 4U) + runtime_column) =
          k_basis.at(runtime_column) * transform.matrix.at(runtime_row, runtime_column) *
          scale.at(runtime_row) * k_basis.at(runtime_row);
    }
  }
  result.at(12) = transform.translation.x;
  result.at(13) = -transform.translation.y;
  result.at(14) = -transform.translation.z;
  return glm::make_mat4(result.data());
}

}  // namespace App::Runtime::Presentation

// NOLINTEND(misc-include-cleaner)

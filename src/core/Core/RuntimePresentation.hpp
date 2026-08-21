#pragma once

// NOLINTNEXTLINE(misc-include-cleaner) -- GLM umbrella header is canonical.
#include <array>
#include <glm/glm.hpp>

#include "Core/RuntimeMath.hpp"
#include "Core/Vertex.hpp"

namespace App::Runtime::Presentation {

/// Runtime -> conventional OpenGL basis, B = diag(1, -1, -1). This is a
/// 180-degree X rotation (determinant +1), so it does not reverse winding.
[[nodiscard]] Vec3 to_gl(const Vec3& value);
[[nodiscard]] std::array<float, 3> to_gl(const std::array<float, 3>& value);
[[nodiscard]] Vertex to_gl(const Vertex& vertex);

/// Adapts a Runtime row-vector affine transform into a GLM column-vector
/// matrix. The linear part is `B * transpose(S * R) * B`, where Runtime scale
/// multiplies rows; translation is `t * B`.
[[nodiscard]] glm::mat4 to_gl(const Transform& transform);

[[nodiscard]] constexpr float basis_determinant() {
  return 1.0F;
}

}  // namespace App::Runtime::Presentation

#pragma once

#include <array>
#include <span>

namespace App {

/// Sphere in the space the frustum was built for (GL presentation space for world rendering).
struct BoundingSphere {
  std::array<float, 3> center{};
  float radius{0.0F};
};

/// Six-plane view frustum extracted from a combined projection * view matrix.
///
/// Culling is deliberately conservative: a sphere is rejected only when it lies entirely behind one
/// plane by more than a small safety margin, so rounding error can never remove visible geometry.
/// Non-finite input is always reported as visible.
class ViewFrustum {
 public:
  /// Extracts normalized planes from a column-major OpenGL clip matrix (Gribb-Hartmann).
  [[nodiscard]] static ViewFrustum from_clip_matrix(std::span<const float, 16> clip_from_world);

  /// False only when the sphere is provably outside the frustum.
  [[nodiscard]] bool intersects(const BoundingSphere& sphere) const;

 private:
  /// Inward-facing planes {nx, ny, nz, d}: left, right, bottom, top, near, far.
  std::array<std::array<float, 4>, 6> m_planes{};
};

}  // namespace App

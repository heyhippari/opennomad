#include "Core/ViewFrustum.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace App {

namespace {

/// Absolute slack (world units) plus a radius-relative term covering float rounding in the matrix
/// product and in the plane normalization.
constexpr float K_ABSOLUTE_MARGIN{1.0F};
constexpr float K_RELATIVE_MARGIN{0.01F};

[[nodiscard]] float row_element(
    const std::span<const float, 16> matrix, const std::size_t row, const std::size_t column) {
  return matrix[(column * 4U) + row];
}

}  // namespace

ViewFrustum ViewFrustum::from_clip_matrix(const std::span<const float, 16> clip_from_world) {
  ViewFrustum frustum;
  // Plane k is row3 +/- row_i; the sign selects the near or far side of each clip axis.
  constexpr std::array<std::array<float, 2>, 6> K_PLANE_SOURCES{{
      {0.0F, 1.0F},   // left   : row3 + row0
      {0.0F, -1.0F},  // right  : row3 - row0
      {1.0F, 1.0F},   // bottom : row3 + row1
      {1.0F, -1.0F},  // top    : row3 - row1
      {2.0F, 1.0F},   // near   : row3 + row2
      {2.0F, -1.0F},  // far    : row3 - row2
  }};
  for (std::size_t plane_index{0}; plane_index < K_PLANE_SOURCES.size(); ++plane_index) {
    const std::size_t row{static_cast<std::size_t>(K_PLANE_SOURCES.at(plane_index).at(0))};
    const float sign{K_PLANE_SOURCES.at(plane_index).at(1)};
    std::array<float, 4> plane{};
    for (std::size_t column{0}; column < 4U; ++column) {
      plane.at(column) = row_element(clip_from_world, 3U, column) +
                         (sign * row_element(clip_from_world, row, column));
    }
    const float length{std::sqrt(
        (plane.at(0) * plane.at(0)) + (plane.at(1) * plane.at(1)) + (plane.at(2) * plane.at(2)))};
    if (length > 0.0F && std::isfinite(length)) {
      for (float& coefficient : plane) {
        coefficient /= length;
      }
    } else {
      // Degenerate matrix: a zero plane yields distance 0 and never rejects.
      plane = {};
    }
    frustum.m_planes.at(plane_index) = plane;
  }
  return frustum;
}

bool ViewFrustum::intersects(const BoundingSphere& sphere) const {
  const float reach{sphere.radius + K_ABSOLUTE_MARGIN + (K_RELATIVE_MARGIN * sphere.radius)};
  for (const std::array<float, 4>& plane : m_planes) {
    const float distance{(plane.at(0) * sphere.center.at(0)) + (plane.at(1) * sphere.center.at(1)) +
                         (plane.at(2) * sphere.center.at(2)) + plane.at(3)};
    // Written as a rejection so that NaN comparisons fall through to "visible".
    if (distance < -reach) {
      return false;
    }
  }
  return true;
}

}  // namespace App

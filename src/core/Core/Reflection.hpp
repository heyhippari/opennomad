#pragma once

// NOLINTBEGIN(misc-include-cleaner)
// glm follows a "single-include" convention — the umbrella headers are the
// canonical way to pull in the library, even though clang-tidy cannot trace
// individual symbols back to a direct sub-header.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
// NOLINTEND(misc-include-cleaner)

namespace App {

/// World-space plane (normal, d) through three non-collinear points.
///
/// The normal follows (b - a) x (c - a), normalised; a point p lies on the
/// plane when dot(normal, p) + d == 0.
[[nodiscard]] inline glm::vec4 plane_from_points(const glm::vec3 a,
                                                 const glm::vec3 b,
                                                 const glm::vec3 c) {
  const glm::vec3 normal{glm::normalize(glm::cross(b - a, c - a))};
  return glm::vec4{normal, -glm::dot(normal, a)};
}

/// Reflects a point through a plane (mirror symmetry).
[[nodiscard]] inline glm::vec3 reflect_point(const glm::vec3 point, const glm::vec4 plane) {
  const glm::vec3 normal{glm::vec3{plane}};
  const float distance{glm::dot(normal, point) + plane[3]};
  return point - ((2.0F * distance) * normal);
}

/// Reflects a direction vector through a plane.
[[nodiscard]] inline glm::vec3 reflect_direction(const glm::vec3 direction,
                                                 const glm::vec4 plane) {
  const glm::vec3 normal{glm::vec3{plane}};
  return direction - ((2.0F * glm::dot(normal, direction)) * normal);
}

/// Camera basis recovered from a rigid view matrix (glm::lookAt).
///
/// The rotation block stores the basis in its rows (row 0 = right,
/// row 1 = up, row 2 = -front), so the columns of the inverse are the
/// world-space basis vectors. Reading the columns of the view itself mixes
/// basis components and is only correct for the identity rotation.
struct ViewBasis {
  glm::vec3 right{};
  glm::vec3 up{};
  glm::vec3 front{};
};

/// Recovers the world-space camera basis of a rigid view matrix.
[[nodiscard]] inline ViewBasis view_basis(const glm::mat4 view) {
  const glm::mat4 inverse{glm::inverse(view)};
  return ViewBasis{.right = glm::vec3{inverse[0]},
      .up = glm::vec3{inverse[1]},
      .front = -glm::vec3{inverse[2]}};
}

/// Builds the view matrix of a camera mirrored through a plane.
///
/// The result has the opposite handedness of the input view; renderers must
/// flip the front-face winding while using it.
[[nodiscard]] inline glm::mat4 reflected_view_matrix(const glm::mat4 view,
                                                     const glm::vec4 plane) {
  // Recover the camera pose from the view matrix.
  const glm::mat4 inverse{glm::inverse(view)};
  const glm::vec3 eye{glm::vec3{inverse[3]}};
  const glm::vec3 up{glm::vec3{inverse[1]}};
  const glm::vec3 front{-glm::vec3{inverse[2]}};

  return glm::lookAt(reflect_point(eye, plane),
                     reflect_point(eye, plane) + reflect_direction(front, plane),
                     reflect_direction(up, plane));
}

}  // namespace App

#include "Camera.hpp"

// NOLINTBEGIN(misc-include-cleaner)
// glm follows a "single-include" convention — the umbrella headers are the
// canonical way to pull in the library, even though clang-tidy cannot trace
// individual symbols back to a direct sub-header.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <span>

namespace App {

Camera::Camera(const float fov_degrees,
               const float aspect_ratio,
               const float near_plane,
               const float far_plane)
    : m_fov(fov_degrees),
      m_aspect_ratio(aspect_ratio),
      m_near(near_plane),
      m_far(far_plane) {
  set_aspect_ratio(m_aspect_ratio);
}

void Camera::set_aspect_ratio(const float aspect_ratio) {
  m_aspect_ratio = aspect_ratio;
  const glm::mat4 proj{
      glm::perspective(glm::radians(m_fov), m_aspect_ratio, m_near, m_far)};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  std::copy_n(glm::value_ptr(proj), 16, std::begin(m_projection_matrix));
}

std::span<const float, 16> Camera::get_view_matrix() const {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) — lazy cached const.
  const_cast<Camera*>(this)->update_view_matrix();
  return m_view_matrix;
}

std::span<const float, 16> Camera::get_projection_matrix() const { return m_projection_matrix; }

std::span<const float, 3> Camera::get_position() const { return m_position; }

float Camera::get_yaw() const { return m_yaw; }

float Camera::get_pitch() const { return m_pitch; }

float Camera::get_near_plane() const { return m_near; }

float Camera::get_far_plane() const { return m_far; }

void Camera::set_position(const float pos_x, const float pos_y, const float pos_z) {
  m_position[0] = pos_x;
  m_position[1] = pos_y;
  m_position[2] = pos_z;
  m_view_dirty = true;
}

void Camera::set_rotation(const float yaw_degrees, const float pitch_degrees) {
  m_yaw = yaw_degrees;
  m_pitch = pitch_degrees;
  m_view_dirty = true;
}

void Camera::look_at(const float target_x, const float target_y, const float target_z) {
  const glm::vec3 offset{target_x - m_position[0], target_y - m_position[1],
                         target_z - m_position[2]};
  if (glm::length(offset) == 0.0F) {
    // Target coincides with the eye; keep the current orientation.
    return;
  }

  const glm::vec3 direction{glm::normalize(offset)};

  // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
  // glm::vec3 uses an anonymous union for .x/.y/.z — idiomatic GLM usage.
  m_pitch = glm::degrees(std::asin(direction.y));
  m_yaw = glm::degrees(std::atan2(direction.x, direction.z));
  // NOLINTEND(cppcoreguidelines-pro-type-union-access)
  m_view_dirty = true;
}

void Camera::update_view_matrix() {
  if (!m_view_dirty) {
    return;
  }

  const float yaw_rad{glm::radians(m_yaw)};
  const float pitch_rad{glm::radians(m_pitch)};

  // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
  // glm::vec3 uses an anonymous union for .x/.y/.z — idiomatic GLM usage.
  glm::vec3 front{};
  front.x = std::cos(pitch_rad) * std::sin(yaw_rad);
  front.y = std::sin(pitch_rad);
  front.z = std::cos(pitch_rad) * std::cos(yaw_rad);
  // NOLINTEND(cppcoreguidelines-pro-type-union-access)
  front = glm::normalize(front);

  constexpr glm::vec3 k_world_up{0.0F, 1.0F, 0.0F};
  const glm::vec3 right{glm::normalize(glm::cross(front, k_world_up))};
  const glm::vec3 up{glm::cross(right, front)};

  const glm::vec3 eye{m_position[0], m_position[1], m_position[2]};
  const glm::vec3 center{eye + front};
  const glm::mat4 view{glm::lookAt(eye, center, up)};

  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  std::copy_n(glm::value_ptr(view), 16, std::begin(m_view_matrix));

  m_view_dirty = false;
}

// NOLINTEND(misc-include-cleaner)

}  // namespace App

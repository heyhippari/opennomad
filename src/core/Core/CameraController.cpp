#include "CameraController.hpp"

// NOLINTBEGIN(misc-include-cleaner)
// glm follows a "single-include" convention — the umbrella headers are the
// canonical way to pull in the library, even though clang-tidy cannot trace
// individual symbols back to a direct sub-header.
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Core/Input/InputAction.hpp"

namespace App {

namespace {

constexpr glm::vec3 K_WORLD_UP{0.0F, 1.0F, 0.0F};

}  // namespace

CameraController::CameraController(Camera& camera) : m_camera(camera) {}

void CameraController::update(const App::Input::InputManager& input, const float delta_time) {
  using App::Input::Action;

  // --- Mouse look (per-frame deltas, so no delta_time scaling) ---
  const float yaw_delta{input.get_action_value(Action::k_look_yaw) * m_look_sensitivity};
  // Mouse down (positive delta) looks down, so the pitch axis is inverted.
  const float pitch_delta{-(input.get_action_value(Action::k_look_pitch) * m_look_sensitivity)};

  // Screen-right in world space is cross(front, up) — the lookAt "s" basis —
  // which at yaw 0 (facing +Z) is -X. Turning toward it therefore *decreases*
  // yaw, so the mouse x-axis is inverted to make mouse-right turn right.
  const float new_yaw{m_camera.get_yaw() - yaw_delta};
  const float new_pitch{
      std::clamp(m_camera.get_pitch() + pitch_delta, -k_max_pitch_degrees, k_max_pitch_degrees)};
  m_camera.set_rotation(new_yaw, new_pitch);

  // --- Movement along the camera basis ---
  const float yaw_rad{glm::radians(new_yaw)};
  const float pitch_rad{glm::radians(new_pitch)};

  // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
  // glm::vec3 uses an anonymous union for .x/.y/.z — idiomatic GLM usage.
  glm::vec3 front{};
  front.x = std::cos(pitch_rad) * std::sin(yaw_rad);
  front.y = std::sin(pitch_rad);
  front.z = std::cos(pitch_rad) * std::cos(yaw_rad);
  // NOLINTEND(cppcoreguidelines-pro-type-union-access)
  front = glm::normalize(front);

  // Cross(front, up) is the screen-right direction in world space — the
  // same basis the view matrix derives from glm::lookAt.
  const glm::vec3 right{glm::normalize(glm::cross(front, K_WORLD_UP))};

  glm::vec3 direction{(front * input.get_action_value(Action::k_move_forward)) +
                      (right * input.get_action_value(Action::k_move_right)) +
                      (K_WORLD_UP * input.get_action_value(Action::k_move_up))};
  // Keep diagonal movement at the same speed as the individual axes.
  if (glm::length(direction) > 1.0F) {
    direction = glm::normalize(direction);
  }

  const glm::vec3 position{glm::make_vec3(m_camera.get_position().data())};
  const glm::vec3 new_position{position + (direction * (m_move_speed * delta_time))};

  // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
  m_camera.set_position(new_position.x, new_position.y, new_position.z);
  // NOLINTEND(cppcoreguidelines-pro-type-union-access)
}

void CameraController::set_move_speed(const float units_per_second) {
  m_move_speed = units_per_second;
}

void CameraController::set_look_sensitivity(const float degrees_per_pixel) {
  m_look_sensitivity = degrees_per_pixel;
}

float CameraController::get_move_speed() const {
  return m_move_speed;
}

float CameraController::get_look_sensitivity() const {
  return m_look_sensitivity;
}

// NOLINTEND(misc-include-cleaner)

}  // namespace App

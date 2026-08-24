#include "Core/WorldCamera.hpp"

// NOLINTBEGIN(misc-include-cleaner) -- GLM umbrella headers are canonical.
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <numbers>
#include <span>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/RuntimePresentation.hpp"
#include "Core/WorldPresentation.hpp"

namespace App {

namespace {

/// Signed shortest angular displacement from `from` to `to`, in degrees.
///
/// Camera roll is periodic. A transition such as 345 -> 0 must therefore
/// travel +15 degrees rather than -345 degrees.
///
/// std::remainder places the result in [-180, 180], which is exactly the
/// shortest arc required for camera interpolation.
float shortest_angle_delta_degrees(const float from, const float to) {
  return std::remainder(to - from, 360.0F);
}

}  // namespace

void WorldCameraSystem::set_aspect_ratio(const float aspect_ratio) {
  if (aspect_ratio > 0.0F) {
    m_camera.set_aspect_ratio(aspect_ratio);
  }
}

void WorldCameraSystem::reset() {
  m_current = WorldCameraPose{};
  m_transition_start = WorldCameraPose{};
  m_transition_target = WorldCameraPose{};
  m_transition_elapsed = 0.0F;
  m_transition_duration = 0.0F;
  m_runtime_view = Runtime::CameraView{};
  m_has_pose = false;
  m_has_scripted_pose = false;
  m_active_camera_id.reset();
  m_last_command.reset();
}

void WorldCameraSystem::set_fallback_pose(const std::array<float, 3>& center, const float radius) {
  if (m_has_scripted_pose) {
    return;
  }

  const float safe_radius{std::max(radius, 1.0F)};
  const float distance{std::max(safe_radius * 1.5F, 6.0F)};
  const float height{std::max(safe_radius * 0.20F, 1.0F)};
  m_current = WorldCameraPose{
      .eye = {.x = center.at(0), .y = center.at(1) - height, .z = center.at(2) - distance},
      .target = {.x = center.at(0), .y = center.at(1), .z = center.at(2)}};
  m_has_pose = true;
  m_transition_duration = 0.0F;
  m_transition_elapsed = 0.0F;
  commit_pose();
}

void WorldCameraSystem::apply_command(const WorldCameraCommand& command) {
  APP_PROFILE_FUNCTION();

  const WorldCameraPose requested{.eye = command.runtime_eye,
      .target = command.runtime_target,
      .roll_degrees = static_cast<float>(command.roll_degrees),
      .horizontal_fov_degrees = static_cast<float>(command.horizontal_fov_degrees)};

  m_last_command = command;
  m_active_camera_id = command.camera_id;
  m_has_scripted_pose = true;

  const float duration_seconds{
      std::abs(static_cast<float>(command.duration_units)) / k_scenario_frames_per_second};

  if (!m_has_pose || duration_seconds <= 0.0F) {
    m_current = requested;
    m_transition_start = requested;
    m_transition_target = requested;
    m_transition_elapsed = 0.0F;
    m_transition_duration = 0.0F;
    m_has_pose = true;
    commit_pose();
    return;
  }

  // Starting from m_current rather than the previous command's destination
  // makes a new camera command continuous even when it interrupts an active
  // transition.
  m_transition_start = m_current;
  m_transition_target = requested;
  m_transition_elapsed = 0.0F;
  m_transition_duration = duration_seconds;
}

void WorldCameraSystem::update(const float delta_seconds) {
  APP_PROFILE_FUNCTION();

  if (!m_has_pose || m_transition_duration <= 0.0F) {
    return;
  }

  m_transition_elapsed += std::max(delta_seconds, 0.0F);
  const float linear_amount{std::clamp(m_transition_elapsed / m_transition_duration, 0.0F, 1.0F)};

  // Runtime's camera transition is quadratic ease-in/ease-out. AREA timing
  // remains in the original 30 Hz duration units, but this curve is sampled
  // every display frame so high-refresh presentation stays smooth.
  float eased_amount{0.0F};
  if (linear_amount < 0.5F) {
    eased_amount = 2.0F * linear_amount * linear_amount;
  } else {
    const float remaining{1.0F - linear_amount};
    eased_amount = 1.0F - (2.0F * remaining * remaining);
  }

  m_current = interpolate(m_transition_start, m_transition_target, eased_amount);
  commit_pose();

  if (linear_amount >= 1.0F) {
    m_current = m_transition_target;
    m_transition_elapsed = 0.0F;
    m_transition_duration = 0.0F;
    commit_pose();
  }
}

WorldCameraPose WorldCameraSystem::interpolate(
    const WorldCameraPose& from, const WorldCameraPose& to, const float amount) {
  WorldCameraPose result;
  result.eye = Runtime::Vec3{.x = from.eye.x + ((to.eye.x - from.eye.x) * amount),
      .y = from.eye.y + ((to.eye.y - from.eye.y) * amount),
      .z = from.eye.z + ((to.eye.z - from.eye.z) * amount)};
  result.target = Runtime::Vec3{.x = from.target.x + ((to.target.x - from.target.x) * amount),
      .y = from.target.y + ((to.target.y - from.target.y) * amount),
      .z = from.target.z + ((to.target.z - from.target.z) * amount)};
  result.roll_degrees =
      from.roll_degrees +
      (shortest_angle_delta_degrees(from.roll_degrees, to.roll_degrees) * amount);
  result.horizontal_fov_degrees =
      from.horizontal_fov_degrees +
      ((to.horizontal_fov_degrees - from.horizontal_fov_degrees) * amount);
  return result;
}

void WorldCameraSystem::commit_pose() {
  constexpr float k_degrees_to_radians{std::numbers::pi_v<float> / 180.0F};
  m_runtime_view = Runtime::camera_view(
      m_current.eye, m_current.target, m_current.roll_degrees * k_degrees_to_radians);
  const glm::mat4 gl_view{Runtime::Presentation::to_gl(m_runtime_view.world_to_camera)};
  const Runtime::Vec3 gl_eye{Runtime::Presentation::to_gl(m_current.eye)};
  const std::array<float, 3> eye{gl_eye.x, gl_eye.y, gl_eye.z};
  m_camera.set_view_matrix(std::span<const float, 16>{glm::value_ptr(gl_view), 16}, eye);

  if (m_current.horizontal_fov_degrees > 0.0F) {
    m_camera.set_perspective(
        Runtime::horizontal_4_3_to_vertical_fov(m_current.horizontal_fov_degrees),
        Runtime::k_default_near_inches,
        Runtime::metres_to_inches(Runtime::k_default_clip_distance_metres));
  }
}

}  // namespace App

// NOLINTEND(misc-include-cleaner)

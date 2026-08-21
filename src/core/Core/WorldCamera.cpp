#include "Core/WorldCamera.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/WorldPresentation.hpp"

namespace App {

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
  m_has_pose = false;
  m_has_scripted_pose = false;
  m_active_camera_id.reset();
  m_last_command.reset();
}

std::array<float, 3> WorldCameraSystem::runtime_to_renderer(
    const std::array<std::int32_t, 3>& value) {
  return {static_cast<float>(value.at(0)) * k_runtime_units_to_world,
      -static_cast<float>(value.at(1)) * k_runtime_units_to_world,
      -static_cast<float>(value.at(2)) * k_runtime_units_to_world};
}

void WorldCameraSystem::set_fallback_pose(const std::array<float, 3>& center, const float radius) {
  if (m_has_scripted_pose) {
    return;
  }

  const float safe_radius{std::max(radius, 1.0F)};
  const float distance{std::max(safe_radius * 1.5F, 6.0F)};
  const float height{std::max(safe_radius * 0.20F, 1.0F)};
  m_current = WorldCameraPose{
      .eye = {center.at(0), center.at(1) + height, center.at(2) + distance}, .target = center};
  m_has_pose = true;
  m_transition_duration = 0.0F;
  m_transition_elapsed = 0.0F;
  commit_pose();
}

void WorldCameraSystem::apply_command(const WorldCameraCommand& command) {
  APP_PROFILE_FUNCTION();

  const WorldCameraPose requested{.eye = runtime_to_renderer(command.runtime_eye),
      .target = runtime_to_renderer(command.runtime_target)};

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
  const float linear_amount{
      std::clamp(m_transition_elapsed / m_transition_duration, 0.0F, 1.0F)};

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
  for (std::size_t axis{0}; axis < 3U; ++axis) {
    result.eye.at(axis) = from.eye.at(axis) + ((to.eye.at(axis) - from.eye.at(axis)) * amount);
    result.target.at(axis) =
        from.target.at(axis) + ((to.target.at(axis) - from.target.at(axis)) * amount);
  }
  return result;
}

void WorldCameraSystem::commit_pose() {
  m_camera.set_position(m_current.eye.at(0), m_current.eye.at(1), m_current.eye.at(2));
  m_camera.look_at(m_current.target.at(0), m_current.target.at(1), m_current.target.at(2));
}

}  // namespace App
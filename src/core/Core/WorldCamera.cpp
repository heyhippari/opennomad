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

void WorldCameraSystem::set_attachment_pose_provider(AttachmentPoseProvider provider) {
  m_attachment_pose_provider = std::move(provider);
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
  m_active_controller_mode.reset();
  m_controller_transition_elapsed = 0.0F;
  m_controller_transition_duration = 0.0F;
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

  if (command.kind == WorldCameraCommandKind::k_controller_mode) {
    apply_controller_mode(command);
    return;
  }

  const WorldCameraPose requested{resolve_command_pose(command)};

  m_last_command = command;
  m_active_camera_id = command.camera_id;
  m_active_controller_mode = command.camera_type;
  m_controller_transition_elapsed = 0.0F;
  m_controller_transition_duration = 0.0F;
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

void WorldCameraSystem::apply_controller_mode(const WorldCameraCommand& command) {
  m_active_controller_mode = command.controller_mode;
  m_controller_transition_elapsed = 0.0F;
  m_controller_transition_duration =
      std::max(static_cast<float>(command.duration_units), 0.0F) / k_scenario_frames_per_second;
  m_has_scripted_pose = true;

  // Native mode 13 is not a frozen camera. Runtime's controller update
  // (0x00417D10) continuously copies eye/target/roll/FOV from the live camera
  // source at 0x009103D4 while that source exists. OpenNomad does not model
  // that source object yet, so recording the controller mode must be
  // non-destructive: retain the currently evaluated IAM camera and any active
  // interpolation rather than discarding the best pose information we have.
}

void WorldCameraSystem::update(const float delta_seconds) {
  APP_PROFILE_FUNCTION();

  if (!m_has_pose) {
    return;
  }

  if (controller_transitioning()) {
    m_controller_transition_elapsed = std::min(m_controller_transition_duration,
        m_controller_transition_elapsed + std::max(delta_seconds, 0.0F));
  }

  if (m_last_command.has_value()) {
    m_transition_target = resolve_command_pose(m_last_command.value());
    if (m_transition_duration <= 0.0F) {
      m_current = m_transition_target;
      commit_pose();
      return;
    }
  }

  if (m_transition_duration <= 0.0F) {
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

WorldCameraPose WorldCameraSystem::resolve_command_pose(const WorldCameraCommand& command) const {
  return WorldCameraPose{
      .eye = resolve_attachment_point(
          command.serialized_eye, command.eye_attachment_selector, command.runtime_eye),
      .target = resolve_attachment_point(
          command.serialized_target, command.target_attachment_selector, command.runtime_target),
      .roll_degrees = static_cast<float>(command.roll_degrees),
      .horizontal_fov_degrees = static_cast<float>(command.horizontal_fov_degrees)};
}

Runtime::Vec3 WorldCameraSystem::resolve_attachment_point(
    const std::array<std::int32_t, 3>& serialized,
    const std::int16_t selector,
    const Runtime::Vec3& absolute_fallback) const {
  if (selector == -1) {
    return absolute_fallback;
  }
  if ((selector != 0 && selector != 1) || !m_attachment_pose_provider) {
    return absolute_fallback;
  }
  const std::optional<WorldCameraAttachmentPose> attachment{m_attachment_pose_provider()};
  if (!attachment.has_value()) {
    return absolute_fallback;
  }
  const Runtime::Vec3 relative{Runtime::iam_camera_vector_to_runtime(serialized)};
  const Runtime::Matrix3& orientation{selector == 0 ? attachment->body_offset_orientation
                                                    : attachment->effective_orientation};
  const Runtime::Vec3 rotated{Runtime::transform_vector(relative, orientation)};
  return Runtime::Vec3{.x = attachment->translation.x - rotated.x,
      .y = attachment->translation.y - rotated.y,
      .z = attachment->translation.z - rotated.z};
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
  result.roll_degrees = from.roll_degrees +
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

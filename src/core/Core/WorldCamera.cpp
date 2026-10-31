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

void WorldCameraSystem::set_clip_distance_metres(const float distance) {
  m_clip_distance_metres = std::max(distance, 0.0F);
  m_camera.set_perspective(m_camera.get_vertical_fov_degrees(),
      Runtime::k_default_near_inches,
      Runtime::metres_to_inches(m_clip_distance_metres));
}

void WorldCameraSystem::set_attachment_pose_provider(AttachmentPoseProvider provider) {
  m_attachment_pose_provider = std::move(provider);
}

void WorldCameraSystem::set_controller_pose_provider(ControllerPoseProvider provider) {
  m_controller_pose_provider = std::move(provider);
}

void WorldCameraSystem::set_segment_query_provider(SegmentQueryProvider provider) {
  m_segment_query_provider = std::move(provider);
}

WorldCameraCommand WorldCameraSystem::player_camera_command(const std::uint32_t scene_id,
    const std::uint32_t scene_generation,
    const WorldCameraAttachmentParticipants& participants) {
  return WorldCameraCommand{.scene_id = scene_id,
      .scene_generation = scene_generation,
      .attachment_participants = participants,
      // The preset table holds Runtime float inches; there is no serialized
      // AREA vector for this camera.
      .runtime_eye = K_PLAYER_CAMERA_EYE_OFFSET,
      .camera_type = 0U,
      .horizontal_fov_degrees = K_PLAYER_CAMERA_FOV_DEGREES,
      .target_attachment_selector = 0,
      .eye_attachment_selector = 0,
      .tail_fields = K_PLAYER_CAMERA_EASING_FRAMES};
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
  m_active_operation.reset();
  m_completed_operation.reset();
  m_last_command.reset();
  m_follow = WorldCameraFollow{};
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

  const WorldCameraPose requested{start_follow(command)};

  m_last_command = command;
  m_active_camera_id = command.camera_id;
  m_active_controller_mode = command.camera_type;
  m_controller_transition_elapsed = 0.0F;
  m_controller_transition_duration = 0.0F;
  m_active_operation.reset();
  if (command.camera_type == 12U && command.wait_for_completion &&
      command.operation_generation.has_value()) {
    m_active_operation =
        WorldCameraOperationCompletion{.operation_generation = command.operation_generation.value(),
            .scene_id = command.scene_id,
            .scene_generation = command.scene_generation,
            .source_area_id = command.source_area_id,
            .camera_id = command.camera_id};
  }
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
    complete_active_operation();
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

void WorldCameraSystem::release_structured_controller() {
  if (m_active_controller_mode != 13U) {
    return;
  }
  m_active_controller_mode = 0U;
  m_controller_transition_elapsed = 0.0F;
  m_controller_transition_duration = 0.0F;
  m_active_camera_id.reset();
  m_last_command.reset();
  m_transition_elapsed = 0.0F;
  m_transition_duration = 0.0F;
  // The last evaluated pose stays as a presentation fallback; the scripted
  // source claim is released.
  m_has_scripted_pose = false;
}

void WorldCameraSystem::apply_controller_mode(const WorldCameraCommand& command) {
  m_active_controller_mode = command.controller_mode;
  m_controller_transition_elapsed = 0.0F;
  m_controller_transition_duration =
      std::max(static_cast<float>(command.duration_units), 0.0F) / k_scenario_frames_per_second;
  m_has_scripted_pose = true;

  // Mode 13 owns the evaluated pose from this point onward. Keep the current
  // pose as the native null-source fallback, but do not allow an interrupted
  // AREA interpolation to resume underneath the live structured-camera source.
  if (command.controller_mode == 13U) {
    m_transition_elapsed = 0.0F;
    m_transition_duration = 0.0F;
    m_last_command.reset();
  }
}

void WorldCameraSystem::update(const float delta_seconds) {
  APP_PROFILE_FUNCTION();

  if (controller_transitioning()) {
    m_controller_transition_elapsed = std::min(m_controller_transition_duration,
        m_controller_transition_elapsed + std::max(delta_seconds, 0.0F));
  }

  // Runtime controller 13 (0x00417D10) reads the source pointer at
  // 0x009103D4 and copies source +0x14..+0x30 into the active camera every
  // update. A null source does nothing: the last evaluated camera is retained.
  if (m_active_controller_mode.has_value() && m_active_controller_mode.value() == 13U) {
    if (m_controller_pose_provider) {
      const std::optional<WorldCameraPose> source{m_controller_pose_provider()};
      if (source.has_value()) {
        m_current = source.value();
        m_transition_start = m_current;
        m_transition_target = m_current;
        m_transition_elapsed = 0.0F;
        m_transition_duration = 0.0F;
        m_has_pose = true;
        m_has_scripted_pose = true;
        // A named 3DO camera, not a numeric IAM AREA/SCENE camera, now owns
        // the presentation pose.
        m_active_camera_id.reset();
        commit_pose();
      }
    }
    return;
  }

  if (!m_has_pose) {
    return;
  }

  if (m_last_command.has_value()) {
    m_transition_target = advance_follow(m_last_command.value(), delta_seconds);
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
    complete_active_operation();
  }
}

WorldCameraPose WorldCameraSystem::start_follow(const WorldCameraCommand& command) {
  m_follow.reset(command.runtime_eye,
      command.runtime_target,
      WorldCameraFollowSettings{.target_offset = command.runtime_target,
          .eye_offset = command.runtime_eye,
          .target_position_frames = static_cast<std::int16_t>(command.tail_fields.at(0)),
          .eye_position_frames = static_cast<std::int16_t>(command.tail_fields.at(1)),
          .eye_angle_frames = static_cast<std::int16_t>(command.tail_fields.at(2))});

  // Runtime 0x00414520: automatic mode 0, and record modes 12/20 whose eye is
  // attached by selector 0, take the adventure follow setup (0x00413C00)
  // unless the player is in fight states 11, 13 or 14 (0x00413CD0).
  const bool follow_mode{
      command.camera_type == 0U || command.camera_type == 12U || command.camera_type == 20U};
  const std::optional<Character::BodyIdentity> eye_actor{
      command.attachment_participants.participant_a_body_identity};
  if (follow_mode && command.eye_attachment_selector == 0 && eye_actor.has_value() &&
      m_attachment_pose_provider) {
    const std::optional<WorldCameraAttachmentPose> actor{
        m_attachment_pose_provider(eye_actor.value())};
    if (actor.has_value() && actor->adventure_mode != 11 && actor->adventure_mode != 13 &&
        actor->adventure_mode != 14) {
      m_follow.enable_adventure(actor->translation.y, actor->body_bottom);
    }
  }
  return advance_follow(command, 0.0F);
}

WorldCameraPose WorldCameraSystem::advance_follow(
    const WorldCameraCommand& command, const float delta_seconds) {
  m_follow.update(WorldCameraFollowInput{.target = resolve_endpoint_source(
                                             command, command.target_attachment_selector),
                      .eye = resolve_endpoint_source(command, command.eye_attachment_selector),
                      .delta_units = std::max(delta_seconds, 0.0F) * k_scenario_frames_per_second},
      m_segment_query_provider);
  return WorldCameraPose{.eye = m_follow.eye(),
      .target = m_follow.target(),
      .roll_degrees = static_cast<float>(command.roll_degrees),
      .horizontal_fov_degrees = static_cast<float>(command.horizontal_fov_degrees)};
}

std::optional<WorldCameraEndpointSource> WorldCameraSystem::resolve_endpoint_source(
    const WorldCameraCommand& command, const std::int16_t selector) const {
  if (selector == -1 || !m_attachment_pose_provider) {
    return std::nullopt;
  }
  if (selector != 0 && selector != 6) {
    // Selectors 1..5 and 7..9 have distinct native resolvers whose transform
    // semantics remain unrecovered; the endpoint keeps its absolute value.
    return std::nullopt;
  }

  const std::optional<Character::BodyIdentity> participant_a_identity{
      command.attachment_participants.participant_a_body_identity};
  if (!participant_a_identity.has_value()) {
    return std::nullopt;
  }
  const std::optional<WorldCameraAttachmentPose> participant_a{
      m_attachment_pose_provider(participant_a_identity.value())};
  if (!participant_a.has_value()) {
    return std::nullopt;
  }

  if (selector == 0) {
    return WorldCameraEndpointSource{.position = participant_a->translation,
        .angles_degrees = participant_a->principal_orientation_degrees};
  }

  // Runtime selector 6 derives one shared orientation solely from the live
  // A/B relationship. It does not compose either actor's orientation.
  const std::optional<Character::BodyIdentity> participant_b_identity{
      command.attachment_participants.participant_b_body_identity};
  if (!participant_b_identity.has_value()) {
    return std::nullopt;
  }
  const std::optional<WorldCameraAttachmentPose> participant_b{
      m_attachment_pose_provider(participant_b_identity.value())};
  if (!participant_b.has_value()) {
    return std::nullopt;
  }

  const Runtime::Vec3 delta{.x = participant_a->translation.x - participant_b->translation.x,
      .y = participant_a->translation.y - participant_b->translation.y,
      .z = participant_a->translation.z - participant_b->translation.z};
  const float length{std::sqrt((delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z))};
  constexpr float k_minimum_participant_separation_inches{0.0001F};
  if (!std::isfinite(length) || length <= k_minimum_participant_separation_inches) {
    return std::nullopt;
  }

  constexpr float k_radians_to_degrees{180.0F / std::numbers::pi_v<float>};
  const float yaw_degrees{
      (std::atan2(delta.z / length, delta.x / length) * k_radians_to_degrees) + 90.0F};
  return WorldCameraEndpointSource{
      .position = {.x = (participant_a->translation.x + participant_b->translation.x) * 0.5F,
          .y = (participant_a->translation.y + participant_b->translation.y) * 0.5F,
          .z = (participant_a->translation.z + participant_b->translation.z) * 0.5F},
      .angles_degrees = {.x = 0.0F, .y = yaw_degrees, .z = 0.0F}};
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

void WorldCameraSystem::complete_active_operation() {
  if (!m_active_operation.has_value()) {
    return;
  }
  m_completed_operation = m_active_operation;
  m_active_operation.reset();
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
        Runtime::metres_to_inches(m_clip_distance_metres));
  }
}

}  // namespace App

// NOLINTEND(misc-include-cleaner)

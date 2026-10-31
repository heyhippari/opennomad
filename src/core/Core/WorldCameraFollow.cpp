#include "Core/WorldCameraFollow.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "Core/RuntimeMath.hpp"

namespace App {

namespace {

/// Runtime 0x004BC134: per-axis angle difference treated as settled.
constexpr float K_ANGLE_SETTLE_DEGREES{0.1F};

[[nodiscard]] Runtime::Vec3 add(const Runtime::Vec3& first, const Runtime::Vec3& second) {
  return {.x = first.x + second.x, .y = first.y + second.y, .z = first.z + second.z};
}

[[nodiscard]] Runtime::Vec3 subtract(const Runtime::Vec3& first, const Runtime::Vec3& second) {
  return {.x = first.x - second.x, .y = first.y - second.y, .z = first.z - second.z};
}

[[nodiscard]] Runtime::Vec3 scale(const Runtime::Vec3& vector, const float amount) {
  return {.x = vector.x * amount, .y = vector.y * amount, .z = vector.z * amount};
}

[[nodiscard]] float length(const Runtime::Vec3& vector) {
  return std::sqrt((vector.x * vector.x) + (vector.y * vector.y) + (vector.z * vector.z));
}

/// `origin + direction * (distance / |direction|)`; a degenerate direction
/// leaves the origin, where Runtime would divide by zero.
[[nodiscard]] Runtime::Vec3 along(
    const Runtime::Vec3& origin, const Runtime::Vec3& direction, const float distance) {
  const float magnitude{length(direction)};
  if (!(magnitude > 0.0F)) {
    return origin;
  }
  return add(origin, scale(direction, distance / magnitude));
}

/// Runtime easing frames: an authored 1 behaves like 2, and the snap flag
/// forces an immediate assignment.
[[nodiscard]] float easing_frames(const std::int16_t authored, const bool snap) {
  if (snap) {
    return 0.0F;
  }
  return authored == 1 ? 2.0F : static_cast<float>(authored);
}

/// `current + (desired - current) * delta / frames`, or the desired value
/// when no easing is requested.
[[nodiscard]] float ease(
    const float current, const float desired, const float delta_units, const float frames) {
  if (frames <= 0.0F) {
    return desired;
  }
  return current + ((desired - current) * delta_units / frames);
}

[[nodiscard]] Runtime::Vec3 ease(const Runtime::Vec3& current,
    const Runtime::Vec3& desired,
    const float delta_units,
    const float frames) {
  return {.x = ease(current.x, desired.x, delta_units, frames),
      .y = ease(current.y, desired.y, delta_units, frames),
      .z = ease(current.z, desired.z, delta_units, frames)};
}

/// Wraps one angle difference into [-180, 180] like 0x00415E60's loops.
[[nodiscard]] float wrap_degrees(float degrees) {
  while (degrees < -180.0F) {
    degrees += 360.0F;
  }
  while (degrees > 180.0F) {
    degrees -= 360.0F;
  }
  return degrees;
}

[[nodiscard]] float ease_angle(
    const float current, const float desired, const float delta_units, const float frames) {
  if (frames <= 0.0F) {
    return desired;
  }
  const float delta{wrap_degrees(desired - current)};
  if (std::abs(delta) <= K_ANGLE_SETTLE_DEGREES) {
    return desired;
  }
  return current + (delta * delta_units / frames);
}

}  // namespace

Runtime::Vec3 WorldCameraFollow::attached_point(
    const Runtime::Vec3& source, const Runtime::Vec3& angles_degrees, const Runtime::Vec3& offset) {
  return subtract(source,
      Runtime::transform_vector(offset, Runtime::camera_euler_rotation_degrees(angles_degrees)));
}

void WorldCameraFollow::reset(const Runtime::Vec3& eye,
    const Runtime::Vec3& target,
    const WorldCameraFollowSettings& settings) {
  *this = WorldCameraFollow{};
  m_settings = settings;
  m_eye = eye;
  m_target = target;
  m_eye_out = eye;
  m_target_out = target;
}

void WorldCameraFollow::enable_adventure(const float eye_actor_y, const float body_bottom) {
  m_adventure = true;
  m_head_offset = body_bottom * K_HEAD_HEIGHT_SCALE;
  m_actor_y = eye_actor_y;
  m_previous_actor_y = eye_actor_y;
}

void WorldCameraFollow::update(
    const WorldCameraFollowInput& input, const SegmentQuery& segment_query) {
  const float delta_units{std::isfinite(input.delta_units)
                              ? std::clamp(input.delta_units, 0.0F, K_MAXIMUM_DELTA_UNITS)
                              : 0.0F};
  m_eye_out = m_eye;
  m_target_out = m_target;

  if (input.target.has_value()) {
    follow_target(input.target.value(), delta_units);
  }
  if (input.eye.has_value()) {
    follow_eye(input.eye.value(), delta_units);
  }

  if (m_adventure) {
    m_previous_actor_y = m_actor_y;
    if (m_eye_source.has_value()) {
      m_actor_y = m_eye_source->position.y;
    }
    // Flag 0x10 (0x00416450) is also set here, but its trigger compares the
    // actor Y with the copy taken just above and can never fire.
    resolve_occlusion(delta_units, segment_query);
  }

  m_eye = m_eye_out;
  m_target = m_target_out;
  m_snap = false;
}

void WorldCameraFollow::follow_target(
    const WorldCameraEndpointSource& source, const float delta_units) {
  // Runtime 0x00415D10: target sources are not angle-eased.
  m_target_source = source;
  const Runtime::Vec3 desired{
      attached_point(source.position, source.angles_degrees, m_settings.target_offset)};
  m_target_out = ease(
      m_target_out, desired, delta_units, easing_frames(m_settings.target_position_frames, m_snap));
}

void WorldCameraFollow::follow_eye(
    const WorldCameraEndpointSource& source, const float delta_units) {
  // Runtime 0x00415E60: ease the source angles first, then the position of
  // the point those eased angles produce.
  m_eye_source = source;
  const float angle_frames{easing_frames(m_settings.eye_angle_frames, m_snap)};
  m_eye_angles = Runtime::Vec3{
      .x = ease_angle(m_eye_angles.x, source.angles_degrees.x, delta_units, angle_frames),
      .y = ease_angle(m_eye_angles.y, source.angles_degrees.y, delta_units, angle_frames),
      .z = ease_angle(m_eye_angles.z, source.angles_degrees.z, delta_units, angle_frames)};
  const Runtime::Vec3 desired{attached_point(source.position, m_eye_angles, m_settings.eye_offset)};
  m_eye_out =
      ease(m_eye_out, desired, delta_units, easing_frames(m_settings.eye_position_frames, m_snap));
}

void WorldCameraFollow::resolve_occlusion(
    const float delta_units, const SegmentQuery& segment_query) {
  // Runtime 0x00417070.
  const Runtime::Vec3 view{subtract(m_eye_out, m_target_out)};
  const float view_length{length(view)};
  const auto probe = [&segment_query](const Runtime::Vec3& from, const Runtime::Vec3& to) {
    return segment_query ? segment_query(from, to) : std::nullopt;
  };

  float held_distance{0.0F};
  const std::optional<Runtime::Vec3> hit{
      probe(m_target_out, add(m_target_out, scale(view, K_RAY_SCALE)))};
  if (hit.has_value()) {
    // Occluded: pull in instantly, ease back out over eight frames.
    if (m_occlusion != Occlusion::k_pulled_in) {
      if (m_occlusion == Occlusion::k_clear) {
        m_occluded_distance = view_length;
      }
      m_occlusion = Occlusion::k_pulled_in;
    }
    const Runtime::Vec3 to_hit{subtract(hit.value(), m_target_out)};
    float distance{length(to_hit) / K_RAY_SCALE};
    if (m_occluded_distance < distance && !m_snap) {
      distance = ease(m_occluded_distance, distance, delta_units, K_DISTANCE_FRAMES);
    }
    m_occluded_distance = distance;
    held_distance = distance;
    m_eye_out = along(m_target_out, to_hit, distance);
  } else {
    if (m_occlusion == Occlusion::k_pulled_in) {
      // Re-test from the unsmoothed endpoints before releasing the hold.
      const Runtime::Vec3 desired_target{m_target_source.has_value()
                                             ? attached_point(m_target_source->position,
                                                   m_target_source->angles_degrees,
                                                   m_settings.target_offset)
                                             : m_target_out};
      const Runtime::Vec3 desired_eye{
          m_eye_source.has_value()
              ? attached_point(m_eye_source->position, m_eye_angles, m_settings.eye_offset)
              : m_eye_out};
      const Runtime::Vec3 desired_view{subtract(desired_eye, desired_target)};
      if (!probe(desired_target, add(desired_target, scale(desired_view, K_RAY_SCALE)))
              .has_value()) {
        m_occlusion = Occlusion::k_recovering;
      } else {
        // Still blocked at the destination: keep the held distance and carry
        // both endpoints vertically with the actor.
        const float actor_rise{m_actor_y - m_previous_actor_y};
        m_eye_out = along(m_target_out, view, m_occluded_distance);
        m_eye_out.y = m_eye.y + actor_rise;
        m_target_out.y = m_target.y + actor_rise;
      }
    }
    if (m_occlusion != Occlusion::k_recovering) {
      return;
    }
    if (view_length <= m_occluded_distance) {
      m_occlusion = Occlusion::k_clear;
      return;
    }
    m_occluded_distance = ease(m_occluded_distance, view_length, delta_units, K_DISTANCE_FRAMES);
    held_distance = m_occluded_distance;
    m_eye_out = along(m_target_out, view, held_distance);
  }

  // Below half the free distance both endpoints blend toward head height.
  const float half_length{view_length * 0.5F};
  const float free_weight{half_length < held_distance
                              ? (held_distance - half_length) / (view_length - half_length)
                              : 0.0F};
  const float head_y{m_head_offset + m_actor_y};
  const float eye_y{(m_eye_out.y * free_weight) + (head_y * (1.0F - free_weight))};
  const float target_y{(m_target_out.y * free_weight) + (head_y * (1.0F - free_weight))};
  const float height_frames{m_snap ? 0.0F : K_HEIGHT_FRAMES};
  m_eye_out.y = ease(m_eye.y, eye_y, delta_units, height_frames);
  m_target_out.y = ease(m_target.y, target_y, delta_units, height_frames);
}

}  // namespace App

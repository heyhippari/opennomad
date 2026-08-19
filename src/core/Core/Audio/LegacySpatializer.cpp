#include "Core/Audio/LegacySpatializer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "Core/Audio/AudioTypes.hpp"

namespace App::Audio {

float vec_length(const Vec3& vector) {
  // Double accumulation guards against intermediate overflow for large,
  // plausible world coordinates.
  const double dx{static_cast<double>(vector.at(0))};
  const double dy{static_cast<double>(vector.at(1))};
  const double dz{static_cast<double>(vector.at(2))};
  return static_cast<float>(std::sqrt((dx * dx) + (dy * dy) + (dz * dz)));
}

float radial_component(const Vec3& vector, const Vec3& direction) {
  return (vector.at(0) * direction.at(0)) + (vector.at(1) * direction.at(1)) +
         (vector.at(2) * direction.at(2));
}

float attenuation_gain(const float distance,
    const float minimum_distance,
    const float maximum_distance) {
  if (!std::isfinite(distance) || distance < 0.0F) {
    return 1.0F;
  }
  // Degenerate range: treat as full gain at any distance (safe step
  // behaviour rather than dividing by zero).
  if (maximum_distance <= minimum_distance) {
    return distance <= minimum_distance ? 1.0F : 0.0F;
  }
  if (distance <= minimum_distance) {
    return 1.0F;
  }
  if (distance >= maximum_distance) {
    return 0.0F;
  }
  const float gain{
      (maximum_distance - distance) / (maximum_distance - minimum_distance)};
  return std::clamp(gain, 0.0F, 1.0F);
}

float pan_factor(const Vec3& relative, const Vec3& listener_right) {
  const float length{vec_length(relative)};
  if (!std::isfinite(length) || length == 0.0F) {
    return 0.0F;  // Coincident positions: neutral pan, no normalization/NaN.
  }
  const float right_length{vec_length(listener_right)};
  if (!std::isfinite(right_length) || right_length == 0.0F) {
    return 0.0F;
  }
  const Vec3 to_source{
      relative.at(0) / length, relative.at(1) / length, relative.at(2) / length};
  const Vec3 right_unit{
      listener_right.at(0) / right_length,
      listener_right.at(1) / right_length,
      listener_right.at(2) / right_length};
  const float dot{radial_component(to_source, right_unit)};
  if (!std::isfinite(dot)) {
    return 0.0F;
  }
  return std::clamp(dot, -1.0F, 1.0F);
}

std::pair<float, float> constant_power_stereo_gains(const float pan) {
  const float clamped{std::clamp(pan, -1.0F, 1.0F)};
  const float angle{
      (clamped + 1.0F) * std::numbers::pi_v<float> * 0.25F};
  return {std::cos(angle), std::sin(angle)};
}

float doppler_frequency_ratio(const float listener_radial_velocity,
    const float source_radial_velocity,
    const float speed_of_sound) {
  const float speed{std::abs(speed_of_sound) > 1.0e-3F ? speed_of_sound
                                                       : k_doppler_speed_of_sound};
  // Clamp the denominator away from zero; preserve its sign.
  float denominator{speed - source_radial_velocity};
  if (std::abs(denominator) < 1.0e-3F) {
    denominator = denominator >= 0.0F ? 1.0e-3F : -1.0e-3F;
  }
  const float ratio{(speed - listener_radial_velocity) / denominator};
  if (!std::isfinite(ratio)) {
    return 1.0F;
  }
  return std::clamp(ratio, k_min_frequency_ratio, k_max_frequency_ratio);
}

SpatialResult spatialize(const AudioListenerState& listener,
    const SoundEmitterState& emitter,
    const float previous_distance,
    const float real_delta_seconds) {
  SpatialResult result;

  const Vec3 relative{
      emitter.position.at(0) - listener.position.at(0),
      emitter.position.at(1) - listener.position.at(1),
      emitter.position.at(2) - listener.position.at(2)};
  const float distance{vec_length(relative)};
  result.distance = distance;

  // Derive the listener right vector defensively: right = normalize(cross(forward, up)),
  // with a stable fallback basis for degenerate transforms.
  const Vec3 forward{listener.forward.at(0), listener.forward.at(1), listener.forward.at(2)};
  const Vec3 up{listener.up.at(0), listener.up.at(1), listener.up.at(2)};
  Vec3 right{
      (forward.at(1) * up.at(2)) - (forward.at(2) * up.at(1)),
      (forward.at(2) * up.at(0)) - (forward.at(0) * up.at(2)),
      (forward.at(0) * up.at(1)) - (forward.at(1) * up.at(0))};
  const float right_length{vec_length(right)};
  if (!std::isfinite(right_length) || right_length < 1.0e-6F) {
    right = Vec3{1.0F, 0.0F, 0.0F};
  } else {
    right = Vec3{right.at(0) / right_length, right.at(1) / right_length,
        right.at(2) / right_length};
  }

  result.attenuation_gain =
      attenuation_gain(distance, emitter.minimum_distance, emitter.maximum_distance);
  result.pan = pan_factor(relative, right);
  const auto [left, right_gain]{constant_power_stereo_gains(result.pan)};
  result.left_gain = left;
  result.right_gain = right_gain;

  // Doppler: radial velocities measured along the direction from SOURCE to
  // LISTENER (a positive radial velocity means "moving toward the other
  // party"). An approaching source therefore raises pitch (ratio > 1) and a
  // receding source lowers it (ratio < 1).
  float listener_radial{0.0F};
  float source_radial{0.0F};
  if (distance > 1.0e-6F && std::isfinite(distance)) {
    // `direction` points listener -> source; `toward` points source -> listener.
    const Vec3 toward{-relative.at(0) / distance, -relative.at(1) / distance,
        -relative.at(2) / distance};
    listener_radial = radial_component(listener.velocity, toward);
    source_radial = radial_component(emitter.velocity, toward);
  }
  result.frequency_ratio =
      doppler_frequency_ratio(listener_radial, source_radial, k_doppler_speed_of_sound);

  // Guard against degenerate frames: a paused, zero, negative, huge or
  // nonfinite delta must not produce a Doppler spike. Doppler above uses the
  // per-second velocities directly; this guard is a defensive parity check
  // and a diagnostic anchor (previous_distance is retained but not yet used
  // by a frame-based distance-delta Doppler).
  static_cast<void>(previous_distance);
  if (!std::isfinite(real_delta_seconds) || real_delta_seconds <= 0.0F) {
    result.frequency_ratio = 1.0F;
  }

  return result;
}

}  // namespace App::Audio

#pragma once

#include <utility>

#include "Core/Audio/AudioTypes.hpp"

namespace App::Audio {

/// Pure, deterministic compatibility spatializer (no SDL calls). Produces
/// derived values that the audio subsystem applies through track gain, forced
/// stereo gains and frequency ratio. All curves and constants are isolated
/// here so they can be replaced as reverse engineering progresses.

// ─────────────────────────────────────────────────────────────────────────────
// Confirmed binary facts
// ─────────────────────────────────────────────────────────────────────────────

/// Effective software-Doppler speed in OpenNomad's metre-based audio boundary,
/// observed near 429.0 in the original binary. Its recovered physical meaning
/// remains provisional; the value is isolated for correction.
inline constexpr float k_doppler_speed_of_sound{429.0F};

// ─────────────────────────────────────────────────────────────────────────────
// Behaviour inferred from the DirectSound/software fallback
// ─────────────────────────────────────────────────────────────────────────────

/// Compatibility distance-attenuation curve: full gain inside the minimum
/// distance, zero gain at/outside the maximum distance, linear falloff
/// between. Numerical parity with the original DirectSound path is
/// provisional and deliberately not labelled exact.
[[nodiscard]] float attenuation_gain(
    float distance, float minimum_distance, float maximum_distance);

// ─────────────────────────────────────────────────────────────────────────────
// Deliberately provisional modern approximations
// ─────────────────────────────────────────────────────────────────────────────

/// Pan in [-1, 1] from the normalized source direction dotted with the
/// listener's right vector. Coincident positions produce neutral pan (0).
[[nodiscard]] float pan_factor(const Vec3& relative, const Vec3& listener_right);

/// Constant-power stereo law for a pan value in [-1, 1]. Returns {left, right}.
[[nodiscard]] std::pair<float, float> constant_power_stereo_gains(float pan);

/// Software-Doppler frequency ratio from radial listener/source velocities
/// along the source direction. A positive radial velocity means "moving away
/// from the other party". An approaching source raises pitch (ratio > 1); a
/// receding source lowers it (ratio < 1). Denominators are clamped away from
/// zero, nonfinite results are rejected, and the result is clamped to the
/// SDL3_mixer frequency-ratio range.
[[nodiscard]] float doppler_frequency_ratio(float listener_radial_velocity,
    float source_radial_velocity,
    float speed_of_sound = k_doppler_speed_of_sound);

/// Minimum/maximum accepted SDL3_mixer track frequency ratios.
inline constexpr float k_min_frequency_ratio{0.01F};
inline constexpr float k_max_frequency_ratio{100.0F};

/// Combines attenuation, pan, stereo and Doppler into one SpatialResult.
/// `real_delta_seconds` guards velocity-derived Doppler; a paused/nonfinite
/// frame produces no Doppler spike.
[[nodiscard]] SpatialResult spatialize(const AudioListenerState& listener,
    const SoundEmitterState& emitter,
    float previous_distance,
    float real_delta_seconds);

/// Radial component of `vector` along `direction` (direction need not be
/// normalized; the result scales with the direction length). Helper for
/// velocity/Doppler tests and the `spatialize` implementation.
[[nodiscard]] float radial_component(const Vec3& vector, const Vec3& direction);

/// Length of a 3-vector (Euclidean), guarded against overflow via double.
[[nodiscard]] float vec_length(const Vec3& vector);

}  // namespace App::Audio

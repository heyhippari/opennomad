#pragma once

#include <algorithm>
#include <cstdint>

namespace App::Interface {

/// Seconds per authentic Runtime effect update (30 Hz). The recovered effect
/// constants are per update, so this drives when an endpoint advances but is
/// never multiplied into the recovered math.
inline constexpr double k_bump_tick_seconds{1.0 / 30.0};

/// How the animated background is presented.
enum class BumpAnimationMode : std::uint8_t {
  /// Show one authentic 30 Hz state per displayed frame (alpha forced to 0).
  /// The baseline for Runtime comparison and parity debugging.
  k_stepped,
  /// Insert smooth intermediate frames between 30 Hz endpoints. Default.
  k_interpolated,
};

/// The 30 Hz animation timeline: which endpoint is current and how far the
/// presentation has progressed toward the next one.
struct BumpTimelineState {
  /// Tick index of the current endpoint (tick N).
  std::uint64_t current_tick{0};
  /// Fractional progress N -> N+1, in [0, 1).
  float alpha{0.0F};
};

/// Result of feeding host time into the timeline.
struct BumpTimelineAdvance {
  /// Number of authentic endpoint boundaries crossed by this update.
  std::uint64_t ticks_advanced{0};
  BumpTimelineState state;
};

/// Advances the 30 Hz timeline by `elapsed_seconds` of host time.
///
/// `remainder_seconds` carries the fractional part of a tick across frames so
/// the effect completes exactly 30 endpoint intervals per second regardless of
/// host frame rate, refresh rate, or irregular frame times. A long stall (for
/// example 100 ms) crosses the corresponding tick boundaries at once and
/// returns the correct current tick + alpha, without generating any
/// intermediate per-pixel work.
inline BumpTimelineAdvance advance_bump_timeline(BumpTimelineState state,
    double& remainder_seconds,
    const double elapsed_seconds) {
  remainder_seconds += elapsed_seconds;
  const auto ticks{static_cast<std::uint64_t>(remainder_seconds / k_bump_tick_seconds)};
  if (ticks > 0U) {
    state.current_tick += ticks;
    remainder_seconds -= static_cast<double>(ticks) * k_bump_tick_seconds;
  }
  state.alpha = static_cast<float>(
      std::clamp(remainder_seconds / k_bump_tick_seconds, 0.0, 1.0));
  return BumpTimelineAdvance{.ticks_advanced = ticks, .state = state};
}

/// Circular (shortest-path) interpolation of two modulo-256 warp offset bytes.
///
/// Naive linear interpolation is wrong for transitions like 250 -> 4 because
/// `mix(250, 4, 0.5) = 127` sweeps almost all the way around the height field
/// in the wrong direction. This helper instead walks the shorter arc, so the
/// result may temporarily leave [0, 256); callers wrap it when constructing
/// the final source coordinate. The exact ±128 ambiguity resolves by taking
/// the positive (forward) arc for +128 and the negative arc for -128, which is
/// deterministic.
inline float wrapped_lerp_256(const float a, const float b, const float alpha) {
  float delta{b - a};
  if (delta > 128.0F) {
    delta -= 256.0F;
  } else if (delta < -128.0F) {
    delta += 256.0F;
  }
  return a + (delta * alpha);
}

}  // namespace App::Interface

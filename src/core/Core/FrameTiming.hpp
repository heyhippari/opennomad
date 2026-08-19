#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace App::FrameTiming {

/// Time-scale modes recovered from the original `UpdateFrameTiming` switch
/// (see docs/ReverseEngineering.md). Deltas are measured in Omikron units:
/// 1.0 delta unit = 1/30 s = one 30 Hz simulation tick.
enum class TimeScaleMode : std::uint8_t {
  /// Measured: delta = 30 / averageFPS, capped at k_max_dynamic_delta.
  k_dynamic = 0,
  /// Delta = 1.0 (exactly one tick per frame).
  k_fixed_30hz = 1,
  /// Delta = 0.5.
  k_fixed_60hz = 2,
  /// Delta = 0.1.
  k_fixed_300hz = 3,
  /// Delta = 2.0.
  k_fixed_15hz = 4,
};

/// One Omikron delta unit per second (30 Hz simulation ticks).
inline constexpr float k_delta_units_per_second{30.0F};
/// Largest dynamic simulation step: 3.0 units = 100 ms.
inline constexpr float k_max_dynamic_delta{3.0F};

/// All timing state produced by the recovered `UpdateFrameTiming`
/// (see docs/ReverseEngineering.md). Millisecond clocks and durations;
/// Omikron delta units for base/effective delta.
struct FrameTimingState {
  /// Timestamp the currently executing frame started from.
  std::uint64_t frame_start_time_ms{};
  /// Timestamp of the last completed frame measurement.
  std::uint64_t current_time_ms{};
  /// Original secondaryFrameClock. Only the timing reset writes it; kept
  /// for fidelity with the recovered function and future consumers.
  std::uint64_t secondary_frame_clock_ms{};

  /// Duration of the last completed frame; never zero (clamped to 1 ms).
  std::uint64_t frame_time_ms{1};
  /// Moving average: (old + new) / 2 per frame; never zero.
  std::uint64_t average_frame_time_ms{1};

  /// Instantaneous FPS of the last completed frame.
  float current_fps{1000.0F};
  /// FPS of the moving average frame time.
  float average_fps{1000.0F};

  /// Unpaused delta after time scale, synchronization, and forced override.
  float base_delta{1.0F};
  /// Delta the next engine callback consumes; zero while gameplay-paused.
  float effective_delta{1.0F};

  TimeScaleMode time_scale_mode{TimeScaleMode::k_dynamic};
  /// Recovered `g_forcedDeltaTime`: while set, replaces the calculated
  /// delta outright (the original's -1.0 sentinel is represented as
  /// "no value").
  std::optional<float> forced_delta{};
  /// Gameplay pause. Distinct from focus loss, application backgrounding,
  /// update suspension, and skip_engine_frame: the engine callback still
  /// runs, but the newly calculated effective delta becomes zero.
  bool gameplay_paused{false};
};

/// Nominal delta in Omikron units for the current time-scale mode.
///
/// Fixed modes directly replace the measured dynamic delta (they are not
/// multipliers). The original switch had no meaningful default assignment;
/// out-of-range values therefore fall back to the dynamic calculation.
/// Precondition: average_fps > 0 (guaranteed by the 1 ms clamp).
[[nodiscard]] inline float calculate_delta(const TimeScaleMode mode, const float average_fps) {
  if (mode == TimeScaleMode::k_fixed_30hz) {
    return 1.0F;
  }
  if (mode == TimeScaleMode::k_fixed_60hz) {
    return 0.5F;
  }
  if (mode == TimeScaleMode::k_fixed_300hz) {
    return 0.1F;
  }
  if (mode == TimeScaleMode::k_fixed_15hz) {
    return 2.0F;
  }
  return std::min(k_delta_units_per_second / average_fps, k_max_dynamic_delta);
}

/// Executes one timed frame in the recovered `UpdateFrameTiming` order:
///
///   reset frame clock (only when reset_frame_clock)
///   → poll/snapshot input and update pressed state
///   → engine-frame callback (unless skip_engine_frame)
///   → measure the completed frame
///   → calculate the delta for the *next* callback
///   → optional forced-delta override
///   → split into base/effective delta (gameplay pause zeroes effective)
///
/// The engine callback therefore consumes the effective delta produced by
/// the preceding frame — the critical recovered behavior. External-clock
/// synchronization is intentionally deferred (no subsystem with the
/// original's FUN_0042cc10 / GetEngineTime semantics exists yet).
///
/// Callables:
/// - clock_now() returns a monotonically increasing millisecond timestamp.
/// - poll_input() performs the frame's input step (snapshot → pressed
///   state → per-frame input-consumption reset).
/// - run_engine_frame() is the engine update/render dispatcher,
///   provisionally the original FUN_004200f0.
template <typename ClockNow, typename PollInput, typename RunEngineFrame>
void run_timed_frame(FrameTimingState& timing,
                     const bool reset_frame_clock,
                     const bool skip_engine_frame,
                     ClockNow clock_now,
                     PollInput poll_input,
                     RunEngineFrame run_engine_frame) {
  if (reset_frame_clock) {
    // The loop was blocked (inactive or suspended) since the previous
    // frame: re-baseline the frame clock so the inactive interval is not
    // measured as one enormous frame. The moving average and the current
    // deltas are deliberately preserved.
    const std::uint64_t now{clock_now()};
    timing.frame_start_time_ms = now;
    timing.current_time_ms = now;
    timing.secondary_frame_clock_ms = now;
  }

  // Input is prepared before the engine callback, and still runs when the
  // callback is skipped.
  poll_input();

  if (!skip_engine_frame) {
    // Provisional FUN_004200f0 (docs/ReverseEngineering.md): the engine's
    // update/render dispatcher. Its internals remain unresolved; OpenNomad
    // routes its own update and render calls through this position.
    run_engine_frame();
  }

  // update_external_clock_state() and the external-clock correction are
  // intentionally omitted until a subsystem with the original's semantics
  // exists; the normal path is "external clock inactive".

  // Measure the frame that just completed.
  const std::uint64_t now{clock_now()};
  timing.current_time_ms = now;
  timing.frame_time_ms = now - timing.frame_start_time_ms;
  // (old + new) / 2, integer division like the original's right shift.
  timing.average_frame_time_ms = (timing.average_frame_time_ms + timing.frame_time_ms) / 2U;
  if (timing.average_frame_time_ms == 0U) {
    timing.average_frame_time_ms = 1U;
  }
  if (timing.frame_time_ms == 0U) {
    timing.frame_time_ms = 1U;
  }
  timing.current_fps = 1000.0F / static_cast<float>(timing.frame_time_ms);
  timing.average_fps = 1000.0F / static_cast<float>(timing.average_frame_time_ms);
  timing.frame_start_time_ms = now;

  // Delta for the next callback, in Omikron units.
  float delta{calculate_delta(timing.time_scale_mode, timing.average_fps)};
  if (timing.forced_delta.has_value()) {
    delta = timing.forced_delta.value();
  }
  timing.base_delta = delta;
  // Gameplay pause zeroes only the effective delta; base_delta keeps the
  // unpaused value for consumers that may need it (menus, diagnostics).
  timing.effective_delta = timing.gameplay_paused ? 0.0F : delta;
}

}  // namespace App::FrameTiming

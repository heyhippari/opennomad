#pragma once

#include <cstdint>
#include <optional>

#include "Core/FrameTiming.hpp"
#include "Core/RuntimeActivityState.hpp"

namespace App::Debug {

/// Callback-boundary observation kept separately from FrameTimingState.
///
/// FrameTimingState contains the delta calculated after a timed frame. This
/// record captures the value at the callback boundary, before that next-frame
/// calculation can replace it.
struct EngineCallbackDebugObservation {
  std::uint64_t timed_frame_sequence{0};
  bool timed_frame_observed{false};
  bool ran{false};
  std::optional<float> consumed_delta_units;

  /// Starts one timed-frame observation. If the callback is skipped, these
  /// values remain the completed observation for that frame.
  void begin_timed_frame() {
    ++timed_frame_sequence;
    timed_frame_observed = true;
    ran = false;
    consumed_delta_units.reset();
  }

  /// Captures the authoritative effective delta at the callback boundary.
  void record_callback(const float delta_units) {
    ran = true;
    consumed_delta_units = delta_units;
  }
};

/// Read-only debugger projection of the authoritative application timing and
/// activity state.
struct RuntimeTimingDebugSnapshot {
  std::uint64_t last_completed_timestamp_ms{0};
  std::uint64_t last_completed_frame_time_ms{1};
  std::uint64_t moving_average_frame_time_ms{1};
  float current_fps{1000.0F};
  float average_fps{1000.0F};

  FrameTiming::TimeScaleMode time_scale_mode{FrameTiming::TimeScaleMode::k_dynamic};
  std::optional<float> forced_delta;
  float next_base_delta_units{1.0F};
  float next_effective_delta_units{1.0F};
  bool gameplay_paused{false};

  EngineCallbackDebugObservation last_engine_callback;
  bool skip_engine_frame{false};

  bool render_window_active{false};
  bool application_active{false};
  bool updates_suspended{false};
  bool may_run_frame{false};
  bool timing_reset_pending{false};
};

/// Builds the debugger projection without duplicating any authoritative gate
/// or timing values.
[[nodiscard]] inline RuntimeTimingDebugSnapshot make_runtime_timing_debug_snapshot(
    const FrameTiming::FrameTimingState& timing,
    const RuntimeActivityState& activity,
    const bool skip_engine_frame,
    const EngineCallbackDebugObservation& last_engine_callback) {
  return RuntimeTimingDebugSnapshot{.last_completed_timestamp_ms = timing.current_time_ms,
      .last_completed_frame_time_ms = timing.frame_time_ms,
      .moving_average_frame_time_ms = timing.average_frame_time_ms,
      .current_fps = timing.current_fps,
      .average_fps = timing.average_fps,
      .time_scale_mode = timing.time_scale_mode,
      .forced_delta = timing.forced_delta,
      .next_base_delta_units = timing.base_delta,
      .next_effective_delta_units = timing.effective_delta,
      .gameplay_paused = timing.gameplay_paused,
      .last_engine_callback = last_engine_callback,
      .skip_engine_frame = skip_engine_frame,
      .render_window_active = activity.render_window_active,
      .application_active = activity.application_active,
      .updates_suspended = activity.updates_suspended,
      .may_run_frame = activity.may_run_frame(),
      .timing_reset_pending = activity.reset_frame_timing_on_next_update};
}

/// Narrow capability through which the debugger inspects and safely controls
/// recovered application timing. It deliberately omits activity-gate writers.
class RuntimeTimingDebugSource {
 public:
  virtual ~RuntimeTimingDebugSource() = default;

  [[nodiscard]] virtual RuntimeTimingDebugSnapshot timing_debug_snapshot() const = 0;
  virtual void set_time_scale_mode(FrameTiming::TimeScaleMode mode) = 0;
  virtual void set_forced_delta(std::optional<float> delta) = 0;
  virtual void set_gameplay_paused(bool paused) = 0;
};

/// Stable UI label for a recovered Runtime time-scale mode.
[[nodiscard]] constexpr const char* time_scale_mode_name(const FrameTiming::TimeScaleMode mode) {
  switch (mode) {
    case FrameTiming::TimeScaleMode::k_dynamic:
      return "Dynamic";
    case FrameTiming::TimeScaleMode::k_fixed_30hz:
      return "Fixed 30 Hz";
    case FrameTiming::TimeScaleMode::k_fixed_60hz:
      return "Fixed 60 Hz";
    case FrameTiming::TimeScaleMode::k_fixed_300hz:
      return "Fixed 300 Hz";
    case FrameTiming::TimeScaleMode::k_fixed_15hz:
      return "Fixed 15 Hz";
  }
  return "Unknown";
}

}  // namespace App::Debug

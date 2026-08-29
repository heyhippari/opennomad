#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <cstdint>
#include <vector>

#include "Core/Debug/RuntimeTimingDebug.hpp"
#include "Core/FrameTiming.hpp"
#include "Core/RuntimeActivityState.hpp"

namespace {

/// Manual millisecond clock for deterministic tests: the test controls the
/// starting timestamp, inactive time, callback execution time, and whether
/// a frame takes zero milliseconds.
class FakeClock {
 public:
  explicit FakeClock(const std::uint64_t start_ms) : m_now(start_ms) {}

  [[nodiscard]] std::uint64_t now() const {
    return m_now;
  }
  void advance(const std::uint64_t ms) {
    m_now += ms;
  }

 private:
  std::uint64_t m_now{};
};

}  // namespace

TEST_SUITE("Core::FrameTiming") {
  using App::FrameTiming::calculate_delta;
  using App::FrameTiming::FrameTimingState;
  using App::FrameTiming::run_timed_frame;
  using App::FrameTiming::TimeScaleMode;

  TEST_CASE("The engine callback sees the previous frame's effective delta") {
    FrameTimingState timing;
    App::Debug::EngineCallbackDebugObservation callback_observation;
    FakeClock clock{100};
    std::vector<float> deltas_seen{};

    // Frame 1: clock reset; the callback observes the initial delta (1.0)
    // and executes for 16 ms.
    callback_observation.begin_timed_frame();
    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock, &timing, &deltas_seen, &callback_observation]() {
          callback_observation.record_callback(timing.effective_delta);
          deltas_seen.push_back(timing.effective_delta);
          clock.advance(16);
        });
    REQUIRE_EQ(deltas_seen.size(), 1U);
    CHECK_EQ(deltas_seen.at(0), doctest::Approx(1.0F));

    // Frame 2: 8 ms of active event-processing time counts normally. The
    // callback sees the delta calculated at the END of frame 1, not a
    // freshly measured one.
    clock.advance(8);
    callback_observation.begin_timed_frame();
    run_timed_frame(
        timing,
        false,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock, &timing, &deltas_seen, &callback_observation]() {
          callback_observation.record_callback(timing.effective_delta);
          deltas_seen.push_back(timing.effective_delta);
          clock.advance(16);
        });
    REQUIRE_EQ(deltas_seen.size(), 2U);
    // Frame 1 measured 16 ms: average (1+16)/2 = 8 ms -> 125 FPS -> 0.24.
    CHECK_EQ(deltas_seen.at(1), doctest::Approx(0.24F));
    // Frame 2 measured 8+16 = 24 ms: average (8+24)/2 = 16 ms -> 62.5 FPS
    // -> 30/62.5 = 0.48, available only after the callback returned.
    CHECK_EQ(timing.effective_delta, doctest::Approx(0.48F));
    CHECK_EQ(timing.frame_time_ms, 24U);

    App::RuntimeActivityState activity;
    activity.on_render_window_active(true);
    activity.on_application_active(true);
    const auto snapshot{App::Debug::make_runtime_timing_debug_snapshot(
        timing, activity, false, callback_observation)};
    REQUIRE(snapshot.last_engine_callback.consumed_delta_units.has_value());
    CHECK_EQ(snapshot.last_engine_callback.consumed_delta_units.value(), doctest::Approx(0.24F));
    CHECK_EQ(snapshot.next_effective_delta_units, doctest::Approx(0.48F));
  }

  TEST_CASE("Timing debug snapshot distinguishes pause, skip, and activity gates") {
    FrameTimingState timing;
    timing.time_scale_mode = TimeScaleMode::k_fixed_30hz;
    timing.base_delta = 1.0F;
    timing.effective_delta = 0.0F;
    timing.gameplay_paused = true;

    App::RuntimeActivityState activity;
    activity.on_render_window_active(true);
    activity.on_application_active(true);
    activity.set_updates_suspended(true);
    activity.reset_frame_timing_on_next_update = true;

    App::Debug::EngineCallbackDebugObservation callback_observation;
    callback_observation.begin_timed_frame();
    const auto snapshot{App::Debug::make_runtime_timing_debug_snapshot(
        timing, activity, true, callback_observation)};

    CHECK(snapshot.gameplay_paused);
    CHECK_EQ(snapshot.next_base_delta_units, doctest::Approx(1.0F));
    CHECK_EQ(snapshot.next_effective_delta_units, doctest::Approx(0.0F));
    CHECK_FALSE(snapshot.last_engine_callback.ran);
    CHECK(snapshot.skip_engine_frame);
    CHECK(snapshot.render_window_active);
    CHECK(snapshot.application_active);
    CHECK(snapshot.updates_suspended);
    CHECK_FALSE(snapshot.may_run_frame);
    CHECK(snapshot.timing_reset_pending);
  }

  TEST_CASE("Callback execution time contributes to the measured frame duration") {
    FrameTimingState timing;
    FakeClock clock{100};

    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock]() {
          clock.advance(32);
        });

    CHECK_EQ(timing.frame_time_ms, 32U);
    CHECK_EQ(timing.current_time_ms, 132U);
  }

  TEST_CASE("Active event-processing time between frames contributes normally") {
    FrameTimingState timing;
    FakeClock clock{100};

    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock]() {
          clock.advance(16);
        });
    CHECK_EQ(timing.frame_time_ms, 16U);

    // The loop processed events for 8 ms before this frame; without a
    // timing reset that time belongs to the new frame.
    clock.advance(8);
    run_timed_frame(
        timing,
        false,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock]() {
          clock.advance(16);
        });
    CHECK_EQ(timing.frame_time_ms, 24U);
  }

  TEST_CASE("Inactive waiting time is excluded after a clock reset") {
    FrameTimingState timing;
    FakeClock clock{100};

    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock]() {
          clock.advance(16);
        });
    CHECK_EQ(timing.frame_time_ms, 16U);

    // The loop was blocked waiting for events; the resumed frame carries
    // the reset request and rebaselines immediately before input.
    clock.advance(10'000);
    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock]() {
          clock.advance(16);
        });
    CHECK_EQ(timing.frame_time_ms, 16U);
  }

  TEST_CASE("Resetting the frame clock does not reset the moving average") {
    FrameTimingState timing;
    FakeClock clock{100};

    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock]() {
          clock.advance(16);
        });
    CHECK_EQ(timing.average_frame_time_ms, 8U);

    clock.advance(10'000);
    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock]() {
          clock.advance(16);
        });
    // (8 + 16) / 2: the pre-reset average was retained.
    CHECK_EQ(timing.average_frame_time_ms, 12U);
  }

  TEST_CASE("A zero-millisecond frame is clamped to one millisecond") {
    FrameTimingState timing;
    FakeClock clock{50};

    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        []() {});

    CHECK_EQ(timing.frame_time_ms, 1U);
    CHECK_EQ(timing.average_frame_time_ms, 1U);
    CHECK_EQ(timing.current_fps, doctest::Approx(1000.0F));
    CHECK_EQ(timing.average_fps, doctest::Approx(1000.0F));
  }

  TEST_CASE("The moving average uses (old + new) / 2") {
    FrameTimingState timing;
    timing.average_frame_time_ms = 40;
    FakeClock clock{100};

    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock]() {
          clock.advance(16);
        });

    CHECK_EQ(timing.average_frame_time_ms, 28U);
  }

  TEST_CASE("Instantaneous and average FPS are calculated separately") {
    FrameTimingState timing;
    timing.average_frame_time_ms = 20;  // pre-existing average: 50 FPS
    FakeClock clock{100};

    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock]() {
          clock.advance(16);
        });

    CHECK_EQ(timing.current_fps, doctest::Approx(1000.0F / 16.0F));
    CHECK_EQ(timing.average_fps, doctest::Approx(1000.0F / 18.0F));
  }

  TEST_CASE("Dynamic delta uses 30 / averageFPS") {
    CHECK_EQ(calculate_delta(TimeScaleMode::k_dynamic, 50.0F), doctest::Approx(0.6F));
    CHECK_EQ(calculate_delta(TimeScaleMode::k_dynamic, 125.0F), doctest::Approx(0.24F));
  }

  TEST_CASE("Dynamic delta is capped at 3.0") {
    // 3.0 delta units = 100 ms: the largest dynamic simulation step.
    CHECK_EQ(calculate_delta(TimeScaleMode::k_dynamic, 1.0F), doctest::Approx(3.0F));
    CHECK_EQ(calculate_delta(TimeScaleMode::k_dynamic, 0.1F), doctest::Approx(3.0F));
  }

  TEST_CASE("Fixed time-scale modes produce the exact known values") {
    CHECK_EQ(calculate_delta(TimeScaleMode::k_fixed_30hz, 999.0F), doctest::Approx(1.0F));
    CHECK_EQ(calculate_delta(TimeScaleMode::k_fixed_60hz, 999.0F), doctest::Approx(0.5F));
    CHECK_EQ(calculate_delta(TimeScaleMode::k_fixed_300hz, 999.0F), doctest::Approx(0.1F));
    CHECK_EQ(calculate_delta(TimeScaleMode::k_fixed_15hz, 999.0F), doctest::Approx(2.0F));

    // Fixed modes directly replace the measured delta: the FPS is ignored.
    CHECK_EQ(calculate_delta(TimeScaleMode::k_fixed_60hz, 1.0F), doctest::Approx(0.5F));
  }

  TEST_CASE("Out-of-range time-scale values fall back to the dynamic calculation") {
    // The original switch had no meaningful default assignment; unknown
    // values behave like the dynamic mode.
    const auto invalid{static_cast<TimeScaleMode>(99)};
    CHECK_EQ(calculate_delta(invalid, 100.0F), doctest::Approx(0.3F));
  }

  TEST_CASE("Forced delta overrides the calculated delta until cleared") {
    FrameTimingState timing;
    timing.time_scale_mode = TimeScaleMode::k_fixed_30hz;
    timing.forced_delta = 2.5F;
    FakeClock clock{100};

    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        []() {});
    CHECK_EQ(timing.base_delta, doctest::Approx(2.5F));
    CHECK_EQ(timing.effective_delta, doctest::Approx(2.5F));

    timing.forced_delta.reset();
    run_timed_frame(
        timing,
        false,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        []() {});
    CHECK_EQ(timing.base_delta, doctest::Approx(1.0F));
    CHECK_EQ(timing.effective_delta, doctest::Approx(1.0F));
  }

  TEST_CASE("Gameplay pause preserves base delta and zeroes effective delta") {
    FrameTimingState timing;
    timing.time_scale_mode = TimeScaleMode::k_fixed_30hz;
    timing.gameplay_paused = true;
    FakeClock clock{100};

    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        []() {});

    CHECK_EQ(timing.base_delta, doctest::Approx(1.0F));
    CHECK_EQ(timing.effective_delta, doctest::Approx(0.0F));
  }

  TEST_CASE("The next callback observes the paused effective delta") {
    FrameTimingState timing;
    timing.time_scale_mode = TimeScaleMode::k_fixed_30hz;
    // Pause is already active before frame 1's timing calculation.
    timing.gameplay_paused = true;
    FakeClock clock{100};
    std::vector<float> deltas_seen{};

    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock, &timing, &deltas_seen]() {
          deltas_seen.push_back(timing.effective_delta);
          clock.advance(16);
        });
    run_timed_frame(
        timing,
        false,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock, &timing, &deltas_seen]() {
          deltas_seen.push_back(timing.effective_delta);
          clock.advance(16);
        });

    REQUIRE_EQ(deltas_seen.size(), 2U);
    CHECK_EQ(deltas_seen.at(0), doctest::Approx(1.0F));
    CHECK_EQ(deltas_seen.at(1), doctest::Approx(0.0F));
  }

  TEST_CASE("skip_engine_frame suppresses only the callback") {
    FrameTimingState timing;
    FakeClock clock{100};
    int callbacks{0};
    int polls{0};

    run_timed_frame(
        timing,
        true,
        true,
        [&clock]() {
          return clock.now();
        },
        [&polls]() {
          ++polls;
        },
        [&callbacks]() {
          ++callbacks;
        });

    CHECK_EQ(callbacks, 0);
    CHECK_EQ(polls, 1);
    // Timing still ran: the frame was measured and the delta updated.
    CHECK_EQ(timing.frame_time_ms, 1U);
    CHECK_EQ(timing.current_time_ms, 100U);
  }

  TEST_CASE("skip_engine_frame still updates input and timing") {
    FrameTimingState timing;
    FakeClock clock{100};
    int polls{0};

    run_timed_frame(
        timing,
        true,
        true,
        [&clock]() {
          return clock.now();
        },
        [&clock, &polls]() {
          ++polls;
          clock.advance(5);  // input snapshot work belongs to the frame
        },
        []() {});

    CHECK_EQ(polls, 1);
    CHECK_EQ(timing.frame_time_ms, 5U);
  }

  TEST_CASE("The timed frame distinguishes normal, skip, and pause states") {
    // Normal: callback runs, effective delta equals base delta.
    // skip: no callback, input and timing still run.
    // Pause: callback runs, effective delta becomes zero.
    // update suspension is handled outside the wrapper:
    // MainLoopController::advance returns k_wait_for_event and the timed
    // frame is never invoked (covered in Core::MainLoop tests).
    FrameTimingState normal;
    FrameTimingState skipped;
    FrameTimingState paused;
    paused.time_scale_mode = TimeScaleMode::k_fixed_30hz;
    paused.gameplay_paused = true;
    FakeClock clock{100};
    int normal_callbacks{0};
    int skipped_callbacks{0};
    int paused_callbacks{0};

    run_timed_frame(
        normal,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&normal_callbacks]() {
          ++normal_callbacks;
        });
    run_timed_frame(
        skipped,
        true,
        true,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&skipped_callbacks]() {
          ++skipped_callbacks;
        });
    run_timed_frame(
        paused,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&paused_callbacks]() {
          ++paused_callbacks;
        });

    CHECK_EQ(normal_callbacks, 1);
    CHECK_EQ(skipped_callbacks, 0);
    CHECK_EQ(paused_callbacks, 1);
    CHECK_EQ(normal.effective_delta, doctest::Approx(normal.base_delta));
    CHECK_EQ(paused.base_delta, doctest::Approx(1.0F));
    CHECK_EQ(paused.effective_delta, doctest::Approx(0.0F));
  }

  TEST_CASE("The secondary frame clock is written only by the timing reset") {
    FrameTimingState timing;
    FakeClock clock{100};

    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock]() {
          clock.advance(16);
        });
    CHECK_EQ(timing.secondary_frame_clock_ms, 100U);

    clock.advance(1'000);
    run_timed_frame(
        timing,
        false,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock]() {
          clock.advance(16);
        });
    CHECK_EQ(timing.secondary_frame_clock_ms, 100U);

    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        [&clock]() {
          clock.advance(16);
        });
    CHECK_EQ(timing.secondary_frame_clock_ms, 1132U);
  }

  TEST_CASE("A timing reset does not clear the forced delta or the moving average") {
    // The reset request only rebaselines the frame clock; the moving
    // average and the current (forced) delta survive. The wrapper never
    // owns the request flag itself — RuntimeActivityState holds it and the
    // application clears it after the frame returns (Core::MainLoop tests).
    FrameTimingState timing;
    timing.forced_delta = 2.5F;
    FakeClock clock{100};

    clock.advance(9'000);  // inactive interval before the resumed frame
    run_timed_frame(
        timing,
        true,
        false,
        [&clock]() {
          return clock.now();
        },
        []() {},
        []() {});

    CHECK_EQ(timing.base_delta, doctest::Approx(2.5F));
    CHECK_EQ(timing.effective_delta, doctest::Approx(2.5F));
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

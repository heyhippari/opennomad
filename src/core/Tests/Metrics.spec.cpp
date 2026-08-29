#include "Core/Debug/Metrics.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

TEST_SUITE("Core::Debug::Metrics") {
  TEST_CASE("Initial state is zero") {
    const auto& metrics_ref = App::Debug::Metrics::get();

    // NOTE: Metrics is a singleton; previous tests may have left it populated.
    // We test behaviour, not initial state, in the cases below.
    static_cast<void>(metrics_ref);  // suppress unused warning
  }

  TEST_CASE("Frame timing accumulates correctly") {
    auto& metrics_ref = App::Debug::Metrics::get();

    const std::uint64_t before_count = metrics_ref.get_frame_count();

    // Feed 3 frames at exactly 16.67 ms each (~60 fps).
    metrics_ref.on_frame_begin(0.01667F);
    metrics_ref.on_frame_begin(0.01667F);
    metrics_ref.on_frame_begin(0.01667F);

    CHECK_EQ(metrics_ref.get_frame_count(), before_count + 3);
    CHECK_LT(std::abs(metrics_ref.get_frame_time_ms() - 16.67F), 0.1F);

    const float fps = metrics_ref.get_fps();
    CHECK_GT(fps, 55.0F);
    CHECK_LT(fps, 65.0F);

    // Min/max/avg should be non-zero.
    CHECK_GT(metrics_ref.get_frame_time_min(), 0.0F);
    CHECK_GT(metrics_ref.get_frame_time_max(), 0.0F);
    CHECK_GT(metrics_ref.get_frame_time_avg(), 0.0F);
  }

  TEST_CASE("Frame time history is within bounds") {
    auto& metrics_ref = App::Debug::Metrics::get();

    // Push 400 frames (exceeds kHistorySize=300).
    for (int frame = 0; frame < 400; ++frame) {
      metrics_ref.on_frame_begin(0.01F);
    }

    // Count should not exceed the buffer size.
    CHECK_LE(metrics_ref.get_frame_time_history_count(), std::size_t{300});

    // The history array should be accessible.
    const auto& history = metrics_ref.get_frame_time_history();
    CHECK_GE(history.size(), std::size_t{300});

    // Verify the head pointer is within bounds.
    const std::size_t head = metrics_ref.get_frame_time_history_head();
    CHECK_LT(head, std::size_t{300});
  }

  TEST_CASE("System info can be queried without crash") {
    auto& metrics_ref = App::Debug::Metrics::get();

    // These should just not throw (no GL context in test, so values may be "N/A").
    static_cast<void>(metrics_ref.opengl_info());
    static_cast<void>(metrics_ref.sdl_info());
    static_cast<void>(metrics_ref.window_info());
    static_cast<void>(metrics_ref.audio_info());
  }

  TEST_CASE("Window / audio info setters work") {
    auto& metrics_ref = App::Debug::Metrics::get();

    metrics_ref.set_window_info_entry("TestKey", "TestValue");
    CHECK_EQ(metrics_ref.window_info().at("TestKey"), "TestValue");

    metrics_ref.set_audio_info_entry("Driver", "PulseAudio");
    CHECK_EQ(metrics_ref.audio_info().at("Driver"), "PulseAudio");
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

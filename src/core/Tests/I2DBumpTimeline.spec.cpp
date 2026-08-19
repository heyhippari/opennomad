#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Core/Interface/I2DBumpEffect.hpp"
#include "Core/Interface/I2DBumpEndpoint.hpp"
#include "Core/Interface/I2DBumpTimeline.hpp"
#include "Core/Omikron/IndexedBmp8.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace,
//             cppcoreguidelines-avoid-do-while,
//             cert-err33-c,
//             readability-identifier-length,
//             readability-math-missing-parentheses,
//             bugprone-implicit-widening-of-multiplication-result,
//             cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

namespace {

App::Omikron::IndexedBmp8 flat_height_map(const std::uint8_t value) {
  return App::Omikron::IndexedBmp8{
      .width = 256,
      .height = 256,
      .indices = std::vector<std::uint8_t>(256U * 256U, value)};
}

}  // namespace

TEST_SUITE("Core::Interface::I2DBumpTimeline") {
  TEST_CASE("wrapped_lerp_256 walks the shortest arc") {
    using App::Interface::wrapped_lerp_256;

    CHECK(wrapped_lerp_256(10.0F, 20.0F, 0.5F) == doctest::Approx(15.0F));
    // 250 -> 4 wraps forward (delta +10): the midpoint is 255, not 127.
    CHECK(wrapped_lerp_256(250.0F, 4.0F, 0.5F) == doctest::Approx(255.0F));
    // 4 -> 250 wraps backward (delta -10): the midpoint is -1.
    CHECK(wrapped_lerp_256(4.0F, 250.0F, 0.5F) == doctest::Approx(-1.0F));
    CHECK(wrapped_lerp_256(255.0F, 0.0F, 0.5F) == doctest::Approx(255.5F));
    CHECK(wrapped_lerp_256(0.0F, 255.0F, 0.5F) == doctest::Approx(-0.5F));

    // Endpoints collapse to the exact inputs.
    CHECK(wrapped_lerp_256(250.0F, 4.0F, 0.0F) == doctest::Approx(250.0F));
    CHECK(wrapped_lerp_256(250.0F, 4.0F, 1.0F) == doctest::Approx(260.0F));  // 4 mod 256
  }

  TEST_CASE("wrapped_lerp_256 is deterministic at the ±128 ambiguity") {
    using App::Interface::wrapped_lerp_256;
    CHECK(wrapped_lerp_256(0.0F, 128.0F, 0.5F) == doctest::Approx(64.0F));
    CHECK(wrapped_lerp_256(128.0F, 0.0F, 0.5F) == doctest::Approx(64.0F));
  }

  TEST_CASE("Timeline reaches the same tick after one second at any refresh rate") {
    using App::Interface::BumpTimelineState;
    using App::Interface::advance_bump_timeline;

    struct FramePattern {
      double frame_time;
      std::size_t frames;
    };
    const std::array<FramePattern, 5> patterns{{
        {1.0 / 30.0, 30},
        {1.0 / 60.0, 60},
        {1.0 / 120.0, 120},
        {1.0 / 144.0, 144},
        {1.0 / 240.0, 240},
    }};

    for (const FramePattern& pattern : patterns) {
      BumpTimelineState state;
      double remainder{0.0};
      for (std::size_t frame{0}; frame < pattern.frames; ++frame) {
        const auto advance{advance_bump_timeline(state, remainder, pattern.frame_time)};
        state = advance.state;
      }

      // One second of effect time = exactly 30 endpoint intervals, regardless
      // of how many display frames delivered it.
      const double total_ticks{
          static_cast<double>(state.current_tick) + static_cast<double>(state.alpha)};
      CHECK(total_ticks == doctest::Approx(30.0).epsilon(1e-3));
    }
  }

  TEST_CASE("Irregular frame times accumulate to the correct tick and alpha") {
    using App::Interface::BumpTimelineState;
    using App::Interface::advance_bump_timeline;

    // 8+9+14+55+3+41 ms = 130 ms = 3.9 endpoint intervals.
    const std::array<double, 6> frames{0.008, 0.009, 0.014, 0.055, 0.003, 0.041};

    BumpTimelineState state;
    double remainder{0.0};
    for (const double frame : frames) {
      const auto advance{advance_bump_timeline(state, remainder, frame)};
      state = advance.state;
    }

    CHECK_EQ(state.current_tick, 3);
    CHECK(state.alpha == doctest::Approx(0.9F).epsilon(1e-2));
  }

  TEST_CASE("A 100 ms frame crosses three ticks at once") {
    using App::Interface::BumpTimelineState;
    using App::Interface::advance_bump_timeline;

    BumpTimelineState state;
    double remainder{0.0};
    const auto advance{advance_bump_timeline(state, remainder, 0.1)};

    CHECK_EQ(advance.ticks_advanced, 3);
    CHECK_EQ(advance.state.current_tick, 3);
    CHECK(advance.state.alpha == doctest::Approx(0.0F).epsilon(1e-6));
  }

  TEST_CASE("Endpoint state matches the CPU reference at representative ticks") {
    using App::Interface::I2DBumpEndpointState;
    using App::Interface::advance_endpoint;

    for (const std::uint64_t ticks : {0ULL, 1ULL, 2ULL, 30ULL, 300ULL}) {
      auto effect{App::Interface::I2DBumpEffect::create(flat_height_map(7))};
      REQUIRE(effect.has_value());
      effect->advance_ticks(static_cast<std::uint32_t>(ticks));

      I2DBumpEndpointState endpoint;
      advance_endpoint(endpoint, ticks);

      CHECK_EQ(endpoint.tick, effect->tick_index());
      CHECK_EQ(endpoint.light_x, effect->light_x());
      CHECK_EQ(endpoint.light_y, effect->light_y());
      CHECK(endpoint.phase_a == doctest::Approx(effect->phase_a()).epsilon(1e-12));
      CHECK(endpoint.phase_b == doctest::Approx(effect->phase_b()).epsilon(1e-12));
      CHECK(endpoint.phase_c == doctest::Approx(effect->phase_c()).epsilon(1e-12));
      CHECK(endpoint.phase_d == doctest::Approx(effect->phase_d()).epsilon(1e-12));
    }
  }
}

// NOLINTEND(misc-use-anonymous-namespace,
//           cppcoreguidelines-avoid-do-while,
//           cert-err33-c,
//           readability-identifier-length,
//           readability-math-missing-parentheses,
//           bugprone-implicit-widening-of-multiplication-result,
//           cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

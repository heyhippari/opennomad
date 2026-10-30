#include "Core/WorldFog.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>

#include "Core/WorldPresentation.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

namespace {

/// ANEKBAH (AREA 0) +0x90..+0x9F.
constexpr std::array<std::uint32_t, 4> K_ANEKBAH_COLORS{0xB1AB5FU, 0x8C6946U, 0x523A1BU, 0x1A1309U};

}  // namespace

TEST_SUITE("Core::WorldFog") {
  TEST_CASE("Day colour selects the phase colour at each phase boundary") {
    for (std::int64_t phase{0}; phase < 4; ++phase) {
      CAPTURE(phase);
      CHECK_EQ(App::Fog::day_color(K_ANEKBAH_COLORS, phase * App::Fog::K_DAY_PHASE_LENGTH),
          K_ANEKBAH_COLORS.at(static_cast<std::size_t>(phase)));
    }
    // The clock wraps every day.
    CHECK_EQ(App::Fog::day_color(K_ANEKBAH_COLORS, App::Fog::K_DAY_LENGTH), K_ANEKBAH_COLORS.at(0));
  }

  TEST_CASE("Day colour interpolates each byte toward the next phase with truncation") {
    // New game: clock 2,000,000 is 2/9 into phase 2 (0x523A1B -> 0x1A1309).
    // R: 82 + trunc(-56 * 2/9) = 70, G: 58 + trunc(-39 * 2/9) = 50, B: 27 + trunc(-18 * 2/9) = 23.
    CHECK_EQ(App::Fog::day_color(K_ANEKBAH_COLORS, App::Fog::K_NEW_GAME_CLOCK), 0x463217U);

    // Phase 3 interpolates back toward phase 0.
    constexpr std::array<std::uint32_t, 4> k_colors{0x0000FFU, 0x000000U, 0x000000U, 0x000000U};
    const std::int64_t half_phase_3{
        (3 * App::Fog::K_DAY_PHASE_LENGTH) + (App::Fog::K_DAY_PHASE_LENGTH / 2)};
    CHECK_EQ(App::Fog::day_color(k_colors, half_phase_3), 0x00007FU);

    // The unused high byte of an authored colour never leaks into the result.
    constexpr std::array<std::uint32_t, 4> k_tagged{
        0xFF000629U, 0xFF000629U, 0xFF000629U, 0xFF000629U};
    CHECK_EQ(App::Fog::day_color(k_tagged, 12345), 0x000629U);
  }

  TEST_CASE("Fog distance is the AREA distance capped by the Clip Distance preference") {
    CHECK_EQ(App::Fog::effective_distance_metres(0, 50), 50);
    CHECK_EQ(App::Fog::effective_distance_metres(80, 50), 50);
    CHECK_EQ(App::Fog::effective_distance_metres(35, 50), 35);
    CHECK_EQ(App::Fog::effective_distance_metres(80, 200), 80);

    const App::Fog::Parameters parameters{App::Fog::evaluate(
        App::Fog::Environment{.colors = K_ANEKBAH_COLORS, .distance_metres = 0}, 0, 50)};
    CHECK_EQ(parameters.color_rgb, K_ANEKBAH_COLORS.at(0));
    CHECK(parameters.end_inches == doctest::Approx(1968.5039F));
    CHECK(parameters.start_inches == doctest::Approx(1968.5039F * 0.25F));
    // Context +0x14C: the far alpha fade for blended surfaces starts at 95 %.
    CHECK(parameters.fade_start_inches == doctest::Approx(1968.5039F * 0.95F));
  }

  TEST_CASE("Day clock adds 0xA6 every five engine frames after the first six") {
    App::Fog::DayClock clock;
    CHECK_EQ(clock.value(), App::Fog::K_NEW_GAME_CLOCK);
    for (int frame{0}; frame < 5; ++frame) {
      clock.tick();
    }
    CHECK_EQ(clock.value(), App::Fog::K_NEW_GAME_CLOCK);
    clock.tick();
    CHECK_EQ(clock.value(), App::Fog::K_NEW_GAME_CLOCK + App::Fog::K_CLOCK_STEP);

    // One second is 30 nominal engine frames: steps on frames 6, 11, 16, 21 and 26.
    App::Fog::DayClock timed;
    timed.advance(1.0F);
    CHECK_EQ(timed.value(), App::Fog::K_NEW_GAME_CLOCK + (5 * App::Fog::K_CLOCK_STEP));

    timed.set(7);
    CHECK_EQ(timed.value(), 7);
  }

  TEST_CASE("Session reset restores the new-game clock") {
    App::WorldPresentationState presentation;
    presentation.day_clock().set(42);
    presentation.clear();
    CHECK_EQ(presentation.day_clock().value(), App::Fog::K_NEW_GAME_CLOCK);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

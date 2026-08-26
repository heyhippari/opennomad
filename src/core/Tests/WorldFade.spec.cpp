#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include <cstdint>

#include "Core/WorldPresentation.hpp"

namespace {

using App::WorldFadeCommand;
using App::WorldFadeState;

WorldFadeCommand fade(const std::uint8_t mode,
    const std::uint32_t color,
    const std::int16_t duration_units,
    const std::int16_t delay_units = 0) {
  return WorldFadeCommand{.mode = mode,
      .color = color,
      .duration_units = duration_units,
      .delay_units = delay_units};
}

}  // namespace

TEST_SUITE("Core::WorldFadeState") {
  TEST_CASE("Mode 1 fades into the authored colour and keeps its terminal alpha") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(1, 0x00FFFFFFU, 30)));
    CHECK_EQ(state.alpha(), 0.0F);
    CHECK_EQ(state.color(), 0x00FFFFFFU);
    CHECK_EQ(state.duration_seconds(), 1.0F);

    state.update(0.5F);
    CHECK(state.alpha() == doctest::Approx(0.5F));
    state.update(0.5F);
    CHECK_EQ(state.alpha(), 1.0F);
    CHECK_EQ(state.mode(), 1U);
    CHECK_FALSE(state.transitioning());
    state.update(2.0F);
    CHECK_EQ(state.alpha(), 1.0F);
  }

  TEST_CASE("Mode 2 fades out of the authored colour to invisibility") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(2, 0x00123456U, 30)));
    CHECK_EQ(state.alpha(), 1.0F);
    CHECK_EQ(state.color(), 0x00123456U);

    state.update(0.5F);
    CHECK(state.alpha() == doctest::Approx(0.5F));
    state.update(0.5F);
    CHECK_EQ(state.alpha(), 0.0F);
    CHECK_EQ(state.mode(), 0U);
    CHECK_FALSE(state.transitioning());
    CHECK(state.apply_command(fade(1, 0x00000000U, 30)));
  }

  TEST_CASE("Zero-duration fades select their direction's terminal alpha") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(1, 0xAAFFFFFFU, 0)));
    CHECK_EQ(state.alpha(), 1.0F);
    CHECK_EQ(state.color(), 0x00FFFFFFU);  // High byte is not inferred as alpha.

    REQUIRE(state.apply_command(fade(2, 0x00010203U, 0)));
    CHECK_EQ(state.alpha(), 0.0F);
    CHECK_EQ(state.mode(), 0U);
    CHECK_EQ(state.color(), 0x00010203U);
  }

  TEST_CASE("Active mode 1 rejects mode 1 without changing state and accepts mode 2") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(1, 0x00FFFFFFU, 60, 3)));
    state.update(1.0F);
    const std::uint8_t mode{state.mode()};
    const std::uint32_t color{state.color()};
    const float alpha{state.alpha()};
    const float elapsed{state.elapsed_seconds()};
    const float duration{state.duration_seconds()};
    const float delay{state.remaining_delay_seconds()};

    CHECK_FALSE(state.apply_command(fade(1, 0x00010203U, 30)));
    CHECK_EQ(state.mode(), mode);
    CHECK_EQ(state.color(), color);
    CHECK_EQ(state.alpha(), alpha);
    CHECK_EQ(state.elapsed_seconds(), elapsed);
    CHECK_EQ(state.duration_seconds(), duration);
    CHECK_EQ(state.remaining_delay_seconds(), delay);

    REQUIRE(state.apply_command(fade(2, 0x0000FF00U, -30)));
    CHECK_EQ(state.mode(), 2U);
    CHECK_EQ(state.color(), 0x0000FF00U);
    CHECK_EQ(state.elapsed_seconds(), 0.0F);
    CHECK_EQ(state.duration_seconds(), 1.0F);
    CHECK_EQ(state.alpha(), 1.0F);
  }

  TEST_CASE("Active mode 2 rejects either overlapping mode") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(2, 0x00ABCDEFU, 30)));
    state.update(0.25F);
    const float alpha{state.alpha()};

    CHECK_FALSE(state.apply_command(fade(1, 0x00000000U, 30)));
    CHECK_FALSE(state.apply_command(fade(2, 0x00000000U, 30)));
    CHECK_EQ(state.mode(), 2U);
    CHECK_EQ(state.color(), 0x00ABCDEFU);
    CHECK_EQ(state.alpha(), alpha);
  }

  TEST_CASE("Unsupported mode is rejected while inactive") {
    WorldFadeState state;
    CHECK_FALSE(state.apply_command(fade(3, 0x00000000U, 30)));
    CHECK_EQ(state.mode(), 0U);
  }

  TEST_CASE("Completed mode 1 remains active for arbitration") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(1, 0x00FFFFFFU, 30)));
    state.update(1.0F);
    REQUIRE_EQ(state.mode(), 1U);
    REQUIRE_EQ(state.alpha(), 1.0F);
    CHECK_FALSE(state.apply_command(fade(1, 0x00000000U, 30)));
    CHECK(state.apply_command(fade(2, 0x00000000U, 30)));
  }

  TEST_CASE("Delay consumes authored 30 Hz units before fade progress") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(1, 0x00FFFFFFU, 30, 2)));
    constexpr float k_tick{1.0F / 30.0F};

    state.update(k_tick);
    CHECK_EQ(state.elapsed_seconds(), 0.0F);
    CHECK_EQ(state.alpha(), 0.0F);
    state.update(k_tick);
    CHECK_EQ(state.elapsed_seconds(), 0.0F);
    CHECK_EQ(state.alpha(), 0.0F);
    state.update(k_tick);
    CHECK(state.elapsed_seconds() == doctest::Approx(k_tick));
    CHECK(state.alpha() == doctest::Approx(k_tick));
  }

  TEST_CASE("A frame remainder advances the fade after exhausting delay") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(1, 0x00FFFFFFU, 30, 1)));
    state.update((1.0F / 30.0F) - 0.01F);
    REQUIRE(state.remaining_delay_seconds() == doctest::Approx(0.01F));

    state.update(0.03F);
    CHECK_EQ(state.remaining_delay_seconds(), 0.0F);
    CHECK(state.elapsed_seconds() == doctest::Approx(0.02F));
    CHECK(state.alpha() == doctest::Approx(0.02F));
  }

  TEST_CASE("Negative delay is immediate and reset clears all state") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(1, 0x00FFFFFFU, 30, -2)));
    CHECK_EQ(state.remaining_delay_seconds(), 0.0F);
    state.update(0.25F);
    REQUIRE_GT(state.alpha(), 0.0F);

    state.reset();
    CHECK_EQ(state.mode(), 0U);
    CHECK_EQ(state.alpha(), 0.0F);
    CHECK_EQ(state.elapsed_seconds(), 0.0F);
    CHECK_EQ(state.duration_seconds(), 0.0F);
    CHECK_EQ(state.remaining_delay_seconds(), 0.0F);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

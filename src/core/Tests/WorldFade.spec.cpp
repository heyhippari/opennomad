#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include <cstdint>

#include "Core/WorldPresentation.hpp"

namespace {

using App::WorldFadeCommand;
using App::WorldFadeState;

WorldFadeCommand fade(const std::uint8_t mode,
    const std::uint32_t color,
    const std::int16_t duration_units) {
  return WorldFadeCommand{.scene_id = 118,
      .scene_generation = 4,
      .mode = mode,
      .color = color,
      .duration_units = duration_units,
      .operand_c = 0};
}

}  // namespace

TEST_SUITE("Core::WorldFadeState") {
  TEST_CASE("Mode 1 fades into the authored colour and keeps its terminal alpha") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(1, 0x00FFFFFFU, 30), 118, 4));
    CHECK_EQ(state.alpha(), 0.0F);
    CHECK_EQ(state.color(), 0x00FFFFFFU);
    CHECK_EQ(state.duration_seconds(), 1.0F);

    state.update(0.5F);
    CHECK(state.alpha() == doctest::Approx(0.5F));
    state.update(0.5F);
    CHECK_EQ(state.alpha(), 1.0F);
    CHECK_FALSE(state.transitioning());
    state.update(2.0F);
    CHECK_EQ(state.alpha(), 1.0F);
  }

  TEST_CASE("Mode 2 fades out of the authored colour to invisibility") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(2, 0x00123456U, 30), 118, 4));
    CHECK_EQ(state.alpha(), 1.0F);
    CHECK_EQ(state.color(), 0x00123456U);

    state.update(0.5F);
    CHECK(state.alpha() == doctest::Approx(0.5F));
    state.update(0.5F);
    CHECK_EQ(state.alpha(), 0.0F);
    CHECK_FALSE(state.transitioning());
  }

  TEST_CASE("Zero-duration fades select their direction's terminal alpha") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(1, 0xAAFFFFFFU, 0), 118, 4));
    CHECK_EQ(state.alpha(), 1.0F);
    CHECK_EQ(state.color(), 0x00FFFFFFU);  // High byte is not inferred as alpha.

    REQUIRE(state.apply_command(fade(2, 0x00010203U, 0), 118, 4));
    CHECK_EQ(state.alpha(), 0.0F);
    CHECK_EQ(state.color(), 0x00010203U);
  }

  TEST_CASE("A new command resets elapsed duration direction and target colour") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(1, 0x00FFFFFFU, 60), 118, 4));
    state.update(1.0F);
    REQUIRE(state.alpha() == doctest::Approx(0.5F));

    REQUIRE(state.apply_command(fade(2, 0x0000FF00U, -30), 118, 4));
    CHECK_EQ(state.mode(), 2U);
    CHECK_EQ(state.color(), 0x0000FF00U);
    CHECK_EQ(state.elapsed_seconds(), 0.0F);
    CHECK_EQ(state.duration_seconds(), 1.0F);
    CHECK_EQ(state.alpha(), 1.0F);
  }

  TEST_CASE("Stale and unsupported commands do not replace active state") {
    WorldFadeState state;
    REQUIRE(state.apply_command(fade(1, 0x00ABCDEFU, 30), 118, 4));
    state.update(0.25F);
    const float alpha{state.alpha()};

    CHECK_FALSE(state.apply_command(fade(2, 0x00000000U, 30), 119, 4));
    CHECK_FALSE(state.apply_command(fade(3, 0x00000000U, 30), 118, 4));
    CHECK_EQ(state.mode(), 1U);
    CHECK_EQ(state.color(), 0x00ABCDEFU);
    CHECK_EQ(state.alpha(), alpha);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

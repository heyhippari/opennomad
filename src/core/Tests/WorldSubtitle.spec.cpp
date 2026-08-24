#include <doctest/doctest.h>

#include "Core/WorldPresentation.hpp"

TEST_SUITE("Core::WorldSubtitleState") {
  TEST_CASE("accepts matching world commands and expires by recovered milliseconds") {
    App::WorldSubtitleState state;
    CHECK(state.apply_command(App::WorldSubtitleCommand{.scene_id = 5,
                                  .scene_generation = 9,
                                  .object_id = 141,
                                  .text = "short",
                                  .duration_ms = 2000U},
        5U,
        9U));
    CHECK(state.active());
    CHECK_EQ(state.text(), "short");
    state.update(1.5F);
    CHECK(state.active());
    CHECK(state.remaining_seconds() == doctest::Approx(0.5F));
    state.update(0.5F);
    CHECK_FALSE(state.active());
  }

  TEST_CASE("does not accept a stale world command") {
    App::WorldSubtitleState state;
    CHECK_FALSE(state.apply_command(App::WorldSubtitleCommand{.scene_id = 4,
                                        .scene_generation = 9,
                                        .object_id = 141,
                                        .text = "stale",
                                        .duration_ms = 2400U},
        5U,
        9U));
    CHECK_FALSE(state.active());
  }
}

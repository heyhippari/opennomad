#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include "Core/WorldPresentation.hpp"

namespace {

using App::WorldLetterboxCommand;
using App::WorldLetterboxState;
using App::WorldPresentationState;

}  // namespace

TEST_SUITE("Core::WorldLetterboxState") {
  TEST_CASE("Runtime cinematic mask scales 64 retail pixels by viewport height") {
    CHECK_EQ(WorldLetterboxState::target_bar_height(640.0F, 480.0F), doctest::Approx(64.0F));
    CHECK_EQ(WorldLetterboxState::target_bar_height(1920.0F, 1080.0F), doctest::Approx(144.0F));
    CHECK_EQ(WorldLetterboxState::target_bar_height(2560.0F, 1080.0F), doctest::Approx(144.0F));
    CHECK_EQ(WorldLetterboxState::target_bar_height(1850.0F, 1000.0F),
        doctest::Approx(133.333333F).epsilon(0.0001));
  }

  TEST_CASE("Begin and end use the confirmed two-second duration") {
    WorldLetterboxState state;
    state.set_enabled(true);
    CHECK_EQ(state.amount(), 0.0F);
    state.update(1.0F);
    CHECK(state.amount() == doctest::Approx(0.5F));
    state.update(1.0F);
    CHECK_EQ(state.amount(), 1.0F);
    CHECK_FALSE(state.transitioning());

    state.set_enabled(false);
    state.update(1.0F);
    CHECK(state.amount() == doctest::Approx(0.5F));
    state.update(1.0F);
    CHECK_EQ(state.amount(), 0.0F);
    CHECK_FALSE(state.transitioning());
  }

  TEST_CASE("A mid-transition reversal starts from the current amount") {
    WorldLetterboxState state;
    state.set_enabled(true);
    state.update(0.8F);
    REQUIRE(state.amount() == doctest::Approx(0.4F));

    state.set_enabled(false);
    CHECK(state.amount() == doctest::Approx(0.4F));
    state.update(0.1F);
    CHECK(state.amount() == doctest::Approx(0.38F));
  }

  TEST_CASE("Resize changes pixels without changing normalized transition state") {
    WorldLetterboxState state;
    state.set_enabled(true);
    state.update(1.0F);
    const float amount{state.amount()};
    const float first_height{state.current_bar_height(640.0F, 480.0F)};
    const float resized_height{state.current_bar_height(1920.0F, 1080.0F)};

    CHECK_EQ(state.amount(), amount);
    CHECK(first_height == doctest::Approx(32.0F).epsilon(0.0001));
    CHECK(resized_height == doctest::Approx(72.0F).epsilon(0.0001));
  }

  TEST_CASE("Commands are generation-safe and reset prevents world leakage") {
    WorldLetterboxState state;
    const WorldLetterboxCommand stale{
        .scene_id = 7U, .scene_generation = 2U, .enabled = true};
    CHECK_FALSE(state.apply_command(stale, 7U, 3U));
    CHECK_FALSE(state.requested());

    const WorldLetterboxCommand current{
        .scene_id = 7U, .scene_generation = 3U, .enabled = true};
    CHECK(state.apply_command(current, 7U, 3U));
    state.update(2.0F);
    REQUIRE_EQ(state.amount(), 1.0F);

    state.reset();
    CHECK_EQ(state.amount(), 0.0F);
    CHECK_FALSE(state.requested());
    CHECK_FALSE(state.transitioning());
  }

  TEST_CASE("The presentation mailbox queues and clears letterbox commands") {
    WorldPresentationState mailbox;
    mailbox.enqueue_letterbox(
        WorldLetterboxCommand{.scene_id = 4U, .scene_generation = 9U, .enabled = true});
    CHECK_EQ(mailbox.pending_letterbox_count(), 1U);
    REQUIRE(mailbox.take_letterbox().has_value());
    CHECK_EQ(mailbox.pending_letterbox_count(), 0U);

    mailbox.enqueue_letterbox(
        WorldLetterboxCommand{.scene_id = 4U, .scene_generation = 9U, .enabled = false});
    mailbox.clear();
    CHECK_EQ(mailbox.pending_letterbox_count(), 0U);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

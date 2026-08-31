#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include "Core/WorldPresentation.hpp"

namespace {

using App::WorldFadeState;
using App::WorldLetterboxCommand;
using App::WorldLetterboxState;
using App::WorldPresentationResetObserver;
using App::WorldPresentationState;

}  // namespace

TEST_SUITE("Core::WorldLetterboxState") {
  TEST_CASE("Cinematic mask leaves a 1.85 to 1 visible image") {
    CHECK(WorldLetterboxState::target_bar_height(640.0F, 480.0F) ==
        doctest::Approx(67.027027F).epsilon(0.0001));
    CHECK(WorldLetterboxState::target_bar_height(1920.0F, 1080.0F) ==
        doctest::Approx(21.081081F).epsilon(0.0001));
    CHECK_EQ(WorldLetterboxState::target_bar_height(1850.0F, 1000.0F), 0.0F);
    CHECK_EQ(WorldLetterboxState::target_bar_height(2560.0F, 1080.0F), 0.0F);
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
    CHECK(first_height == doctest::Approx(33.513514F).epsilon(0.0001));
    CHECK(resized_height == doctest::Approx(10.540541F).epsilon(0.0001));
  }

  TEST_CASE("Global commands persist until explicit disable or session reset") {
    WorldLetterboxState state;
    CHECK(state.apply_command(WorldLetterboxCommand{.enabled = true}));
    state.update(2.0F);
    REQUIRE_EQ(state.amount(), 1.0F);
    state.update(100.0F);
    CHECK_EQ(state.amount(), 1.0F);
    CHECK(state.requested());

    CHECK(state.apply_command(WorldLetterboxCommand{.enabled = false}));
    state.update(2.0F);
    CHECK_EQ(state.amount(), 0.0F);

    state.reset();
    CHECK_EQ(state.amount(), 0.0F);
    CHECK_FALSE(state.requested());
    CHECK_FALSE(state.transitioning());
  }

  TEST_CASE("The presentation mailbox queues and clears letterbox commands") {
    WorldPresentationState mailbox;
    CHECK_EQ(mailbox.reset_generation(), 0U);
    mailbox.enqueue_letterbox(WorldLetterboxCommand{.enabled = true});
    CHECK_EQ(mailbox.pending_letterbox_count(), 1U);
    REQUIRE(mailbox.take_letterbox().has_value());
    CHECK_EQ(mailbox.pending_letterbox_count(), 0U);
    CHECK_EQ(mailbox.reset_generation(), 0U);

    mailbox.enqueue_letterbox(WorldLetterboxCommand{.enabled = false});
    mailbox.clear();
    CHECK_EQ(mailbox.pending_letterbox_count(), 0U);
    CHECK_EQ(mailbox.reset_generation(), 1U);
    mailbox.clear();
    CHECK_EQ(mailbox.reset_generation(), 2U);
  }

  TEST_CASE("Only a session reset epoch clears global overlays") {
    WorldFadeState fade;
    WorldLetterboxState letterbox;
    WorldPresentationResetObserver observer;
    REQUIRE(
        fade.apply_command(App::WorldFadeCommand{.mode = 1U, .color = 0U, .duration_units = 30}));
    REQUIRE(letterbox.apply_command(WorldLetterboxCommand{.enabled = true}));
    fade.update(0.5F);
    letterbox.update(0.5F);
    const float fade_alpha{fade.alpha()};
    const float letterbox_amount{letterbox.amount()};

    // World identity bookkeeping cannot affect an API that observes only the
    // explicit session reset generation.
    CHECK_FALSE(observer.synchronize(0U, fade, letterbox));
    CHECK_EQ(fade.alpha(), fade_alpha);
    CHECK_EQ(letterbox.amount(), letterbox_amount);

    CHECK(observer.synchronize(1U, fade, letterbox));
    CHECK_EQ(observer.observed_generation(), 1U);
    CHECK_EQ(fade.mode(), 0U);
    CHECK_EQ(fade.alpha(), 0.0F);
    CHECK_EQ(letterbox.amount(), 0.0F);
    CHECK_FALSE(letterbox.requested());

    WorldPresentationState mailbox;
    mailbox.clear();
    mailbox.clear();
    mailbox.enqueue_letterbox(WorldLetterboxCommand{.enabled = true});
    REQUIRE(observer.synchronize(mailbox.reset_generation(), fade, letterbox));
    const auto fresh_command{mailbox.take_letterbox()};
    REQUIRE(fresh_command.has_value());
    CHECK(letterbox.apply_command(fresh_command.value_or(WorldLetterboxCommand{})));
    CHECK(letterbox.requested());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

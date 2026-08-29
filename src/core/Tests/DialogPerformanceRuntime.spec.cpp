#include "Core/Dialog/DialogPerformanceRuntime.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <optional>

TEST_CASE("dialogue performance clock selects authored 30 Hz held samples") {
  App::Dialog::DialogPerformanceClock clock;
  clock.start(4U);
  CHECK_EQ(clock.advance(0.010F), std::optional<std::size_t>{0U});
  CHECK_EQ(clock.advance(0.034F), std::optional<std::size_t>{1U});
  CHECK_EQ(clock.advance(0.068F), std::optional<std::size_t>{3U});
  CHECK(clock.active());
  CHECK_FALSE(clock.advance(0.022F).has_value());
  CHECK_FALSE(clock.active());
}

TEST_CASE("dialogue performance clock skips directly across dropped frames") {
  App::Dialog::DialogPerformanceClock clock;
  clock.start(10U);
  CHECK_EQ(clock.advance(0.010F), std::optional<std::size_t>{0U});
  CHECK_EQ(clock.advance(0.090F), std::optional<std::size_t>{3U});
}

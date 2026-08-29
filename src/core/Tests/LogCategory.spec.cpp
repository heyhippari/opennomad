#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <spdlog/common.h>

#include <cstdint>
#include <string>

#include "Core/Debug/LogFilter.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"

namespace {

using App::LogCategory;
using App::Debug::LogFilter;

}  // namespace

TEST_SUITE("Core::LogCategory") {
  TEST_CASE("every category has a stable, non-empty display name") {
    for (const LogCategory category : App::k_all_log_categories) {
      CHECK_FALSE(App::log_category_name(category).empty());
    }
  }

  TEST_CASE("display names are unique") {
    for (std::size_t i{0}; i < App::k_all_log_categories.size(); ++i) {
      for (std::size_t j{i + 1}; j < App::k_all_log_categories.size(); ++j) {
        CHECK(App::log_category_name(App::k_all_log_categories.at(i)) !=
              App::log_category_name(App::k_all_log_categories.at(j)));
      }
    }
  }

  TEST_CASE("category names round-trip through the reverse lookup") {
    for (const LogCategory category : App::k_all_log_categories) {
      CHECK_EQ(App::log_category_from_name(App::log_category_name(category)), category);
    }
  }

  TEST_CASE("unknown logger names resolve to Core") {
    CHECK_EQ(App::log_category_from_name("not-a-category"), LogCategory::Core);
    CHECK_EQ(App::log_category_from_name(""), LogCategory::Core);
  }
}

TEST_SUITE("Core::Debug::LogFilter") {
  TEST_CASE("severity threshold excludes lower levels") {
    LogFilter filter;
    filter.min_level = spdlog::level::warn;
    CHECK(filter.matches(spdlog::level::warn, LogCategory::Core, "x"));
    CHECK(filter.matches(spdlog::level::err, LogCategory::Core, "x"));
    CHECK_FALSE(filter.matches(spdlog::level::info, LogCategory::Core, "x"));
  }

  TEST_CASE("category mask selects a single category") {
    LogFilter filter;
    filter.category_mask = 1U << static_cast<std::uint32_t>(LogCategory::Music);
    CHECK(filter.matches(spdlog::level::info, LogCategory::Music, "x"));
    CHECK_FALSE(filter.matches(spdlog::level::info, LogCategory::Script, "x"));
  }

  TEST_CASE("text search is case-insensitive") {
    LogFilter filter;
    filter.text = "track 109";
    CHECK(filter.matches(spdlog::level::info, LogCategory::Music, "playing TRACK 109 — loop=true"));
    CHECK_FALSE(filter.matches(spdlog::level::info, LogCategory::Music, "playing track 87"));
  }
}

TEST_SUITE("Core::Startup::StartupTraceRecorder") {
  TEST_CASE("record stores events with sequence and detail without side effects") {
    App::Startup::StartupTraceRecorder recorder;
    recorder.record("ProcessBootstrap.Begin");
    recorder.record("ProcessBootstrap.Complete", "detail");
    recorder.record("Startup.Complete");

    REQUIRE_EQ(recorder.size(), 3U);
    REQUIRE_EQ(recorder.events().at(0).sequence, 0U);
    REQUIRE_EQ(recorder.events().at(0).name, "ProcessBootstrap.Begin");
    CHECK(recorder.events().at(0).detail.empty());
    REQUIRE_EQ(recorder.events().at(1).sequence, 1U);
    CHECK_EQ(recorder.events().at(1).detail, "detail");
    REQUIRE_EQ(recorder.events().at(2).sequence, 2U);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

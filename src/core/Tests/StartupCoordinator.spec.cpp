#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Startup/StartupCoordinator.hpp"
#include "Core/Startup/StartupPhase.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"

namespace {

using App::Startup::StartupCoordinator;
using App::Startup::StartupPhase;
using App::Startup::StartupPhaseStatus;
using App::Startup::StartupTraceRecorder;

std::vector<std::string> event_names(const StartupTraceRecorder& recorder) {
  std::vector<std::string> names;
  names.reserve(recorder.size());
  for (const auto& event : recorder.events()) {
    names.push_back(event.name);
  }
  return names;
}

}  // namespace

TEST_SUITE("Core::Startup::StartupCoordinator") {
  TEST_CASE("phases complete in order and finish records Startup.Complete") {
    StartupTraceRecorder recorder;
    StartupCoordinator coordinator{recorder};

    for (const StartupPhase phase : StartupCoordinator::ordered_phases()) {
      REQUIRE(coordinator.begin(phase).has_value());
      REQUIRE(coordinator.complete(phase, StartupPhaseStatus::k_complete).has_value());
    }
    REQUIRE(coordinator.finish().has_value());
    CHECK(coordinator.finished());

    const std::vector<std::string> names{event_names(recorder)};
    CHECK_EQ(names.size(), StartupCoordinator::k_phase_count * 2 + 1);
    CHECK(names.back() == "Startup.Complete");
    CHECK(names.front() == "ProcessBootstrap.Begin");
  }

  TEST_CASE("an out-of-order begin is rejected") {
    StartupTraceRecorder recorder;
    StartupCoordinator coordinator{recorder};

    const auto result{coordinator.begin(StartupPhase::k_initialize_core_systems)};
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("out of order") != std::string::npos);
  }

  TEST_CASE("completing without a running phase is rejected") {
    StartupTraceRecorder recorder;
    StartupCoordinator coordinator{recorder};

    const auto result{coordinator.complete(
        StartupPhase::k_process_bootstrap, StartupPhaseStatus::k_complete)};
    REQUIRE_FALSE(result.has_value());
  }

  TEST_CASE("a failed phase stops the sequence") {
    StartupTraceRecorder recorder;
    StartupCoordinator coordinator{recorder};

    REQUIRE(coordinator.begin(StartupPhase::k_process_bootstrap).has_value());
    REQUIRE(coordinator.complete(
        StartupPhase::k_process_bootstrap, StartupPhaseStatus::k_failed)
                .has_value());
    CHECK(coordinator.finished());
    CHECK_FALSE(coordinator.begin(StartupPhase::k_create_windows).has_value());
  }

  TEST_CASE("a skipped phase records its status in place and advances") {
    StartupTraceRecorder recorder;
    StartupCoordinator coordinator{recorder};

    REQUIRE(coordinator.begin(StartupPhase::k_process_bootstrap).has_value());
    REQUIRE(coordinator.complete(
        StartupPhase::k_process_bootstrap, StartupPhaseStatus::k_complete)
                .has_value());

    REQUIRE(coordinator.begin(StartupPhase::k_create_windows).has_value());
    REQUIRE(coordinator.complete(
        StartupPhase::k_create_windows, StartupPhaseStatus::k_skipped_by_configuration)
                .has_value());

    CHECK(recorder.first_sequence_of("CreateWindows.SkippedByConfiguration").has_value());
    CHECK_FALSE(coordinator.finished());
  }

  TEST_CASE("finish before all phases is rejected") {
    StartupTraceRecorder recorder;
    StartupCoordinator coordinator{recorder};

    REQUIRE_FALSE(coordinator.finish().has_value());
  }

  TEST_CASE("trace recorder assigns increasing sequence numbers") {
    StartupTraceRecorder recorder;
    recorder.record("A");
    recorder.record("B", "detail");
    recorder.record("A");

    REQUIRE_EQ(recorder.size(), 3U);
    CHECK_EQ(recorder.first_sequence_of("A"), std::optional<std::uint32_t>{0});
    CHECK_EQ(recorder.first_sequence_of("A", 1), std::optional<std::uint32_t>{2});
    CHECK_EQ(recorder.first_sequence_of("B"), std::optional<std::uint32_t>{1});
    CHECK_FALSE(recorder.first_sequence_of("C").has_value());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

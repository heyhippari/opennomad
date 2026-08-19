#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "Core/Startup/StartupMediaPolicy.hpp"
#include "Core/Startup/StartupPhase.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"
#include "Core/Video/StartupVideoSequence.hpp"
#include "Core/Video/VideoPlayer.hpp"

namespace {

using App::Startup::StartupMediaPolicy;
using App::Startup::StartupPhaseStatus;
using App::Startup::StartupTraceRecorder;
using App::Startup::StartupVideoSlot;
using App::Video::StartupVideoSequence;
using App::Video::VideoFrame;
using App::Video::VideoPresenter;

class StubPresenter final : public VideoPresenter {
 public:
  void present(const VideoFrame& /*frame*/) override {}
};

}  // namespace

TEST_SUITE("Core::Video::StartupVideoSequence") {
  TEST_CASE("a disabled media policy skips all three videos in order") {
    StartupTraceRecorder recorder;
    StartupMediaPolicy policy;
    policy.videos_enabled = false;
    StartupVideoSequence sequence{recorder, policy};
    StubPresenter presenter;

    const StartupPhaseStatus publisher{
        sequence.play_slot(StartupVideoSlot::k_publisher, presenter, [] { return false; })};
    const StartupPhaseStatus developer{
        sequence.play_slot(StartupVideoSlot::k_developer, presenter, [] { return false; })};
    const StartupPhaseStatus intro{
        sequence.play_slot(StartupVideoSlot::k_intro, presenter, [] { return false; })};

    CHECK(publisher == StartupPhaseStatus::k_skipped_by_configuration);
    CHECK(developer == StartupPhaseStatus::k_skipped_by_configuration);
    CHECK(intro == StartupPhaseStatus::k_skipped_by_configuration);

    const std::optional<std::uint32_t> eidos{
        recorder.first_sequence_of("StartupVideo.Eidos.SkippedByConfiguration")};
    const std::optional<std::uint32_t> quantic{
        recorder.first_sequence_of("StartupVideo.QuanticDream.SkippedByConfiguration")};
    const std::optional<std::uint32_t> game{
        recorder.first_sequence_of("StartupVideo.GameIntro.SkippedByConfiguration")};

    REQUIRE(eidos.has_value());
    REQUIRE(quantic.has_value());
    REQUIRE(game.has_value());
    CHECK(eidos.value() < quantic.value());
    CHECK(quantic.value() < game.value());
  }

  TEST_CASE("missing video files are skipped as unavailable without reordering") {
    StartupTraceRecorder recorder;
    StartupMediaPolicy policy;
    policy.videos_enabled = true;
    policy.video_paths = std::array<std::string, 3>{
        "FLIS/DOES_NOT_EXIST_1.mpg",
        "FLIS/DOES_NOT_EXIST_2.mpg",
        "FLIS/DOES_NOT_EXIST_3.mpg"};
    StartupVideoSequence sequence{recorder, policy};
    StubPresenter presenter;

    sequence.play_all(presenter, [] { return false; });

    const std::optional<std::uint32_t> eidos{
        recorder.first_sequence_of("StartupVideo.Eidos.SkippedUnavailable")};
    const std::optional<std::uint32_t> quantic{
        recorder.first_sequence_of("StartupVideo.QuanticDream.SkippedUnavailable")};
    const std::optional<std::uint32_t> game{
        recorder.first_sequence_of("StartupVideo.GameIntro.SkippedUnavailable")};

    REQUIRE(eidos.has_value());
    REQUIRE(quantic.has_value());
    REQUIRE(game.has_value());
    CHECK(eidos.value() < quantic.value());
    CHECK(quantic.value() < game.value());
    // The disabled/skipped statuses never renumber later work: no
    // Startup.Complete marker is emitted by the video sequence itself.
    CHECK_FALSE(recorder.first_sequence_of("Startup.Complete").has_value());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

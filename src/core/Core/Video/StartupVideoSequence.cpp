#include "Core/Video/StartupVideoSequence.hpp"

#include <SDL3/SDL_timer.h>

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"
#include "Core/Startup/StartupMediaPolicy.hpp"
#include "Core/Startup/StartupPhase.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"
#include "Core/Video/VideoPlayer.hpp"

namespace {

/// Number of leading frames per video logged at info level during diagnosis.
constexpr std::size_t K_DIAGNOSTIC_FRAMES{30};

}  // namespace

namespace App::Video {

StartupVideoSequence::StartupVideoSequence(
    Startup::StartupTraceRecorder& recorder, Startup::StartupMediaPolicy policy)
    : m_recorder(recorder), m_policy(std::move(policy)) {}

Startup::StartupPhaseStatus StartupVideoSequence::play_slot(
    const Startup::StartupVideoSlot slot,
    VideoPresenter& presenter,
    const std::function<bool()>& should_stop) {
  APP_PROFILE_FUNCTION();

  const std::string_view base{Startup::startup_video_event_base(slot)};
  if (!m_policy.videos_enabled) {
    m_recorder.record(fmt::format("{}.SkippedByConfiguration", base));
    return Startup::StartupPhaseStatus::k_skipped_by_configuration;
  }

  const std::string& path{m_policy.video_paths.at(static_cast<std::size_t>(slot))};
  if (auto result{m_player.open(path)}; !result) {
    App::Log::info("Startup video unavailable: {}", result.error());
    m_recorder.record(fmt::format("{}.SkippedUnavailable", base));
    return Startup::StartupPhaseStatus::k_skipped_unavailable;
  }

  m_player.start_audio();

  VideoDecodeStatus status{VideoDecodeStatus::k_frame};
  bool stop{false};
  std::uint64_t frame_index{0};
  while (!stop) {
    if (should_stop()) {
      stop = true;
      continue;
    }

    VideoFrame frame;
    status = m_player.next_video_frame(frame);
    if (status == VideoDecodeStatus::k_eof || status == VideoDecodeStatus::k_error) {
      break;
    }

    // Audio-clock sync: wait until the frame's presentation time arrives.
    const double delay{frame.pts_seconds - m_player.clock_seconds()};
    if (frame_index < K_DIAGNOSTIC_FRAMES) {
      App::Log::info(
          "Video[{}] frame {}: pts={:.3f}s clock={:.3f}s delay={:.3f}s audio_queued={}",
          base,
          frame_index,
          frame.pts_seconds,
          m_player.clock_seconds(),
          delay,
          m_player.audio_queued_bytes());
    }
    ++frame_index;

    if (delay > 0.0) {
      APP_PROFILE_SCOPE("VideoWait");

      constexpr double k_step_seconds{0.005};
      const std::uint64_t total_steps{static_cast<std::uint64_t>(delay / k_step_seconds) + 1U};
      for (std::uint64_t step{0}; step < total_steps; ++step) {
        SDL_Delay(5);
        if (should_stop()) {
          stop = true;
          break;
        }
      }
      if (stop) {
        continue;
      }
    }

    presenter.present(frame);
  }

  m_player.stop_audio();

  if (status == VideoDecodeStatus::k_error) {
    m_recorder.record(fmt::format("{}.SkippedUnavailable", base));
    return Startup::StartupPhaseStatus::k_skipped_unavailable;
  }
  m_recorder.record(fmt::format("{}.Complete", base));
  return Startup::StartupPhaseStatus::k_complete;
}

void StartupVideoSequence::play_all(
    VideoPresenter& presenter, const std::function<bool()>& should_stop) {
  static_cast<void>(play_slot(Startup::StartupVideoSlot::k_publisher, presenter, should_stop));
  static_cast<void>(play_slot(Startup::StartupVideoSlot::k_developer, presenter, should_stop));
  static_cast<void>(play_slot(Startup::StartupVideoSlot::k_intro, presenter, should_stop));
}

}  // namespace App::Video

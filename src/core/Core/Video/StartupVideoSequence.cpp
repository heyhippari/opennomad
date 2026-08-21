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
#include "Core/LogCategory.hpp"
#include "Core/Startup/StartupMediaPolicy.hpp"
#include "Core/Startup/StartupPhase.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"
#include "Core/Video/VideoPlayer.hpp"

namespace {

/// Human-oriented display name of a startup video slot.
constexpr std::string_view startup_video_display_name(
    const App::Startup::StartupVideoSlot slot) {
  switch (slot) {
    case App::Startup::StartupVideoSlot::k_publisher: return "Eidos";
    case App::Startup::StartupVideoSlot::k_developer: return "Quantic Dream";
    case App::Startup::StartupVideoSlot::k_intro:     return "game intro";
  }
  return "unknown";
}

}  // namespace

namespace App::Video {

VideoOpenOptions startup_video_open_options(const Startup::StartupVideoSlot slot) {
  // GAME.MPG is 320x240 with symmetrical 34-pixel encoded letterbox bars.
  // Keep this fixed rectangle close to the per-slot policy so it is easy to
  // adjust if comparison against the original asset exposes codec-edge noise.
  static constexpr VideoCrop k_intro_crop{.x = 0, .y = 34, .width = 320, .height = 172};

  if (slot == Startup::StartupVideoSlot::k_intro) {
    return VideoOpenOptions{.crop = k_intro_crop};
  }
  return {};
}

StartupVideoSequence::StartupVideoSequence(
    Startup::StartupTraceRecorder& recorder, Startup::StartupMediaPolicy policy)
    : m_recorder(recorder),
      m_policy(std::move(policy)) {}

Startup::StartupPhaseStatus StartupVideoSequence::play_slot(const Startup::StartupVideoSlot slot,
    VideoPresenter& presenter,
    const std::function<bool()>& should_stop) {
  APP_PROFILE_FUNCTION();

  const std::string_view base{Startup::startup_video_event_base(slot)};
  const std::string_view display{startup_video_display_name(slot)};

  if (!m_policy.videos_enabled) {
    App::Log::info(LogCategory::Video, "{} startup video disabled by configuration", display);
    m_recorder.record(fmt::format("{}.SkippedByConfiguration", base));
    return Startup::StartupPhaseStatus::k_skipped_by_configuration;
  }

  const std::string& path{m_policy.video_paths.at(static_cast<std::size_t>(slot))};
  if (auto result{m_player.open(path, startup_video_open_options(slot))}; !result) {
    App::Log::info(
        LogCategory::Video, "{} startup video unavailable — {}", display, result.error());
    m_recorder.record(fmt::format("{}.SkippedUnavailable", base));
    return Startup::StartupPhaseStatus::k_skipped_unavailable;
  }

  m_player.start_audio();
  App::Log::info(LogCategory::Video, "playing {} startup video", display);

  VideoDecodeStatus status{VideoDecodeStatus::k_frame};
  bool stopped_by_user{false};
  std::uint64_t frame_index{0};
  while (!stopped_by_user) {
    if (should_stop()) {
      stopped_by_user = true;
      continue;
    }

    VideoFrame frame;
    status = m_player.next_video_frame(frame);
    if (status == VideoDecodeStatus::k_eof || status == VideoDecodeStatus::k_error) {
      break;
    }

    // Audio-clock sync: wait until the frame's presentation time arrives.
    // The per-frame PTS/clock samples are high-volume forensic detail kept
    // at trace level, which is disabled by default.
    const double delay{frame.pts_seconds - m_player.clock_seconds()};
    App::Log::trace(LogCategory::Video,
        "{} frame {}: pts={:.3f}s clock={:.3f}s delay={:.3f}s audio_queued={}",
        base,
        frame_index,
        frame.pts_seconds,
        m_player.clock_seconds(),
        delay,
        m_player.audio_queued_bytes());
    ++frame_index;

    if (delay > 0.0) {
      APP_PROFILE_SCOPE("VideoWait");

      constexpr double k_step_seconds{0.005};
      const std::uint64_t total_steps{static_cast<std::uint64_t>(delay / k_step_seconds) + 1U};
      for (std::uint64_t step{0}; step < total_steps; ++step) {
        SDL_Delay(5);
        if (should_stop()) {
          stopped_by_user = true;
          break;
        }
      }
      if (stopped_by_user) {
        continue;
      }
    }

    presenter.present(frame);
  }

  m_player.stop_audio();

  if (status == VideoDecodeStatus::k_error) {
    App::Log::info(
        LogCategory::Video, "{} startup video unavailable — decode error", display);
    m_recorder.record(fmt::format("{}.SkippedUnavailable", base));
    return Startup::StartupPhaseStatus::k_skipped_unavailable;
  }
  if (stopped_by_user) {
    App::Log::info(LogCategory::Video, "{} startup video skipped", display);
    m_recorder.record(fmt::format("{}.SkippedByUser", base));
    return Startup::StartupPhaseStatus::k_skipped_by_user;
  }
  App::Log::info(LogCategory::Video, "{} startup video complete", display);
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

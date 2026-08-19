#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "Core/Startup/StartupMediaPolicy.hpp"
#include "Core/Startup/StartupPhase.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"
#include "Core/Video/VideoPlayer.hpp"

namespace App::Video {

/// Presentation hook driven by the application while a video plays.
class VideoPresenter {
 public:
  virtual ~VideoPresenter() = default;

  /// Uploads and renders one decoded frame.
  virtual void present(const VideoFrame& frame) = 0;
};

/// Plays the three startup videos in order, recording one trace event per
/// video. Video files are optional presentation: a missing or undecodable
/// file produces SkippedUnavailable, never a startup failure.
class StartupVideoSequence {
 public:
  StartupVideoSequence(Startup::StartupTraceRecorder& recorder,
      Startup::StartupMediaPolicy policy);

  /// Plays one video slot and records its trace event.
  ///
  /// The caller supplies `should_stop`: called before each decode and while
  /// waiting for a frame's presentation time, it returns true to skip the
  /// video. It is expected to pump platform events and resolve the skip
  /// action through the input manager — the video sequence itself performs
  /// no event processing and therefore never consumes input.
  [[nodiscard]] Startup::StartupPhaseStatus play_slot(Startup::StartupVideoSlot slot,
      VideoPresenter& presenter,
      const std::function<bool()>& should_stop);

  /// Plays all three slots in order.
  void play_all(VideoPresenter& presenter, const std::function<bool()>& should_stop);

 private:
  Startup::StartupTraceRecorder& m_recorder;
  Startup::StartupMediaPolicy m_policy;
  VideoPlayer m_player;
};

}  // namespace App::Video

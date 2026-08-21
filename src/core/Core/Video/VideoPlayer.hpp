#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace App::Video {

/// One decoded video frame in tightly packed RGBA8.
struct VideoFrame {
  std::vector<std::uint8_t> rgba;
  int width{0};
  int height{0};
  double pts_seconds{0.0};
};

/// Fixed source-frame rectangle selected before YUV-to-RGB conversion.
struct VideoCrop {
  int x{0};
  int y{0};
  int width{0};
  int height{0};

  bool operator==(const VideoCrop&) const = default;
};

/// Per-movie decoding and presentation hints.
struct VideoOpenOptions {
  std::optional<VideoCrop> crop;
};

/// Result of one video decode step.
enum class VideoDecodeStatus : std::uint8_t {
  k_frame,  ///< A video frame was decoded into `frame`.
  k_eof,    ///< End of the stream was reached.
  k_error,  ///< Decoding failed.
};

/// MPEG (and other ffmpeg-supported) decoder with SDL3 audio output.
///
/// Presentation is driven externally: next_video_frame() decodes one video
/// frame while feeding any decoded audio into the SDL audio stream. The
/// audio stream is the playback master clock; a wall clock is used when the
/// file has no audio.
class VideoPlayer {
 public:
  VideoPlayer();
  ~VideoPlayer();

  VideoPlayer(const VideoPlayer&) = delete;
  VideoPlayer(VideoPlayer&&) = delete;
  VideoPlayer& operator=(const VideoPlayer&) = delete;
  VideoPlayer& operator=(VideoPlayer&&) = delete;

  /// Opens and prepares the file. The path is resolved case-insensitively
  /// against the game-data root.
  [[nodiscard]] std::expected<void, std::string> open(
      const std::string& path, const VideoOpenOptions& options = {});

  /// Opens the SDL audio stream (no-op when the file has no audio stream).
  void start_audio();

  /// Closes the SDL audio stream.
  void stop_audio();

  /// Decodes the next video frame into `frame`. Any audio decoded along the
  /// way is queued to the audio stream.
  [[nodiscard]] VideoDecodeStatus next_video_frame(VideoFrame& frame);

  /// The playback master clock in seconds: a wall clock anchored to the first
  /// video frame's PTS (ffplay's external-clock model). It advances in real
  /// time independently of the audio device so a single-threaded
  /// decode/present loop never stalls waiting for audio it cannot feed.
  [[nodiscard]] double clock_seconds() const;

  /// Bytes currently queued in the SDL audio stream (-1 when no audio stream).
  /// Diagnostic-only: used to confirm whether the audio master clock advances.
  [[nodiscard]] std::int64_t audio_queued_bytes() const;

  [[nodiscard]] bool has_video() const;
  [[nodiscard]] bool has_audio() const;

 private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace App::Video

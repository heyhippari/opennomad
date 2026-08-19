#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL_audio.h>
#include <SDL3_mixer/SDL_mixer.h>

#include "Core/Audio/AudioTypes.hpp"

namespace App::Audio {

/// One streamed music source: a caller-created, seekable SDL IO stream.
/// The MusicPlayer assigns it to its music track with `closeio = true`, so
/// SDL_mixer owns cleanup exactly once (on replacement or track destruction).
struct MusicSource {
  std::string display_name;
  SDL_IOStream* io{nullptr};
};

/// One fully decoded raw-PCM music source. The MusicPlayer takes ownership of
/// `samples` and keeps the buffer alive for as long as SDL3_mixer may read it
/// (the mixer input is an IOStream pointing into this buffer).
struct RawPcmMusicSource {
  std::string display_name;
  SDL_AudioSpec spec{};
  std::vector<std::int16_t> samples;
};

/// Playback options for the generic streamed-music foundation. This is NOT
/// wired to speculative original Omikron music opcodes.
struct MusicPlayOptions {
  bool loop{false};
  std::optional<std::int64_t> loop_start_ms{};
  std::int64_t fade_in_ms{0};
};

/// Generic streamed-music player on one dedicated, `"music"`-tagged track.
/// Supports seekable SDL IO streams, looping, loop-start and fade-in through
/// documented `MIX_PlayTrack` properties.
class MusicPlayer {
 public:
  /// Default-constructs an unattached player (no mixer, no track). Call
  /// `attach` once a mixer is available.
  MusicPlayer() = default;

  /// Creates the music track for `mixer` immediately.
  explicit MusicPlayer(MIX_Mixer* mixer);

  MusicPlayer(const MusicPlayer&) = delete;
  MusicPlayer(MusicPlayer&&) = delete;
  MusicPlayer& operator=(const MusicPlayer&) = delete;
  MusicPlayer& operator=(MusicPlayer&&) = delete;
  ~MusicPlayer();

  /// Creates and tags the music track for `mixer` (idempotent).
  void attach(MIX_Mixer* mixer);

  /// Stops and destroys the music track (safe to call before the mixer is
  /// destroyed; the destructor is then a no-op).
  void shutdown();

  /// Plays (or replaces) a music source. Returns false on failure (including
  /// nonseekable sources requested with looping or a loop start).
  [[nodiscard]] bool play(MusicSource source, const MusicPlayOptions& options);

  /// Plays (or replaces) a fully decoded raw-PCM source. The samples are
  /// owned here and fed to SDL3_mixer as a raw PCM IOStream; the previous
  /// source's storage is released only after the mixer has dropped its old
  /// input, so track replacement cannot touch freed memory.
  [[nodiscard]] bool play_raw_pcm(RawPcmMusicSource source, const MusicPlayOptions& options);

  /// Stops music, fading out over `fade_out_ms` (converted to input frames).
  void stop(std::int64_t fade_out_ms = 0);
  void pause();
  void resume();

  [[nodiscard]] bool is_playing() const;
  [[nodiscard]] bool is_paused() const;

  void set_gain(float gain);

  /// Current playback position in milliseconds (or 0 when unknown).
  [[nodiscard]] std::int64_t playback_position_ms() const;

  [[nodiscard]] MusicDebugInfo debug_info() const;

 private:
  MIX_Mixer* m_mixer{nullptr};
  MIX_Track* m_track{nullptr};
  std::string m_source_name;
  bool m_loop{false};
  std::int64_t m_loop_start_ms{0};
  std::int64_t m_fade_in_ms{0};
  float m_gain{1.0F};

  /// Owned raw PCM storage for the current source (valid while the mixer may
  /// read the IOStream pointing into it).
  std::vector<std::int16_t> m_raw_samples;
  SDL_AudioSpec m_raw_spec{};
  bool m_raw_active{false};
};

}  // namespace App::Audio

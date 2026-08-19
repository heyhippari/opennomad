#include "Core/Audio/MusicPlayer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3_mixer/SDL_mixer.h>

#include "Core/Audio/AudioTypes.hpp"
#include "Core/Log.hpp"

namespace App::Audio {

MusicPlayer::MusicPlayer(MIX_Mixer* mixer) {
  attach(mixer);
}

void MusicPlayer::attach(MIX_Mixer* mixer) {
  if (m_track != nullptr || mixer == nullptr) {
    return;
  }
  m_mixer = mixer;
  m_track = MIX_CreateTrack(mixer);
  if (m_track != nullptr) {
    if (!MIX_TagTrack(m_track, "music")) {
      App::Log::warn("Audio: failed to tag music track: {}", SDL_GetError());
    }
  }
}

MusicPlayer::~MusicPlayer() { shutdown(); }

void MusicPlayer::shutdown() {
  if (m_track != nullptr) {
    MIX_DestroyTrack(m_track);
    m_track = nullptr;
  }
  m_mixer = nullptr;
}

bool MusicPlayer::play(MusicSource source, const MusicPlayOptions& options) {
  if (m_mixer == nullptr || m_track == nullptr || source.io == nullptr) {
    App::Log::error("Audio: music play unavailable (mixer/track/source missing)");
    return false;
  }

  // Looping or a loop start require seeking; a nonseekable stream cannot
  // support either and must be diagnosed rather than silently misplayed.
  const bool needs_seek{options.loop || options.loop_start_ms.has_value()};
  if (needs_seek) {
    const Sint64 position{SDL_SeekIO(source.io, 0, SDL_IO_SEEK_CUR)};
    if (position < 0) {
      App::Log::error(
          "Audio: music source '{}' is not seekable but looping/loop-start was requested",
          source.display_name);
      return false;
    }
  }

  if (!MIX_SetTrackIOStream(m_track, source.io, /*closeio=*/true)) {
    App::Log::error("Audio: MIX_SetTrackIOStream failed for '{}': {}",
        source.display_name,
        SDL_GetError());
    return false;
  }

  const SDL_PropertiesID props{SDL_CreateProperties()};
  if (options.loop) {
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
  } else {
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, 0);
  }
  if (options.loop_start_ms.has_value()) {
    SDL_SetNumberProperty(
        props, MIX_PROP_PLAY_LOOP_START_MILLISECOND_NUMBER, options.loop_start_ms.value());
  }
  if (options.fade_in_ms > 0) {
    SDL_SetNumberProperty(
        props, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, options.fade_in_ms);
  }

  const bool started{MIX_PlayTrack(m_track, props)};
  SDL_DestroyProperties(props);
  if (!started) {
    App::Log::error("Audio: MIX_PlayTrack failed for '{}': {}",
        source.display_name,
        SDL_GetError());
    return false;
  }

  m_source_name = source.display_name;
  m_loop = options.loop;
  m_loop_start_ms = options.loop_start_ms.value_or(0);
  m_fade_in_ms = options.fade_in_ms;
  App::Log::info("Audio: music '{}' playing (loop {}, fade-in {} ms)",
      m_source_name,
      m_loop,
      m_fade_in_ms);
  return true;
}

bool MusicPlayer::play_raw_pcm(RawPcmMusicSource source, const MusicPlayOptions& options) {
  if (m_mixer == nullptr || m_track == nullptr) {
    App::Log::error("Audio: music play unavailable (mixer/track missing)");
    return false;
  }
  if (source.samples.empty()) {
    App::Log::error("Audio: raw PCM music source '{}' is empty", source.display_name);
    return false;
  }

  // Build the new IOStream from the caller's buffer BEFORE replacing the
  // track input. Only after MIX_SetTrackRawIOStream has synchronously closed
  // the previous input is it safe to release the old PCM storage.
  const std::size_t byte_size{source.samples.size() * sizeof(std::int16_t)};
  SDL_IOStream* io{SDL_IOFromConstMem(source.samples.data(), byte_size)};
  if (io == nullptr) {
    App::Log::error("Audio: SDL_IOFromConstMem failed for '{}': {}",
        source.display_name,
        SDL_GetError());
    return false;
  }

  // Stop playback and replace the input; this closes the previous IOStream.
  MIX_StopTrack(m_track, 0);
  if (!MIX_SetTrackRawIOStream(m_track, io, &source.spec, /*closeio=*/true)) {
    App::Log::error("Audio: MIX_SetTrackRawIOStream failed for '{}': {}",
        source.display_name,
        SDL_GetError());
    SDL_CloseIO(io);
    return false;
  }

  // The old input is gone: it is now safe to replace the owned PCM storage.
  // std::vector move keeps the buffer pointer, so `io` still points at valid
  // memory owned here.
  m_raw_samples = std::move(source.samples);
  m_raw_spec = source.spec;
  m_raw_active = true;
  m_source_name = std::move(source.display_name);

  const SDL_PropertiesID props{SDL_CreateProperties()};
  if (options.loop) {
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
  } else {
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, 0);
  }
  if (options.loop_start_ms.has_value()) {
    SDL_SetNumberProperty(
        props, MIX_PROP_PLAY_LOOP_START_MILLISECOND_NUMBER, options.loop_start_ms.value());
  }
  if (options.fade_in_ms > 0) {
    SDL_SetNumberProperty(
        props, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, options.fade_in_ms);
  }

  const bool started{MIX_PlayTrack(m_track, props)};
  SDL_DestroyProperties(props);
  if (!started) {
    App::Log::error("Audio: MIX_PlayTrack failed for '{}': {}",
        m_source_name,
        SDL_GetError());
    return false;
  }

  m_loop = options.loop;
  m_loop_start_ms = options.loop_start_ms.value_or(0);
  m_fade_in_ms = options.fade_in_ms;
  App::Log::info("Audio: music '{}' playing raw PCM (loop {}, {} ch, {} Hz, {} frames)",
      m_source_name,
      m_loop,
      m_raw_spec.channels,
      m_raw_spec.freq,
      m_raw_samples.size() / static_cast<std::size_t>(m_raw_spec.channels));
  return true;
}

void MusicPlayer::stop(const std::int64_t fade_out_ms) {
  if (m_track == nullptr) {
    return;
  }
  Sint64 frames{0};
  if (fade_out_ms > 0) {
    const Sint64 converted{MIX_TrackMSToFrames(m_track, fade_out_ms)};
    if (converted > 0) {
      frames = converted;
    }
  }
  if (!MIX_StopTrack(m_track, frames)) {
    App::Log::warn("Audio: music stop failed: {}", SDL_GetError());
  }
}

void MusicPlayer::pause() {
  if (m_track != nullptr) {
    MIX_PauseTrack(m_track);
  }
}

void MusicPlayer::resume() {
  if (m_track != nullptr) {
    MIX_ResumeTrack(m_track);
  }
}

bool MusicPlayer::is_playing() const {
  return m_track != nullptr && MIX_TrackPlaying(m_track);
}

bool MusicPlayer::is_paused() const {
  return m_track != nullptr && MIX_TrackPaused(m_track);
}

void MusicPlayer::set_gain(const float gain) {
  m_gain = gain;
  if (m_track != nullptr) {
    MIX_SetTrackGain(m_track, gain);
  }
}

std::int64_t MusicPlayer::playback_position_ms() const {
  if (m_track == nullptr) {
    return 0;
  }
  const Sint64 frames{MIX_GetTrackPlaybackPosition(m_track)};
  if (frames < 0) {
    return 0;
  }
  const Sint64 ms{MIX_TrackFramesToMS(m_track, frames)};
  return ms < 0 ? 0 : ms;
}

MusicDebugInfo MusicPlayer::debug_info() const {
  MusicDebugInfo info;
  info.source_name = m_source_name;
  info.playing = is_playing();
  info.paused = is_paused();
  info.loop = m_loop;
  info.loop_start_ms = m_loop_start_ms;
  info.playback_position_ms = playback_position_ms();
  info.gain = m_gain;
  if (m_raw_active) {
    info.channels = m_raw_spec.channels;
    info.sample_rate = m_raw_spec.freq;
    if (m_raw_spec.channels > 0) {
      info.total_frames = static_cast<std::uint64_t>(
          m_raw_samples.size() / static_cast<std::size_t>(m_raw_spec.channels));
    }
  }
  if (m_track != nullptr) {
    const Sint64 remaining{MIX_GetTrackRemaining(m_track)};
    if (remaining >= 0) {
      const Sint64 remaining_ms{MIX_TrackFramesToMS(m_track, remaining)};
      if (remaining_ms >= 0) {
        info.duration_ms = playback_position_ms() + remaining_ms;
      }
    }
  }
  return info;
}

}  // namespace App::Audio

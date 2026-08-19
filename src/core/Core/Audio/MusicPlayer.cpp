#include "Core/Audio/MusicPlayer.hpp"

#include <cstdint>
#include <string>

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

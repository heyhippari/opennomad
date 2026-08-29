#include "Core/Audio/DialogVoicePlayer.hpp"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"

namespace App::Audio {

DialogVoicePlayer::~DialogVoicePlayer() {
  shutdown();
}

void DialogVoicePlayer::attach(MIX_Mixer* mixer) {
  if (m_track != nullptr || mixer == nullptr) {
    return;
  }
  m_mixer = mixer;
  m_track = MIX_CreateTrack(mixer);
  if (m_track != nullptr && !MIX_TagTrack(m_track, "dialog")) {
    App::Log::warn(LogCategory::Audio, "failed to tag dialogue voice track: {}", SDL_GetError());
  }
  set_gain(m_gain);
}

void DialogVoicePlayer::shutdown() {
  if (m_track != nullptr) {
    MIX_DestroyTrack(m_track);
    m_track = nullptr;
  }
  m_mixer = nullptr;
  m_samples.reset();
}

bool DialogVoicePlayer::play(std::string display_name, DialogVoiceSamples stereo_samples) {
  if (m_mixer == nullptr || m_track == nullptr || stereo_samples == nullptr ||
      stereo_samples->empty()) {
    return false;
  }
  const std::size_t byte_size{stereo_samples->size() * sizeof(std::int16_t)};

  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_S16;
  spec.channels = 2;
  spec.freq = 22080;
  // The PCM is already completely decoded and resident. Wrap it directly in
  // MIX_Audio instead of making SDL_mixer stream it through an SDL_IOStream.
  // No copy is made; m_samples below keeps the backing allocation alive.
  MIX_Audio* input{MIX_LoadRawAudioNoCopy(m_mixer,
      stereo_samples->data(),
      byte_size,
      &spec,
      /*free_when_done=*/false)};
  if (input == nullptr) {
    return false;
  }

  // Replacing a track's MIX_Audio while it is playing is explicitly legal.
  // Do not MIX_StopTrack() first: MIX_PlayTrack() below restarts the track
  // against the new input, avoiding an unnecessary stop/start hole.
  if (!MIX_SetTrackAudio(m_track, input)) {
    MIX_DestroyAudio(input);
    return false;
  }

  // MIX_SetTrackAudio() takes its own reference.
  MIX_DestroyAudio(input);

  // Keep the no-copy PCM backing alive for as long as the track can reference
  // this MIX_Audio.
  m_samples = std::move(stereo_samples);
  m_source_name = std::move(display_name);

  // Default MIX_PlayTrack options already mean zero loops.
  const bool started{MIX_PlayTrack(m_track, 0)};

  if (!started) {
    App::Log::warn(
        LogCategory::Audio, "dialogue voice '{}' failed: {}", m_source_name, SDL_GetError());
  }
  return started;
}

void DialogVoicePlayer::stop() {
  if (m_track != nullptr) {
    MIX_StopTrack(m_track, 0);
    MIX_SetTrackAudio(m_track, nullptr);
  }
  m_samples.reset();
}

void DialogVoicePlayer::set_gain(const float gain) {
  m_gain = gain;
  if (m_track != nullptr) {
    MIX_SetTrackGain(m_track, m_gain);
  }
}

bool DialogVoicePlayer::is_playing() const {
  return m_track != nullptr && MIX_TrackPlaying(m_track);
}

}  // namespace App::Audio

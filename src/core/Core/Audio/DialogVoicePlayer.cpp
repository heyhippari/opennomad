#include "Core/Audio/DialogVoicePlayer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>
#include <SDL3_mixer/SDL_mixer.h>

#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"

namespace App::Audio {

DialogVoicePlayer::~DialogVoicePlayer() { shutdown(); }

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
  m_samples.clear();
}

bool DialogVoicePlayer::play(
    std::string display_name, std::vector<std::int16_t> stereo_samples) {
  if (m_mixer == nullptr || m_track == nullptr || stereo_samples.empty()) {
    return false;
  }
  const std::size_t byte_size{stereo_samples.size() * sizeof(std::int16_t)};
  SDL_IOStream* io{SDL_IOFromConstMem(stereo_samples.data(), byte_size)};
  if (io == nullptr) {
    return false;
  }
  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_S16;
  spec.channels = 2;
  spec.freq = 22080;
  MIX_StopTrack(m_track, 0);
  if (!MIX_SetTrackRawIOStream(m_track, io, &spec, /*closeio=*/true)) {
    SDL_CloseIO(io);
    return false;
  }
  m_samples = std::move(stereo_samples);
  m_source_name = std::move(display_name);
  const SDL_PropertiesID properties{SDL_CreateProperties()};
  SDL_SetNumberProperty(properties, MIX_PROP_PLAY_LOOPS_NUMBER, 0);
  const bool started{MIX_PlayTrack(m_track, properties)};
  SDL_DestroyProperties(properties);
  if (!started) {
    App::Log::warn(
        LogCategory::Audio, "dialogue voice '{}' failed: {}", m_source_name, SDL_GetError());
  }
  return started;
}

void DialogVoicePlayer::stop() {
  if (m_track != nullptr) {
    MIX_StopTrack(m_track, 0);
  }
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

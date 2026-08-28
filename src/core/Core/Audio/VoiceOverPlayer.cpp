#include "Core/Audio/VoiceOverPlayer.hpp"

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

VoiceOverPlayer::~VoiceOverPlayer() {
  shutdown();
}

void VoiceOverPlayer::attach(MIX_Mixer* mixer) {
  if (m_track != nullptr || mixer == nullptr) {
    return;
  }
  m_mixer = mixer;
  m_track = MIX_CreateTrack(mixer);
  if (m_track != nullptr && !MIX_TagTrack(m_track, "voiceover")) {
    App::Log::warn(
        LogCategory::Audio, "failed to tag OBJECTS voice-over track: {}", SDL_GetError());
  }
  set_gain(m_gain);
}

void VoiceOverPlayer::shutdown() {
  if (m_track != nullptr) {
    MIX_DestroyTrack(m_track);
    m_track = nullptr;
  }
  m_mixer = nullptr;
  m_samples.reset();
}

bool VoiceOverPlayer::play(
    std::string display_name, const SDL_AudioSpec spec, VoiceOverSamples samples) {
  if (m_mixer == nullptr || m_track == nullptr || samples == nullptr || samples->empty()) {
    return false;
  }
  const std::size_t byte_size{samples->size() * sizeof(std::int16_t)};
  MIX_Audio* input{
      MIX_LoadRawAudioNoCopy(m_mixer, samples->data(), byte_size, &spec, /*free_when_done=*/false)};
  if (input == nullptr) {
    return false;
  }
  if (!MIX_SetTrackAudio(m_track, input)) {
    MIX_DestroyAudio(input);
    return false;
  }
  MIX_DestroyAudio(input);
  m_samples = std::move(samples);
  m_source_name = std::move(display_name);
  if (!MIX_PlayTrack(m_track, 0)) {
    App::Log::warn(
        LogCategory::Audio, "OBJECTS voice-over '{}' failed: {}", m_source_name, SDL_GetError());
    return false;
  }
  return true;
}

void VoiceOverPlayer::stop() {
  if (m_track != nullptr) {
    MIX_StopTrack(m_track, 0);
    MIX_SetTrackAudio(m_track, nullptr);
  }
  m_samples.reset();
}

void VoiceOverPlayer::set_gain(const float gain) {
  m_gain = gain;
  if (m_track != nullptr) {
    MIX_SetTrackGain(m_track, m_gain);
  }
}

bool VoiceOverPlayer::is_playing() const {
  return m_track != nullptr && MIX_TrackPlaying(m_track);
}

}  // namespace App::Audio

#include "Core/Audio/VoiceOverPlayer.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <string>
#include <utility>

#include "Core/Audio/AdpStream.hpp"
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
}

bool VoiceOverPlayer::play(std::string display_name, const AdpStream stream) {
  if (stream.io == nullptr) {
    return false;
  }
  if (m_mixer == nullptr || m_track == nullptr) {
    SDL_CloseIO(stream.io);
    return false;
  }
  // The raw input is pulled on demand, so the clip decodes as it plays.
  // Replacing a playing track's input is legal; with closeio, SDL_mixer
  // closes the previous stream, and `stream.io` when this call fails.
  if (!MIX_SetTrackRawIOStream(m_track, stream.io, &stream.spec, /*closeio=*/true)) {
    return false;
  }
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

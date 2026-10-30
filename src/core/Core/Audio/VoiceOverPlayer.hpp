#pragma once

#include <SDL3_mixer/SDL_mixer.h>

#include <string>

#include "Core/Audio/AdpStream.hpp"

namespace App::Audio {

/// One dedicated, nonspatial, fire-and-forget OBJECTS voice-over lane.
///
/// Runtime 0x0041B1F0 starts VOICEOFF clips on a second instance of the music
/// ring streamer (0x0042C870 -> 0x0042C300, state 0x004EA7D0), so clips are
/// decoded while they play rather than up front.
class VoiceOverPlayer {
 public:
  VoiceOverPlayer() = default;
  ~VoiceOverPlayer();
  VoiceOverPlayer(const VoiceOverPlayer&) = delete;
  VoiceOverPlayer(VoiceOverPlayer&&) = delete;
  VoiceOverPlayer& operator=(const VoiceOverPlayer&) = delete;
  VoiceOverPlayer& operator=(VoiceOverPlayer&&) = delete;

  void attach(MIX_Mixer* mixer);
  void shutdown();
  /// Replaces the current clip with `stream`. SDL_mixer owns and closes
  /// `stream.io`, including when this call fails.
  [[nodiscard]] bool play(std::string display_name, AdpStream stream);
  void stop();
  void set_gain(float gain);
  [[nodiscard]] bool is_playing() const;

 private:
  MIX_Mixer* m_mixer{nullptr};
  MIX_Track* m_track{nullptr};
  std::string m_source_name;
  float m_gain{1.0F};
};

}  // namespace App::Audio

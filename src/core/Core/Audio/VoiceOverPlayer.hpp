#pragma once

#include <SDL3/SDL_audio.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace App::Audio {

/// Immutable decoded PCM backing retained while SDL_mixer reads it.
using VoiceOverSamples = std::shared_ptr<const std::vector<std::int16_t>>;

/// One dedicated, nonspatial, fire-and-forget OBJECTS voice-over lane.
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
  [[nodiscard]] bool play(std::string display_name, SDL_AudioSpec spec, VoiceOverSamples samples);
  void stop();
  [[nodiscard]] bool is_playing() const;

 private:
  MIX_Mixer* m_mixer{nullptr};
  MIX_Track* m_track{nullptr};
  VoiceOverSamples m_samples;
  std::string m_source_name;
};

}  // namespace App::Audio

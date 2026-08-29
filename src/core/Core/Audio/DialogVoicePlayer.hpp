#pragma once

#include <SDL3_mixer/SDL_mixer.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace App::Audio {

/// Immutable PCM backing shared by the preparation cache and mixer lane.
using DialogVoiceSamples = std::shared_ptr<const std::vector<std::int16_t>>;

/// One dedicated, nonspatial, one-shot dialogue voice lane.
class DialogVoicePlayer {
 public:
  DialogVoicePlayer() = default;
  ~DialogVoicePlayer();
  DialogVoicePlayer(const DialogVoicePlayer&) = delete;
  DialogVoicePlayer(DialogVoicePlayer&&) = delete;
  DialogVoicePlayer& operator=(const DialogVoicePlayer&) = delete;
  DialogVoicePlayer& operator=(DialogVoicePlayer&&) = delete;

  void attach(MIX_Mixer* mixer);
  void shutdown();
  [[nodiscard]] bool play(std::string display_name, DialogVoiceSamples stereo_samples);
  void stop();
  void set_gain(float gain);
  [[nodiscard]] bool is_playing() const;

 private:
  MIX_Mixer* m_mixer{nullptr};
  MIX_Track* m_track{nullptr};
  DialogVoiceSamples m_samples;
  std::string m_source_name;
  float m_gain{1.0F};
};

}  // namespace App::Audio

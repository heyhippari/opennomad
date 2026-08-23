#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <SDL3_mixer/SDL_mixer.h>

namespace App::Audio {

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
  [[nodiscard]] bool play(std::string display_name, std::vector<std::int16_t> stereo_samples);
  void stop();
  void set_gain(float gain);
  [[nodiscard]] bool is_playing() const;

 private:
  MIX_Mixer* m_mixer{nullptr};
  MIX_Track* m_track{nullptr};
  std::vector<std::int16_t> m_samples;
  std::string m_source_name;
  float m_gain{1.0F};
};

}  // namespace App::Audio

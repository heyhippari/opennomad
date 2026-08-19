#pragma once

#include <SDL3_mixer/SDL_mixer.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Audio/AudioTypes.hpp"
#include "Core/Audio/MusicPlayer.hpp"
#include "Core/Audio/SoundResourceCache.hpp"
#include "Core/Audio/VoicePool.hpp"

namespace App::Audio {

/// Owns the SDL3_mixer device mixer, the 16-voice SFX pool, the sound-resource
/// cache, the listener state, the music player and the main-thread event
/// queue. SDL3_mixer details stay inside this subsystem; script code submits
/// typed requests.
class AudioSystem {
 public:
  /// Resolves an owner token to the object's current world position, or
  /// nullopt when the object no longer exists (invalidated).
  using EmitterResolver = std::function<std::optional<Vec3>(const AudioOwnerToken&)>;

  /// Initialises SDL3_mixer, creates the device mixer (or a memory mixer when
  /// no audio device is available), the 16 SFX tracks and the music track.
  /// Failure is non-fatal: the system enters a logged silent state.
  [[nodiscard]] static std::expected<std::unique_ptr<AudioSystem>, std::string> create();

  ~AudioSystem();

  AudioSystem(const AudioSystem&) = delete;
  AudioSystem(AudioSystem&&) = delete;
  AudioSystem& operator=(const AudioSystem&) = delete;
  AudioSystem& operator=(AudioSystem&&) = delete;

  /// True when the mixer is usable (play requests may still fail per-voice).
  [[nodiscard]] bool available() const;

  // --- Resource cache (SCX integration) ---

  /// Loads (or reuses) a bounded PCM WAV byte span as a cached resource.
  [[nodiscard]] std::expected<SoundResourceId, std::string> load_sound(
      const std::string& canonical_key,
      std::string_view scenario_name,
      std::size_t record_index,
      std::string_view name,
      std::uint16_t h_id,
      std::span<const std::byte> wav_bytes);

  // --- Playback ---

  /// Queues a play request. Returns the allocated voice handle, or nullopt
  /// when the pool is exhausted, the resource is invalid, or audio is
  /// unavailable. The voice starts on the next main-thread update.
  [[nodiscard]] std::optional<VoiceHandle> play_sound(const SoundPlayRequest& request);

  /// Stops the first active voice matching (soundId, owner). Returns false
  /// when no match exists (harmless StopSound no-op).
  [[nodiscard]] bool stop_first(SoundResourceId sound, const AudioOwnerToken& owner);

  /// Stops the exact voice generation (inspector stop button). Returns false
  /// when the handle is stale or the voice is free.
  [[nodiscard]] bool stop_voice(VoiceHandle handle);

  /// Stops every active voice owned by `owner` (scenario unload).
  void stop_owned_by(const AudioOwnerToken& owner);

  /// Stops every active SFX voice.
  void stop_all_sfx();

  /// Nonspatial debug audition through the normal 16-voice pool.
  [[nodiscard]] std::optional<VoiceHandle> audition(SoundResourceId resource);

  // --- Listener / emitters ---

  void set_listener(const AudioListenerState& listener);
  void set_emitter_resolver(EmitterResolver resolver);

  // --- Frame update ---

  /// Drains stopped-callback events, applies stops, starts queued voices and
  /// spatializes active voices. Uses real seconds (never 30 Hz script frames).
  void update(float real_delta_seconds);

  // --- Gains ---

  void set_master_gain(float gain);
  void set_sfx_gain(float gain);
  void set_music_gain(float gain);
  [[nodiscard]] float master_gain() const;
  [[nodiscard]] float sfx_gain() const;
  [[nodiscard]] float music_gain() const;

  // --- Music ---

  [[nodiscard]] MusicPlayer& music();
  [[nodiscard]] const MusicPlayer& music() const;

  // --- Inspection ---

  /// Stable main-thread snapshot for the ImGui inspector.
  [[nodiscard]] const AudioDebugSnapshot& debug_snapshot() const;

  /// Compact audio context for the script debugger's audio diagnostics.
  [[nodiscard]] AudioContextInfo context_info() const;

 private:
  AudioSystem() = default;

  /// RAII deleter for the device mixer.
  struct MixerDeleter {
    void operator()(MIX_Mixer* mixer) const {
      if (mixer != nullptr) {
        MIX_DestroyMixer(mixer);
      }
    }
  };

  /// One per-track context handed to the stopped callback (compact, atomic).
  struct SlotContext {
    AudioSystem* system{nullptr};
    std::uint32_t index{0};
    std::atomic<std::uint32_t> generation{0};
  };

  static void SDLCALL stopped_callback(void* userdata, MIX_Track* track);

  /// Releases a slot exactly once when its generation still matches.
  void release_slot(VoiceHandle handle, const char* reason);

  /// Applies the spatial result (gain/stereo/frequency) to a track.
  void apply_spatial(std::size_t slot_index, const SoundVoice& voice, bool nonspatial);

  /// Updates the snapshot after a frame.
  void rebuild_snapshot();

  void append_event(AudioEventSeverity severity, std::string message);

  /// Bounded, thread-safe stop-event queue (mixer callback -> main thread).
  struct StopEventQueue {
    static constexpr std::size_t k_capacity{64};
    void push(std::uint32_t index, std::uint32_t generation);
    [[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t>> drain();
    std::mutex mutex{};
    std::array<std::pair<std::uint32_t, std::uint32_t>, k_capacity> slots{};
    std::size_t count{0};
  };

  bool m_initialized{false};
  bool m_unavailable{false};
  std::string m_state_note;

  std::unique_ptr<MIX_Mixer, MixerDeleter> m_mixer;
  std::unique_ptr<SoundResourceCache> m_cache;
  std::array<MIX_Track*, VoicePool::k_voice_count> m_tracks{};
  std::array<SlotContext, VoicePool::k_voice_count> m_contexts{};
  VoicePool m_pool;
  MusicPlayer m_music;
  AudioListenerState m_listener{};
  EmitterResolver m_emitter_resolver;
  StopEventQueue m_stop_events;

  float m_master_gain{1.0F};
  float m_sfx_gain{1.0F};
  float m_music_gain{1.0F};

  std::string m_mixer_version;
  std::string m_device_name;
  std::string m_requested_format;
  std::string m_negotiated_format;
  float m_last_update_delta_seconds{0.0F};

  /// Main-thread event ring for the inspector (bounded).
  static constexpr std::size_t k_event_capacity{256};
  std::vector<AudioEvent> m_events;
  mutable AudioDebugSnapshot m_snapshot;
};

}  // namespace App::Audio

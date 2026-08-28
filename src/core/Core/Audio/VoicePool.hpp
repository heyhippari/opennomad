#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "Core/Audio/AudioTypes.hpp"

namespace App::Audio {

/// One of the 16 SFX voice slots. SDL track state (the owning `MIX_Track`)
/// lives in AudioSystem; this struct carries only the policy state so the
/// allocation and matching rules stay unit-testable without a device.
struct SoundVoice {
  VoiceState state{VoiceState::k_free};
  std::uint32_t generation{0};
  SoundResourceId resource{};
  std::uint16_t scenario_sound_index{0xFFFFU};
  AudioOwnerToken owner{};
  AudioProvenance provenance;
  SoundCategory category{SoundCategory::k_sfx};
  std::optional<SoundEmitterState> emitter{};
  float previous_distance{-1.0F};
  float base_frequency_hz{0.0F};
  float distance{0.0F};
  float attenuation_gain{1.0F};
  float pan{0.0F};
  float left_gain{1.0F};
  float right_gain{1.0F};
  float frequency_ratio{1.0F};
  bool looping{false};
  bool nonspatial{false};
  /// Recovered original flag 0x08: observed in cleanup logic but not
  /// understood; preserved explicitly rather than given invented semantics.
  bool unknown_flag{false};
};

/// The original 16-voice SFX pool: deterministic first-free allocation, no
/// voice stealing, generation-counted handles. Pure policy (no SDL calls).
class VoicePool {
 public:
  static constexpr std::size_t k_voice_count{16};

  VoicePool() = default;
  ~VoicePool() = default;
  VoicePool(const VoicePool&) = delete;
  VoicePool(VoicePool&&) = delete;
  VoicePool& operator=(const VoicePool&) = delete;
  VoicePool& operator=(VoicePool&&) = delete;

  /// Allocates the first free slot (scanning 0..15) and returns its handle.
  /// Fails with `VoicePoolExhausted` when all 16 slots are occupied; never
  /// steals a playing voice.
  [[nodiscard]] std::expected<VoiceHandle, std::string> allocate();

  /// Initializes a freshly allocated slot from a play request and bumps the
  /// generation on reuse. Resets every per-use field to neutral defaults.
  void configure(VoiceHandle handle, const SoundPlayRequest& request);

  /// Transitions a slot to a new state.
  void mark(VoiceHandle handle, VoiceState state);

  /// Releases a slot (marks it free) and bumps its generation so every stale
  /// handle to it is invalidated.
  void release(VoiceHandle handle);

  /// The first active voice matching (soundId, owner), or nullopt. Pure
  /// matching; the caller decides whether/how to stop the underlying track.
  [[nodiscard]] std::optional<VoiceHandle> find_first_active(
      SoundResourceId sound, const AudioOwnerToken& owner) const;

  /// Releases every active voice owned by `owner` (used at scenario unload).
  void release_owned_by(const AudioOwnerToken& owner);

  /// Releases every active voice (used at shutdown once tracks are stopped).
  void release_all();

  [[nodiscard]] const SoundVoice* find(VoiceHandle handle) const;
  [[nodiscard]] SoundVoice* find(VoiceHandle handle);
  [[nodiscard]] bool generation_matches(VoiceHandle handle) const;

  [[nodiscard]] std::size_t active_count() const;
  [[nodiscard]] std::size_t free_count() const;
  [[nodiscard]] std::size_t size() const {
    return m_slots.size();
  }
  [[nodiscard]] const SoundVoice& at(std::size_t index) const {
    return m_slots.at(index);
  }
  [[nodiscard]] SoundVoice& at(std::size_t index) {
    return m_slots.at(index);
  }

 private:
  [[nodiscard]] static std::size_t resolve_index(VoiceHandle handle);
  [[nodiscard]] bool valid_handle(VoiceHandle handle) const;
  [[nodiscard]] static bool is_active(const SoundVoice& voice);

  std::array<SoundVoice, k_voice_count> m_slots{};
};

}  // namespace App::Audio

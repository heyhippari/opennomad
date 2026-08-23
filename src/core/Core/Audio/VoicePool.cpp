#include "Core/Audio/VoicePool.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "Core/Audio/AudioTypes.hpp"

namespace App::Audio {

bool VoicePool::is_active(const SoundVoice& voice) {
  return voice.state != VoiceState::k_free;
}

std::size_t VoicePool::resolve_index(const VoiceHandle handle) {
  if (!handle.valid()) {
    return k_voice_count;
  }
  return static_cast<std::size_t>(handle.index);
}

bool VoicePool::valid_handle(const VoiceHandle handle) const {
  const std::size_t index{resolve_index(handle)};
  if (index >= m_slots.size()) {
    return false;
  }
  return m_slots.at(index).generation == handle.generation;
}

std::expected<VoiceHandle, std::string> VoicePool::allocate() {
  for (std::size_t index{0}; index < m_slots.size(); ++index) {
    if (m_slots.at(index).state == VoiceState::k_free) {
      // Mark the slot occupied immediately so subsequent allocations skip it
      // (configure() then fills the per-use fields).
      m_slots.at(index).state = VoiceState::k_queued;
      return VoiceHandle{
          .index = static_cast<std::uint32_t>(index), .generation = m_slots.at(index).generation};
    }
  }
  return std::expected<VoiceHandle, std::string>{
      std::unexpect, "VoicePoolExhausted: all 16 SFX voices are occupied"};
}

void VoicePool::configure(const VoiceHandle handle, const SoundPlayRequest& request) {
  if (!valid_handle(handle)) {
    return;
  }
  SoundVoice& voice{m_slots.at(static_cast<std::size_t>(handle.index))};
  voice.state = VoiceState::k_queued;
  // Generation is bumped by release() on reuse; allocation above returns the
  // slot's current generation, which is stable while the slot is occupied.
  voice.resource = request.resource;
  voice.scenario_sound_index = request.scenario_sound_index;
  voice.owner = request.owner;
  voice.provenance = request.provenance;
  voice.emitter = request.emitter;
  voice.previous_distance = -1.0F;
  voice.base_frequency_hz = 0.0F;
  voice.distance = 0.0F;
  voice.attenuation_gain = 1.0F;
  voice.pan = 0.0F;
  voice.left_gain = 1.0F;
  voice.right_gain = 1.0F;
  voice.frequency_ratio = 1.0F;
  voice.looping = request.loop;
  voice.nonspatial = !request.emitter.has_value();
  voice.unknown_flag = (request.raw_flags & 0x08U) != 0U;
}

void VoicePool::mark(const VoiceHandle handle, const VoiceState state) {
  SoundVoice* voice{find(handle)};
  if (voice == nullptr) {
    return;
  }
  voice->state = state;
}

void VoicePool::release(const VoiceHandle handle) {
  if (!valid_handle(handle)) {
    return;
  }
  SoundVoice& voice{m_slots.at(static_cast<std::size_t>(handle.index))};
  voice.state = VoiceState::k_free;
  voice.resource = SoundResourceId{};
  voice.scenario_sound_index = 0xFFFFU;
  voice.owner = AudioOwnerToken{};
  voice.provenance = AudioProvenance{};
  voice.emitter.reset();
  voice.previous_distance = -1.0F;
  voice.base_frequency_hz = 0.0F;
  voice.distance = 0.0F;
  voice.attenuation_gain = 1.0F;
  voice.pan = 0.0F;
  voice.left_gain = 1.0F;
  voice.right_gain = 1.0F;
  voice.frequency_ratio = 1.0F;
  voice.looping = false;
  voice.nonspatial = false;
  voice.unknown_flag = false;
  voice.generation += 1;
}

std::optional<VoiceHandle> VoicePool::find_first_active(
    const SoundResourceId sound, const AudioOwnerToken& owner) const {
  for (std::size_t index{0}; index < m_slots.size(); ++index) {
    const SoundVoice& voice{m_slots.at(index)};
    if (!is_active(voice)) {
      continue;
    }
    if (voice.resource == sound && voice.owner == owner) {
      return VoiceHandle{
          .index = static_cast<std::uint32_t>(index), .generation = voice.generation};
    }
  }
  return std::nullopt;
}

void VoicePool::release_owned_by(const AudioOwnerToken& owner) {
  for (std::size_t index{0}; index < m_slots.size(); ++index) {
    SoundVoice& voice{m_slots.at(index)};
    if (!is_active(voice)) {
      continue;
    }
    if (voice.owner == owner) {
      voice.state = VoiceState::k_free;
      voice.generation += 1;
    }
  }
}

void VoicePool::release_all() {
  for (std::size_t index{0}; index < m_slots.size(); ++index) {
    SoundVoice& voice{m_slots.at(index)};
    if (is_active(voice)) {
      voice.state = VoiceState::k_free;
      voice.generation += 1;
    }
  }
}

const SoundVoice* VoicePool::find(const VoiceHandle handle) const {
  if (!valid_handle(handle)) {
    return nullptr;
  }
  return &m_slots.at(static_cast<std::size_t>(handle.index));
}

SoundVoice* VoicePool::find(const VoiceHandle handle) {
  if (!valid_handle(handle)) {
    return nullptr;
  }
  return &m_slots.at(static_cast<std::size_t>(handle.index));
}

bool VoicePool::generation_matches(const VoiceHandle handle) const {
  return valid_handle(handle);
}

std::size_t VoicePool::active_count() const {
  std::size_t count{0};
  for (const SoundVoice& voice : m_slots) {
    if (is_active(voice)) {
      count += 1;
    }
  }
  return count;
}

std::size_t VoicePool::free_count() const {
  return m_slots.size() - active_count();
}

}  // namespace App::Audio

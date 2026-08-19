#include "Core/Audio/SoundResourceCache.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3_mixer/SDL_mixer.h>

#include "Core/Audio/AudioTypes.hpp"
#include "Core/Log.hpp"

namespace App::Audio {

namespace {

/// "RIFF" as a little-endian word: every embedded WAV payload must start with it.
constexpr std::uint32_t K_RIFF_MAGIC{0x46464952U};

/// Reads a little-endian u32 from a bounded span at `offset`.
[[nodiscard]] std::uint32_t read_u32_at(const std::span<const std::byte> bytes,
    const std::size_t offset) {
  const std::span<const std::byte> word{bytes.subspan(offset, 4U)};
  std::uint32_t value{0};
  std::memcpy(&value, word.data(), 4U);
  return value;
}

}  // namespace

bool SoundResourceTable::valid(const SoundResourceId id) const {
  return id.valid() && static_cast<std::size_t>(id.index) < m_entries.size();
}

std::expected<SoundResourceId, std::string> SoundResourceTable::acquire(
    const std::string& canonical_key) {
  const auto existing{m_by_key.find(canonical_key)};
  if (existing != m_by_key.end()) {
    return m_entries.at(existing->second).id;
  }
  if (m_entries.size() >= k_capacity) {
    return std::expected<SoundResourceId, std::string>{std::unexpect,
        fmt::format("SoundResourceCacheExhausted: {} cached sounds (capacity {})",
            m_entries.size(),
            k_capacity)};
  }
  const SoundResourceId id{.index = static_cast<std::uint16_t>(m_entries.size())};
  Entry entry;
  entry.id = id;
  entry.canonical_key = canonical_key;
  m_entries.push_back(std::move(entry));
  m_by_key.emplace(canonical_key, static_cast<std::size_t>(id.index));
  return id;
}

void SoundResourceTable::mark_loaded(const SoundResourceId id,
    std::string format,
    const int channels,
    const int frequency,
    const std::int64_t duration_ms,
    const std::size_t byte_size) {
  if (!valid(id)) {
    return;
  }
  Entry& entry{m_entries.at(static_cast<std::size_t>(id.index))};
  entry.format = std::move(format);
  entry.channels = channels;
  entry.frequency = frequency;
  entry.duration_ms = duration_ms;
  entry.byte_size = byte_size;
  entry.loaded = true;
  entry.load_error.clear();
}

void SoundResourceTable::mark_failed(const SoundResourceId id, std::string error) {
  if (!valid(id)) {
    return;
  }
  Entry& entry{m_entries.at(static_cast<std::size_t>(id.index))};
  entry.loaded = false;
  entry.load_error = std::move(error);
}

void SoundResourceTable::set_metadata(const SoundResourceId id,
    std::string scenario_name,
    const std::size_t record_index,
    std::string name,
    const std::uint16_t h_id,
    const std::size_t byte_size) {
  if (!valid(id)) {
    return;
  }
  Entry& entry{m_entries.at(static_cast<std::size_t>(id.index))};
  entry.scenario_name = std::move(scenario_name);
  entry.record_index = record_index;
  entry.name = std::move(name);
  entry.h_id = h_id;
  entry.byte_size = byte_size;
}

void SoundResourceTable::add_reference(const SoundResourceId id) {
  if (!valid(id)) {
    return;
  }
  m_entries.at(static_cast<std::size_t>(id.index)).ref_count += 1;
}

void SoundResourceTable::remove_reference(const SoundResourceId id) {
  if (!valid(id)) {
    return;
  }
  Entry& entry{m_entries.at(static_cast<std::size_t>(id.index))};
  if (entry.ref_count > 0) {
    entry.ref_count -= 1;
  }
}

const SoundResourceTable::Entry* SoundResourceTable::find(const SoundResourceId id) const {
  if (!valid(id)) {
    return nullptr;
  }
  return &m_entries.at(static_cast<std::size_t>(id.index));
}

std::size_t SoundResourceTable::count() const { return m_entries.size(); }

// ─────────────────────────────────────────────────────────────────────────────
// SoundResourceCache
// ─────────────────────────────────────────────────────────────────────────────

SoundResourceCache::SoundResourceCache(MIX_Mixer* mixer) : m_mixer(mixer) {
  m_audio.resize(SoundResourceTable::k_capacity, nullptr);
}

SoundResourceCache::~SoundResourceCache() { clear(); }

std::expected<SoundResourceId, std::string> SoundResourceCache::load(
    const std::string& canonical_key,
    const std::string_view scenario_name,
    const std::size_t record_index,
    const std::string_view name,
    const std::uint16_t h_id,
    const std::span<const std::byte> wav_bytes) {
  // Acquire the slot first so dedup and the capacity bound are enforced
  // before any decoding work.
  auto acquired{m_table.acquire(canonical_key)};
  if (!acquired) {
    return std::expected<SoundResourceId, std::string>{std::unexpect, acquired.error()};
  }
  const SoundResourceId resource{acquired.value()};

  const SoundResourceTable::Entry* existing{m_table.find(resource)};
  if (existing != nullptr && existing->loaded) {
    return resource;  // Already decoded: reuse the cached MIX_Audio.
  }

  // Fill static metadata regardless of the decode outcome.
  m_table.set_metadata(resource,
      std::string{scenario_name},
      record_index,
      std::string{name},
      h_id,
      wav_bytes.size());

  // Validate the RIFF/WAVE signature before handing the span to SDL, so a
  // malformed span fails deterministically and never triggers an
  // out-of-bounds read inside the decoder.
  if (wav_bytes.size() < 12U || read_u32_at(wav_bytes, 0) != K_RIFF_MAGIC) {
    const std::string error{"not a RIFF/WAVE stream (missing RIFF signature)"};
    m_table.mark_failed(resource, error);
    App::Log::warn("Audio: sound '{}' (scenario '{}', record {}): {}",
        name,
        scenario_name,
        record_index,
        error);
    return std::expected<SoundResourceId, std::string>{std::unexpect, error};
  }

  SDL_IOStream* io{SDL_IOFromConstMem(wav_bytes.data(), wav_bytes.size())};
  if (io == nullptr) {
    const std::string error{fmt::format("SDL_IOFromConstMem failed: {}", SDL_GetError())};
    m_table.mark_failed(resource, error);
    return std::expected<SoundResourceId, std::string>{std::unexpect, error};
  }

  MIX_Audio* audio{MIX_LoadAudio_IO(m_mixer, io, /*predecode=*/true, /*closeio=*/true)};
  if (audio == nullptr) {
    const std::string error{fmt::format("MIX_LoadAudio_IO failed: {}", SDL_GetError())};
    m_table.mark_failed(resource, error);
    App::Log::warn("Audio: sound '{}' (scenario '{}', record {}): {}",
        name,
        scenario_name,
        record_index,
        error);
    return std::expected<SoundResourceId, std::string>{std::unexpect, error};
  }

  SDL_AudioSpec spec{};
  const bool have_format{MIX_GetAudioFormat(audio, &spec)};
  const std::int64_t duration_ms{MIX_GetAudioDuration(audio)};
  const char* format_name{have_format ? SDL_GetAudioFormatName(spec.format) : "unknown"};
  m_table.mark_loaded(resource,
      format_name,
      have_format ? spec.channels : 0,
      have_format ? spec.freq : 0,
      duration_ms,
      wav_bytes.size());
  m_audio.at(static_cast<std::size_t>(resource.index)) = audio;

  App::Log::info("Audio: loaded sound '{}' (scenario '{}', record {}) -> resource {} "
                 "({}, {} ch, {} Hz, {} ms, {} bytes)",
      name,
      scenario_name,
      record_index,
      resource.index,
      format_name,
      have_format ? spec.channels : 0,
      have_format ? spec.freq : 0,
      duration_ms,
      wav_bytes.size());
  return resource;
}

MIX_Audio* SoundResourceCache::audio(const SoundResourceId id) const {
  if (!id.valid() || static_cast<std::size_t>(id.index) >= m_audio.size()) {
    return nullptr;
  }
  return m_audio.at(static_cast<std::size_t>(id.index));
}

const SoundResourceTable::Entry* SoundResourceCache::find_entry(const SoundResourceId id) const {
  return m_table.find(id);
}

void SoundResourceCache::add_reference(const SoundResourceId id) { m_table.add_reference(id); }

void SoundResourceCache::remove_reference(const SoundResourceId id) {
  m_table.remove_reference(id);
}

void SoundResourceCache::clear() {
  for (MIX_Audio*& slot : m_audio) {
    if (slot != nullptr) {
      MIX_DestroyAudio(slot);
      slot = nullptr;
    }
  }
}

std::size_t SoundResourceCache::count() const { return m_table.count(); }

std::size_t SoundResourceCache::capacity() const { return m_table.capacity(); }

std::vector<ResourceDebugInfo> SoundResourceCache::debug_info() const {
  std::vector<ResourceDebugInfo> result;
  result.reserve(m_table.entries().size());
  for (const SoundResourceTable::Entry& entry : m_table.entries()) {
    result.push_back(ResourceDebugInfo{.resource = entry.id,
        .canonical_key = entry.canonical_key,
        .scenario_name = entry.scenario_name,
        .record_index = entry.record_index,
        .name = entry.name,
        .format = entry.format,
        .channels = entry.channels,
        .frequency = entry.frequency,
        .duration_ms = entry.duration_ms,
        .byte_size = entry.byte_size,
        .ref_count = entry.ref_count,
        .loaded = entry.loaded,
        .load_error = entry.load_error});
  }
  return result;
}

}  // namespace App::Audio

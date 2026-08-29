#pragma once

#include <SDL3_mixer/SDL_mixer.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Core/Audio/AudioTypes.hpp"

namespace App::Audio {

/// Pure bookkeeping for cached sound resources: canonical-identity dedup, the
/// original 160-resource compatibility bound, per-entry metadata and voice
/// reference counting. No SDL calls, so the policy is unit-testable without a
/// device. The SDL-backed `MIX_Audio` ownership lives in `SoundResourceCache`.
class SoundResourceTable {
 public:
  static constexpr std::size_t k_capacity{160};

  struct Entry {
    SoundResourceId id{};
    std::string canonical_key;
    std::string scenario_name;
    std::size_t record_index{0};
    std::string name;
    std::uint16_t h_id{0};
    std::string format;
    int channels{0};
    int frequency{0};
    std::int64_t duration_ms{-1};
    std::size_t byte_size{0};
    std::size_t ref_count{0};
    bool loaded{false};
    std::string load_error;
  };

  /// Returns the existing id for a canonical key, or allocates a new entry.
  /// Fails when the 160-resource compatibility bound is exhausted.
  [[nodiscard]] std::expected<SoundResourceId, std::string> acquire(
      const std::string& canonical_key);

  /// Records decode metadata/status for an entry.
  void mark_loaded(SoundResourceId id,
      std::string format,
      int channels,
      int frequency,
      std::int64_t duration_ms,
      std::size_t byte_size);
  void mark_failed(SoundResourceId id, std::string error);

  /// Sets the static source identity/metadata (scenario, record, name, hID,
  /// byte size) independent of the decode outcome.
  void set_metadata(SoundResourceId id,
      std::string scenario_name,
      std::size_t record_index,
      std::string name,
      std::uint16_t h_id,
      std::size_t byte_size);

  /// One voice started/stopped referencing the resource.
  void add_reference(SoundResourceId id);
  void remove_reference(SoundResourceId id);

  [[nodiscard]] const Entry* find(SoundResourceId id) const;
  [[nodiscard]] std::size_t count() const;
  [[nodiscard]] std::size_t capacity() const {
    return k_capacity;
  }
  [[nodiscard]] const std::vector<Entry>& entries() const {
    return m_entries;
  }

 private:
  [[nodiscard]] bool valid(SoundResourceId id) const;

  std::vector<Entry> m_entries;
  std::unordered_map<std::string, std::size_t> m_by_key;
};

/// SDL-backed sound-resource cache. Owns one `MIX_Audio` per successfully
/// decoded resource; the pure `SoundResourceTable` handles identity, capacity
/// and reference counting. The same cached audio is shared by every voice.
class SoundResourceCache {
 public:
  explicit SoundResourceCache(MIX_Mixer* mixer);

  SoundResourceCache(const SoundResourceCache&) = delete;
  SoundResourceCache(SoundResourceCache&&) = delete;
  SoundResourceCache& operator=(const SoundResourceCache&) = delete;
  SoundResourceCache& operator=(SoundResourceCache&&) = delete;
  ~SoundResourceCache();

  /// Loads (or reuses) a bounded PCM WAV byte span as a cached resource.
  /// `canonical_key` is the dedup identity; scenario/record/name are
  /// diagnostic metadata. Fully predecodes short effects.
  [[nodiscard]] std::expected<SoundResourceId, std::string> load(const std::string& canonical_key,
      std::string_view scenario_name,
      std::size_t record_index,
      std::string_view name,
      std::uint16_t h_id,
      std::span<const std::byte> wav_bytes);

  /// The decoded `MIX_Audio` for a loaded resource (not owning to the caller).
  [[nodiscard]] MIX_Audio* audio(SoundResourceId id) const;

  /// The bookkeeping entry for a resource (diagnostics), or nullptr.
  [[nodiscard]] const SoundResourceTable::Entry* find_entry(SoundResourceId id) const;

  /// One voice started/stopped referencing the resource.
  void add_reference(SoundResourceId id);
  void remove_reference(SoundResourceId id);

  /// Destroys every cached `MIX_Audio`. Caller must guarantee no voice still
  /// references a resource (scenario unload stops owned voices first).
  void clear();

  [[nodiscard]] std::size_t count() const;
  [[nodiscard]] std::size_t capacity() const;
  [[nodiscard]] std::vector<ResourceDebugInfo> debug_info() const;

 private:
  MIX_Mixer* m_mixer{nullptr};
  SoundResourceTable m_table;
  /// Owning `MIX_Audio` per entry, parallel to `m_table.entries()` by id.
  std::vector<MIX_Audio*> m_audio;
};

}  // namespace App::Audio

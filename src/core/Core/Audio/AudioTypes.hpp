#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace App::Audio {

/// Three-component vector, matching the repository's established position
/// convention (`std::array<float, 3>`).
using Vec3 = std::array<float, 3>;

/// Typed runtime sound-resource handle. The serialized `uint16_t` resource id
/// of a scenario sound record is converted to/from this only at the
/// SCX/runtime-record boundary; `0xFFFF` remains the invalid sentinel.
struct SoundResourceId {
  static constexpr std::uint16_t k_invalid{0xFFFFU};

  std::uint16_t index{k_invalid};

  [[nodiscard]] constexpr bool valid() const { return index != k_invalid; }
  friend constexpr bool operator==(const SoundResourceId&, const SoundResourceId&) = default;
};

/// Stable scenario/object identity for object-attached playback. Never an
/// unguarded raw pointer to a scenario object: `scenario` identifies the
/// owning scenario (opaque), `object_index`/`generation` identify the object
/// within it. A null owner (nonspatial playback) has `scenario == nullptr`.
struct AudioOwnerToken {
  const void* scenario{nullptr};
  std::uint32_t object_index{0};
  std::uint32_t generation{0};

  [[nodiscard]] constexpr bool is_null() const { return scenario == nullptr; }

  /// Printable, deterministic debug representation (scenario identity is
  /// rendered as a pointer-sized integer, never dereferenced).
  [[nodiscard]] std::string describe() const {
    if (is_null()) {
      return "null";
    }
    const auto address{reinterpret_cast<std::uintptr_t>(scenario)};
    return std::string{"scenario#"} + std::to_string(address) +
           " object " + std::to_string(object_index) +
           " gen " + std::to_string(generation);
  }

  friend constexpr bool operator==(const AudioOwnerToken&, const AudioOwnerToken&) = default;
};

/// Spatial parameters of one play request (position, velocity, distances).
struct SoundEmitterState {
  Vec3 position{0.0F, 0.0F, 0.0F};
  Vec3 velocity{0.0F, 0.0F, 0.0F};
  float minimum_distance{78.0F};
  float maximum_distance{1170.0F};
};

/// Typed play request submitted by script handlers to the audio system.
/// Absent `emitter` means nonspatial (centered, full gain, normal pitch).
struct SoundPlayRequest {
  SoundResourceId resource{};
  bool loop{false};
  std::optional<SoundEmitterState> emitter{};
  AudioOwnerToken owner{};
  /// Scenario sound-table index and name, retained for diagnostics only.
  std::uint16_t scenario_sound_index{0xFFFFU};
  std::string sound_name;
  /// Raw recovered command flags, preserved for diagnostics. Bit 0 is the
  /// confirmed infinite-loop flag; bit 3 (0x08) is retained as an explicit
  /// unknown/unsupported representation.
  std::uint32_t raw_flags{0};
};

/// Descriptor of one resolved scenario sound (diagnostics / script pause).
struct SoundDescriptor {
  SoundResourceId resource{};
  std::string name;
  std::uint16_t h_id{0};
  bool loaded{false};
};

enum class VoiceState : std::uint8_t {
  k_free,
  k_queued,
  k_playing,
  k_stopping,
};

/// Stable identifier of one of the 16 SFX voice slots (index + generation).
struct VoiceHandle {
  static constexpr std::uint32_t k_invalid_index{0xFFFFFFFFU};

  std::uint32_t index{k_invalid_index};
  std::uint32_t generation{0};

  [[nodiscard]] constexpr bool valid() const { return index != k_invalid_index; }
  friend constexpr bool operator==(const VoiceHandle&, const VoiceHandle&) = default;
};

/// Listener state pushed by the active camera/player view. `forward` and
/// `up` follow the OpenNomad camera convention; `right` is derived safely by
/// the audio subsystem, never supplied.
struct AudioListenerState {
  Vec3 position{0.0F, 0.0F, 0.0F};
  Vec3 velocity{0.0F, 0.0F, 0.0F};
  Vec3 forward{0.0F, 0.0F, -1.0F};
  Vec3 up{0.0F, 1.0F, 0.0F};
};

/// Derived spatial values of one voice (LegacySpatializer output).
struct SpatialResult {
  float distance{0.0F};
  float attenuation_gain{1.0F};
  float pan{0.0F};
  float left_gain{1.0F};
  float right_gain{1.0F};
  float frequency_ratio{1.0F};
};

/// Severity of one audio event (bounded ring for the inspector).
enum class AudioEventSeverity : std::uint8_t {
  k_debug,
  k_info,
  k_warn,
  k_error,
};

/// One bounded audio event, appended on the main thread only.
struct AudioEvent {
  AudioEventSeverity severity{AudioEventSeverity::k_info};
  std::string message;
};

/// Read-only snapshot of one SFX voice slot (free slots included).
struct VoiceDebugInfo {
  std::uint32_t index{0};
  std::uint32_t generation{0};
  VoiceState state{VoiceState::k_free};
  SoundResourceId resource{};
  std::uint16_t scenario_sound_index{0xFFFFU};
  std::string sound_name;
  std::string owner_description;
  bool looping{false};
  bool nonspatial{false};
  bool unknown_flag{false};
  std::int64_t playback_position_ms{-1};
  std::int64_t remaining_ms{-1};
  std::array<float, 3> emitter_position{0.0F, 0.0F, 0.0F};
  std::array<float, 3> emitter_velocity{0.0F, 0.0F, 0.0F};
  float minimum_distance{0.0F};
  float maximum_distance{0.0F};
  float distance{0.0F};
  float previous_distance{-1.0F};
  float attenuation_gain{1.0F};
  float pan{0.0F};
  float left_gain{1.0F};
  float right_gain{1.0F};
  float base_frequency_hz{0.0F};
  float frequency_ratio{1.0F};
};

/// Read-only snapshot of one cached sound resource.
struct ResourceDebugInfo {
  SoundResourceId resource{};
  std::string canonical_key;
  std::string scenario_name;
  std::size_t record_index{0};
  std::string name;
  std::string format;
  int channels{0};
  int frequency{0};
  std::int64_t duration_ms{-1};
  std::size_t byte_size{0};
  std::size_t ref_count{0};
  bool loaded{false};
  std::string load_error;
};

/// Read-only music state snapshot.
struct MusicDebugInfo {
  std::string source_name;
  bool playing{false};
  bool paused{false};
  bool loop{false};
  std::int64_t loop_start_ms{0};
  std::int64_t playback_position_ms{0};
  std::int64_t duration_ms{-1};
  float gain{1.0F};
  std::string status_note;
};

/// Stable, main-thread snapshot consumed by the ImGui inspector.
struct AudioDebugSnapshot {
  bool initialized{false};
  bool unavailable{false};
  std::string state_note;
  std::string mixer_version;
  std::string device_name;
  std::string requested_format;
  std::string negotiated_format;
  float master_gain{1.0F};
  float sfx_gain{1.0F};
  float music_gain{1.0F};
  std::size_t active_voices{0};
  std::size_t free_voices{0};
  std::size_t cached_resources{0};
  std::size_t cache_capacity{0};
  float last_update_delta_seconds{0.0F};
  std::array<float, 3> listener_position{0.0F, 0.0F, 0.0F};
  std::array<float, 3> listener_velocity{0.0F, 0.0F, 0.0F};
  std::array<float, 3> listener_forward{0.0F, 0.0F, -1.0F};
  std::array<float, 3> listener_up{0.0F, 1.0F, 0.0F};
  std::array<float, 3> listener_right{1.0F, 0.0F, 0.0F};
  bool listener_degenerate{false};
  std::vector<VoiceDebugInfo> voices;
  std::vector<ResourceDebugInfo> resources;
  MusicDebugInfo music;
  std::vector<AudioEvent> events;
};

/// Compact, SDL-free audio context reported to the script debugger when an
/// audio/music opcode pauses. Built from the latest debug snapshot.
struct AudioContextInfo {
  bool available{false};
  std::string negotiated_format;
  std::size_t active_voices{0};
  std::size_t free_voices{0};
  std::string music_state;
  std::vector<std::string> recent_events;
};

}  // namespace App::Audio

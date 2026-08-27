#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "Core/RuntimeMath.hpp"

namespace App::Omikron {

/// One immutable camera pose within a DEAD000A camera-editing timeline.
/// Stride in payload: 0x34 bytes.
struct ScxCameraEditingPose {
  /// Camera ID used to link timed keys to this pose.
  std::uint32_t id{0};

  /// Fixed 12-byte name field.
  std::string name;

  /// Eye position in Runtime-native world coordinates (inches).
  Runtime::Vec3 eye{};

  /// Look-at target in Runtime-native world coordinates (inches).
  Runtime::Vec3 target{};

  /// Camera roll in degrees.
  float roll_degrees{0.0F};

  /// Horizontal field of view in degrees.
  float horizontal_fov_degrees{0.0F};

  /// Serialized next-pointer placeholder (overwritten by Runtime).
  std::uint32_t serialized_next_placeholder{0};
};

/// One timed key attaching a camera pose to a local segment time.
/// Stride in payload: 0x1C bytes.
struct ScxCameraEditingKey {
  /// Key ID used by segments to reference this key.
  std::uint32_t id{0};

  /// Camera ID; resolved to camera_index during graph linking.
  std::uint32_t camera_id{0};

  /// Local segment time in 30 Hz script-frame units.
  float local_time_frames{0.0F};

  /// Unresolved semantic purpose.
  std::array<std::uint32_t, 3> unknown{};

  /// Serialized next-pointer placeholder (overwritten by Runtime).
  std::uint32_t serialized_next_placeholder{0};

  // Safe resolved modern representation:
  /// Index into camera array after graph linking.
  std::size_t camera_index{0};
};

/// One segment of a camera-editing timeline, containing a sequence of timed keys.
/// Fixed portion stride in payload: 0x18 bytes.
struct ScxCameraEditingSegment {
  /// Segment ID used by editing tracks to reference this segment.
  std::uint32_t id{0};

  /// Fixed 10-byte name field.
  std::string name;

  /// Count of u32 IDs following the fixed segment array.
  std::uint16_t serialized_key_reference_count{0};

  /// Serialized key-reference-array pointer placeholder (overwritten by Runtime).
  std::uint32_t serialized_refs_placeholder{0};

  /// Serialized next-segment pointer placeholder (overwritten by Runtime).
  std::uint32_t serialized_next_placeholder{0};

  // Safe resolved modern representation:
  /// Resolved indices into key array in serialized order.
  std::vector<std::size_t> key_indices;
};

/// One editing track: a sequence of segments organized into a timeline.
/// Fixed portion stride in payload: 0x20 bytes.
struct ScxCameraEditingTrack {
  /// Context ID attached to script instances using this track.
  std::uint8_t context_id{0};

  /// Fixed 11-byte name field.
  std::string name;

  /// Count of u32 segment IDs following the fixed track array.
  std::uint16_t serialized_segment_reference_count{0};

  /// Unresolved semantic purpose.
  std::uint16_t unknown_0e{0};

  /// Serialized segment-reference-array pointer placeholder (overwritten by Runtime).
  std::uint32_t serialized_refs_placeholder{0};

  /// Serialized next-editing pointer placeholder (overwritten by Runtime).
  std::uint32_t serialized_next_placeholder{0};

  /// Timeline duration in 30 Hz script-frame units.
  std::uint32_t total_duration_frames{0};

  /// Script ID this track is attached to (0 for detached).
  std::uint16_t target_script_id{0};

  /// Unresolved semantic purpose.
  std::uint16_t unknown_1e{0};

  // Safe resolved modern representation:
  /// Resolved indices into segment array in serialized order.
  std::vector<std::size_t> segment_indices;
};

/// Parsed DEAD000A compiled camera-editing timeline.
struct ScxCameraEditingData {
  /// Supported format version.
  static constexpr std::uint32_t k_supported_version = 3U;

  /// Format version from header.
  std::uint32_t version{0};

  /// All camera poses indexed by array position.
  std::vector<ScxCameraEditingPose> poses;

  /// All timed keys indexed by array position.
  std::vector<ScxCameraEditingKey> keys;

  /// All timeline segments indexed by array position.
  std::vector<ScxCameraEditingSegment> segments;

  /// All editing tracks indexed by array position.
  std::vector<ScxCameraEditingTrack> tracks;

  /// Finds the first track with the given context ID.
  /// Returns nullopt if not found.
  [[nodiscard]] std::optional<std::size_t> track_by_context_id(std::uint8_t context_id) const;

  /// Finds the first track with the given target script ID.
  /// Returns nullopt if not found.
  [[nodiscard]] std::optional<std::size_t> track_by_target_script_id(
      std::uint16_t target_script_id) const;
};

/// Parser for DEAD000A compiled camera-editing timelines.
class ScxCameraEditing {
 public:
  /// Parses a DEAD000A payload extracted from an SCX embedded resource.
  /// Validates all record counts, bounds, and graph references.
  /// Returns a structured error if the payload is malformed or unsupported.
  [[nodiscard]] static std::expected<ScxCameraEditingData, std::string> load(
      std::span<const std::byte> payload);

 private:
  /// Validates that all key camera IDs resolve to valid poses.
  [[nodiscard]] static std::expected<void, std::string> validate_and_resolve_keys(
      ScxCameraEditingData& data);

  /// Validates that all segment key references resolve to valid keys and
  /// resolves numeric key IDs to array indices.
  [[nodiscard]] static std::expected<void, std::string> validate_and_resolve_segments(
      ScxCameraEditingData& data);

  /// Validates that all track segment references resolve to valid segments and
  /// resolves numeric segment IDs to array indices.
  [[nodiscard]] static std::expected<void, std::string> validate_and_resolve_tracks(
      ScxCameraEditingData& data);

  /// Validates that key time intervals are non-degenerate (t_B > t_A).
  [[nodiscard]] static std::expected<void, std::string> validate_key_intervals(
      const ScxCameraEditingData& data);
};

}  // namespace App::Omikron

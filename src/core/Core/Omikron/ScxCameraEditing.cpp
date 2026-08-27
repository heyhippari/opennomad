#include "Core/Omikron/ScxCameraEditing.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/BinaryReader.hpp"

namespace App::Omikron {

namespace {

constexpr std::size_t K_HEADER_SIZE{0x24};
constexpr std::size_t K_POSE_STRIDE{0x34};
constexpr std::size_t K_KEY_STRIDE{0x1C};
constexpr std::size_t K_SEGMENT_STRIDE{0x18};
constexpr std::size_t K_TRACK_STRIDE{0x20};

constexpr std::size_t K_POSE_NAME_SIZE{12};
constexpr std::size_t K_SEGMENT_NAME_SIZE{10};
constexpr std::size_t K_TRACK_NAME_SIZE{11};

constexpr std::uint32_t K_MAX_SAFE_COUNT{1'000'000U};

std::string fixed_string(const std::span<const std::byte> bytes) {
  const void* raw{bytes.data()};
  const char* begin{static_cast<const char*>(raw)};
  const void* nul{std::memchr(raw, '\0', bytes.size())};
  const std::size_t length{
      nul == nullptr ? bytes.size()
                     : static_cast<std::size_t>(static_cast<const char*>(nul) - begin)};
  return std::string{begin, length};
}

/// Safely checks if count * stride would overflow or exceed max_bytes.
[[nodiscard]] bool safe_record_bounds(
    std::size_t count, std::size_t stride, std::size_t available_bytes) {
  if (count > K_MAX_SAFE_COUNT) {
    return false;
  }
  const std::size_t total{count * stride};
  return total <= available_bytes && total / stride == count;
}

}  // namespace

std::optional<std::size_t> ScxCameraEditingData::track_by_context_id(
    const std::uint8_t context_id) const {
  for (std::size_t index{0}; index < tracks.size(); ++index) {
    if (tracks.at(index).context_id == context_id) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> ScxCameraEditingData::track_by_target_script_id(
    const std::uint16_t target_script_id) const {
  for (std::size_t index{0}; index < tracks.size(); ++index) {
    if (tracks.at(index).target_script_id == target_script_id) {
      return index;
    }
  }
  return std::nullopt;
}

std::expected<ScxCameraEditingData, std::string> ScxCameraEditing::load(
    const std::span<const std::byte> payload) {
  if (payload.size() < K_HEADER_SIZE) {
    return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
        fmt::format("DEAD000A payload too small: {} bytes, expected at least {}", payload.size(),
            K_HEADER_SIZE)};
  }

  BinaryReader reader{payload};

  // Read header
  const std::uint32_t version{reader.read_u32()};
  const std::uint32_t camera_pose_count{reader.read_u32()};
  const std::uint32_t timed_key_count{reader.read_u32()};
  const std::uint32_t segment_count{reader.read_u32()};
  const std::uint32_t editing_track_count{reader.read_u32()};

  // Read reserved/placeholder dwords (not used by OpenNomad)
  static_cast<void>(reader.read_u32());  // serialized camera-array pointer
  static_cast<void>(reader.read_u32());  // serialized timed-key-array pointer
  static_cast<void>(reader.read_u32());  // serialized segment-array pointer
  static_cast<void>(reader.read_u32());  // serialized editing-track-array pointer

  if (reader.has_error()) {
    return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
        fmt::format("DEAD000A header read failed: {}", reader.error())};
  }

  // Validate version
  if (version != ScxCameraEditingData::k_supported_version) {
    return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
        fmt::format("DEAD000A unsupported version: {}, expected {}", version,
            ScxCameraEditingData::k_supported_version)};
  }

  // Validate counts
  if (!safe_record_bounds(camera_pose_count, K_POSE_STRIDE, payload.size() - K_HEADER_SIZE)) {
    return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
        fmt::format("DEAD000A camera pose array too large: {} poses", camera_pose_count)};
  }
  if (!safe_record_bounds(timed_key_count, K_KEY_STRIDE, payload.size() - K_HEADER_SIZE)) {
    return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
        fmt::format("DEAD000A timed key array too large: {} keys", timed_key_count)};
  }
  if (!safe_record_bounds(segment_count, K_SEGMENT_STRIDE, payload.size() - K_HEADER_SIZE)) {
    return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
        fmt::format("DEAD000A segment array too large: {} segments", segment_count)};
  }
  if (!safe_record_bounds(editing_track_count, K_TRACK_STRIDE, payload.size() - K_HEADER_SIZE)) {
    return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
        fmt::format("DEAD000A editing track array too large: {} tracks", editing_track_count)};
  }

  ScxCameraEditingData data;
  data.version = version;

  // Read camera poses
  data.poses.reserve(camera_pose_count);
  for (std::uint32_t index{0}; index < camera_pose_count; ++index) {
    ScxCameraEditingPose pose;
    pose.id = reader.read_u32();
    pose.name = fixed_string(reader.read_bytes(K_POSE_NAME_SIZE));
    pose.eye.x = reader.read_f32();
    pose.eye.y = reader.read_f32();
    pose.eye.z = reader.read_f32();
    pose.target.x = reader.read_f32();
    pose.target.y = reader.read_f32();
    pose.target.z = reader.read_f32();
    pose.roll_degrees = reader.read_f32();
    pose.horizontal_fov_degrees = reader.read_f32();
    pose.serialized_next_placeholder = reader.read_u32();

    if (reader.has_error()) {
      return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
          fmt::format("DEAD000A camera pose {} read failed: {}", index, reader.error())};
    }

    data.poses.push_back(pose);
  }

  // Read timed keys
  data.keys.reserve(timed_key_count);
  for (std::uint32_t index{0}; index < timed_key_count; ++index) {
    ScxCameraEditingKey key;
    key.id = reader.read_u32();
    key.camera_id = reader.read_u32();
    key.local_time_frames = reader.read_f32();
    key.unknown.at(0) = reader.read_u32();
    key.unknown.at(1) = reader.read_u32();
    key.unknown.at(2) = reader.read_u32();
    key.serialized_next_placeholder = reader.read_u32();

    if (reader.has_error()) {
      return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
          fmt::format("DEAD000A timed key {} read failed: {}", index, reader.error())};
    }

    data.keys.push_back(key);
  }

  // Read segment fixed records
  data.segments.reserve(segment_count);
  for (std::uint32_t index{0}; index < segment_count; ++index) {
    ScxCameraEditingSegment segment;
    segment.id = reader.read_u32();
    segment.name = fixed_string(reader.read_bytes(K_SEGMENT_NAME_SIZE));
    segment.serialized_key_reference_count = reader.read_u16();
    segment.serialized_refs_placeholder = reader.read_u32();
    segment.serialized_next_placeholder = reader.read_u32();

    if (reader.has_error()) {
      return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
          fmt::format("DEAD000A segment {} read failed: {}", index, reader.error())};
    }

    data.segments.push_back(segment);
  }

  // Read appended segment key-reference arrays
  std::size_t total_segment_refs{0};
  for (const ScxCameraEditingSegment& segment : data.segments) {
    // Check for overflow when summing
    if (total_segment_refs > K_MAX_SAFE_COUNT ||
        static_cast<std::size_t>(segment.serialized_key_reference_count) >
            K_MAX_SAFE_COUNT - total_segment_refs) {
      return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
          fmt::format("DEAD000A segment key references overflow")};
    }
    total_segment_refs += segment.serialized_key_reference_count;
  }

  if (!safe_record_bounds(total_segment_refs, sizeof(std::uint32_t), payload.size())) {
    return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
        fmt::format("DEAD000A segment reference array too large: {} refs", total_segment_refs)};
  }

  for (ScxCameraEditingSegment& segment : data.segments) {
    segment.key_indices.reserve(segment.serialized_key_reference_count);
    for (std::uint16_t i{0}; i < segment.serialized_key_reference_count; ++i) {
      const std::uint32_t key_id{reader.read_u32()};
      if (reader.has_error()) {
        return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
            fmt::format("DEAD000A segment key reference read failed: {}", reader.error())};
      }
      segment.key_indices.push_back(key_id);  // Resolve later
    }
  }

  // Read editing track fixed records
  data.tracks.reserve(editing_track_count);
  for (std::uint32_t index{0}; index < editing_track_count; ++index) {
    ScxCameraEditingTrack track;
    track.context_id = reader.read_u8();
    track.name = fixed_string(reader.read_bytes(K_TRACK_NAME_SIZE));
    track.serialized_segment_reference_count = reader.read_u16();
    track.unknown_0e = reader.read_u16();
    track.serialized_refs_placeholder = reader.read_u32();
    track.serialized_next_placeholder = reader.read_u32();
    track.total_duration_frames = reader.read_u32();
    track.target_script_id = reader.read_u16();
    track.unknown_1e = reader.read_u16();

    if (reader.has_error()) {
      return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
          fmt::format("DEAD000A editing track {} read failed: {}", index, reader.error())};
    }

    data.tracks.push_back(track);
  }

  // Read appended track segment-reference arrays
  std::size_t total_track_refs{0};
  for (const ScxCameraEditingTrack& track : data.tracks) {
    // Check for overflow when summing
    if (total_track_refs > K_MAX_SAFE_COUNT ||
        static_cast<std::uint32_t>(track.serialized_segment_reference_count) >
            K_MAX_SAFE_COUNT - total_track_refs) {
      return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
          fmt::format("DEAD000A track segment references overflow")};
    }
    total_track_refs += track.serialized_segment_reference_count;
  }

  if (!safe_record_bounds(total_track_refs, sizeof(std::uint32_t), payload.size())) {
    return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
        fmt::format("DEAD000A track reference array too large: {} refs", total_track_refs)};
  }

  for (ScxCameraEditingTrack& track : data.tracks) {
    track.segment_indices.reserve(track.serialized_segment_reference_count);
    for (std::uint16_t i{0}; i < track.serialized_segment_reference_count; ++i) {
      const std::uint32_t segment_id{reader.read_u32()};
      if (reader.has_error()) {
        return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
            fmt::format("DEAD000A track segment reference read failed: {}", reader.error())};
      }
      track.segment_indices.push_back(segment_id);  // Resolve later
    }
  }

  // Validate and resolve all graph references
  if (auto result = validate_and_resolve_keys(data)) {
    // Success
  } else {
    return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
        std::move(result).error()};
  }

  if (auto result = validate_and_resolve_segments(data)) {
    // Success
  } else {
    return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
        std::move(result).error()};
  }

  if (auto result = validate_and_resolve_tracks(data)) {
    // Success
  } else {
    return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
        std::move(result).error()};
  }

  if (auto result = validate_key_intervals(data)) {
    // Success
  } else {
    return std::expected<ScxCameraEditingData, std::string>{std::unexpect,
        std::move(result).error()};
  }

  App::Log::debug(LogCategory::SCX,
      "SCX camera editing: version={} cameras={} keys={} segments={} tracks={}",
      data.version,
      data.poses.size(),
      data.keys.size(),
      data.segments.size(),
      data.tracks.size());

  return data;
}

std::expected<void, std::string> ScxCameraEditing::validate_and_resolve_keys(
    ScxCameraEditingData& data) {
  for (std::size_t key_index{0}; key_index < data.keys.size(); ++key_index) {
    ScxCameraEditingKey& key{data.keys.at(key_index)};

    // Find first pose with matching camera ID
    bool found{false};
    for (std::size_t pose_index{0}; pose_index < data.poses.size(); ++pose_index) {
      if (data.poses.at(pose_index).id == key.camera_id) {
        key.camera_index = pose_index;
        found = true;
        break;  // First match wins
      }
    }

    if (!found) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("DEAD000A key {} references unknown camera ID {}", key_index,
              key.camera_id)};
    }
  }

  return std::expected<void, std::string>{};
}

std::expected<void, std::string> ScxCameraEditing::validate_and_resolve_segments(
    ScxCameraEditingData& data) {
  for (std::size_t segment_index{0}; segment_index < data.segments.size(); ++segment_index) {
    ScxCameraEditingSegment& segment{data.segments.at(segment_index)};

    // Resolve stored key IDs to key indices
    std::vector<std::size_t> resolved_indices;
    resolved_indices.reserve(segment.key_indices.size());

    for (std::size_t ref_index{0}; ref_index < segment.key_indices.size(); ++ref_index) {
      const std::uint32_t key_id{static_cast<std::uint32_t>(segment.key_indices.at(ref_index))};

      // Find first key with matching ID
      bool found{false};
      for (std::size_t key_index{0}; key_index < data.keys.size(); ++key_index) {
        if (data.keys.at(key_index).id == key_id) {
          resolved_indices.push_back(key_index);
          found = true;
          break;  // First match wins
        }
      }

      if (!found) {
        return std::expected<void, std::string>{std::unexpect,
            fmt::format("DEAD000A segment {} references unknown key ID {}", segment_index,
                key_id)};
      }
    }

    segment.key_indices = std::move(resolved_indices);
  }

  return std::expected<void, std::string>{};
}

std::expected<void, std::string> ScxCameraEditing::validate_and_resolve_tracks(
    ScxCameraEditingData& data) {
  for (std::size_t track_index{0}; track_index < data.tracks.size(); ++track_index) {
    ScxCameraEditingTrack& track{data.tracks.at(track_index)};

    // Resolve stored segment IDs to segment indices
    std::vector<std::size_t> resolved_indices;
    resolved_indices.reserve(track.segment_indices.size());

    for (std::size_t ref_index{0}; ref_index < track.segment_indices.size(); ++ref_index) {
      const std::uint32_t segment_id{static_cast<std::uint32_t>(track.segment_indices.at(ref_index))};

      // Find first segment with matching ID
      bool found{false};
      for (std::size_t segment_index{0}; segment_index < data.segments.size();
           ++segment_index) {
        if (data.segments.at(segment_index).id == segment_id) {
          resolved_indices.push_back(segment_index);
          found = true;
          break;  // First match wins
        }
      }

      if (!found) {
        return std::expected<void, std::string>{std::unexpect,
            fmt::format("DEAD000A track {} references unknown segment ID {}", track_index,
                segment_id)};
      }
    }

    track.segment_indices = std::move(resolved_indices);
  }

  return std::expected<void, std::string>{};
}

std::expected<void, std::string> ScxCameraEditing::validate_key_intervals(
    const ScxCameraEditingData& data) {
  for (std::size_t segment_index{0}; segment_index < data.segments.size(); ++segment_index) {
    const ScxCameraEditingSegment& segment{data.segments.at(segment_index)};

    // Check that consecutive keys have strictly increasing times
    for (std::size_t i{1}; i < segment.key_indices.size(); ++i) {
      const ScxCameraEditingKey& key_a{data.keys.at(segment.key_indices.at(i - 1))};
      const ScxCameraEditingKey& key_b{data.keys.at(segment.key_indices.at(i))};

      if (key_b.local_time_frames <= key_a.local_time_frames) {
        return std::expected<void, std::string>{std::unexpect,
            fmt::format(
                "DEAD000A segment {} has zero-duration key interval: key {} t={} -> key {} t={}",
                segment_index,
                key_a.id,
                key_a.local_time_frames,
                key_b.id,
                key_b.local_time_frames)};
      }
    }
  }

  return std::expected<void, std::string>{};
}

}  // namespace App::Omikron

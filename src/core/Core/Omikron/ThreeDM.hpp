#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Core/RuntimeMath.hpp"

namespace App::Omikron {

struct ThreeDmHeader {
  std::uint8_t stream_mode{0};
  std::uint32_t audio_bytes_per_frame{0};
  std::uint32_t morph_vertex_count{0};
  std::uint32_t field_08{0};
  std::vector<std::uint32_t> object_ids;
};

struct ThreeDmFrameLocation {
  std::size_t motion_offset{0};
  std::optional<std::size_t> audio_offset;
};

struct ThreeDmMorphVertex {
  Runtime::Vec3 position{};
  Runtime::Vec3 normal{};
};

struct ThreeDmFrame {
  Runtime::Vec3 root_translation{};
  std::vector<Runtime::Quaternion> object_rotations;
  std::vector<ThreeDmMorphVertex> morph_vertices;
};

/// Immutable, bounds-validated Runtime dialogue-performance stream.
class ThreeDM {
 public:
  static constexpr std::uint32_t k_max_morph_vertices{200};
  static constexpr std::uint32_t k_max_objects{30};

  [[nodiscard]] static std::expected<ThreeDM, std::string> load(
      std::span<const std::byte> data);

  [[nodiscard]] const ThreeDmHeader& header() const {
    return m_header;
  }
  [[nodiscard]] std::span<const ThreeDmFrameLocation> frames() const {
    return m_frames;
  }
  [[nodiscard]] std::size_t motion_size() const {
    return m_motion_size;
  }
  [[nodiscard]] std::size_t audio_chunk_count() const;
  [[nodiscard]] std::expected<ThreeDmFrame, std::string> decode_frame(
      std::size_t frame_index, std::size_t root_object_slot) const;
  [[nodiscard]] std::span<const std::byte> audio_chunk(std::size_t frame_index) const;

 private:
  ThreeDmHeader m_header;
  std::vector<ThreeDmFrameLocation> m_frames;
  std::vector<std::byte> m_bytes;
  std::size_t m_motion_size{0};
};

}  // namespace App::Omikron

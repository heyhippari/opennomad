#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Omikron/IamCamera.hpp"

namespace App::Omikron {

/// Decoded immutable metadata for one 0x40-byte IAM/DIALOG node.
struct IamDialogNode {
  static constexpr std::size_t k_serialized_size{0x40};
  static constexpr std::size_t k_response_count{4};

  std::array<std::uint32_t, k_response_count> condition_script_offsets{};
  std::array<std::uint32_t, k_response_count> action_script_offsets{};
  std::uint32_t strings_offset{0};
  std::array<std::int16_t, k_response_count> target_node_ids{};
  std::int16_t node_id{0};
  std::string face_motion_base;
  std::array<std::int16_t, 2> response_camera_ids{};
  std::array<std::int16_t, 2> line_camera_ids{};
};

/// Parsed owning representation of one record selected from IAM/DIALOG.
/// Serialized record-relative offsets stay in the owned byte copy and are
/// resolved into checked views on demand.
class IamDialogRecord {
 public:
  static constexpr std::size_t k_header_size{0x08};
  static constexpr std::size_t k_string_count{6};

  [[nodiscard]] static std::expected<IamDialogRecord, std::string> load(
      std::span<const std::byte> record);
  [[nodiscard]] static std::expected<IamDialogRecord, std::string> load_from_archive(
      std::span<const std::byte> archive, std::uint16_t dialog_id);

  [[nodiscard]] std::int16_t character_id() const;
  [[nodiscard]] std::int16_t node_count() const;
  [[nodiscard]] std::int16_t camera_count() const;
  [[nodiscard]] std::int16_t camera_count_mirror() const;
  [[nodiscard]] std::size_t record_size() const {
    return m_bytes.size();
  }

  [[nodiscard]] std::optional<IamDialogNode> node_by_id(std::int16_t node_id) const;
  [[nodiscard]] std::optional<IamCameraRecord> camera_by_id(std::int16_t camera_id) const;
  [[nodiscard]] std::string_view main_line(const IamDialogNode& node) const;
  [[nodiscard]] std::string_view response_text(const IamDialogNode& node, std::size_t slot) const;
  [[nodiscard]] std::string_view automatic_player_line(const IamDialogNode& node) const;
  [[nodiscard]] std::span<const std::byte> condition_program(
      const IamDialogNode& node, std::size_t slot) const;
  [[nodiscard]] std::span<const std::byte> action_program(
      const IamDialogNode& node, std::size_t slot) const;

 private:
  explicit IamDialogRecord(std::vector<std::byte> bytes) : m_bytes(std::move(bytes)) {}

  [[nodiscard]] IamDialogNode node_at(std::size_t index) const;
  [[nodiscard]] std::string_view string_at(const IamDialogNode& node, std::size_t index) const;
  [[nodiscard]] std::span<const std::byte> program_at(std::uint32_t offset) const;

  std::vector<std::byte> m_bytes;
};

}  // namespace App::Omikron

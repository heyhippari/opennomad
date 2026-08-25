#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace App::Omikron {

/// Shared 0x2C-byte camera ABI embedded by IAM/AREA and IAM/DIALOG.
struct IamCameraRecord {
  static constexpr std::size_t k_serialized_size{0x2C};

  /// Serialized camera-coordinate dwords. Runtime's AREA/SCENE loader
  /// normalizes these in place before compact camera handlers consume them;
  /// OpenNomad preserves the archive values and converts at the runtime boundary.
  std::array<std::int32_t, 3> serialized_eye{};
  std::array<std::int32_t, 3> serialized_target{};
  std::int16_t camera_id{0};
  std::uint16_t camera_type{0};
  std::int16_t roll_units{0};
  std::int16_t horizontal_fov_units{0};
  /// +0x20. -1 is absolute; 0 attaches using body-offset orientation only;
  /// 1 attaches using actor-base + body-offset orientation.
  std::int16_t target_attachment_selector{0};
  /// +0x22. Same attachment-mode encoding as target_attachment_selector.
  std::int16_t eye_attachment_selector{0};
  std::array<std::uint16_t, 4> tail_fields{};
};

[[nodiscard]] std::expected<IamCameraRecord, std::string> parse_iam_camera(
    std::span<const std::byte> data);

}  // namespace App::Omikron

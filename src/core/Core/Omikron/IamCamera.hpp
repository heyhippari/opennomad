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

  std::array<std::int32_t, 3> serialized_eye{};
  std::array<std::int32_t, 3> serialized_target{};
  std::int16_t camera_id{0};
  std::uint16_t camera_type{0};
  std::int16_t roll_units{0};
  std::int16_t horizontal_fov_units{0};
  std::int16_t field_20{0};
  std::int16_t field_22{0};
  std::array<std::uint16_t, 4> tail_fields{};
};

[[nodiscard]] std::expected<IamCameraRecord, std::string> parse_iam_camera(
    std::span<const std::byte> data);

}  // namespace App::Omikron

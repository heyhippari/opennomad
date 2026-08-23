#include "Core/Omikron/IamCamera.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>

namespace App::Omikron {

namespace {

template <typename Value>
Value read_at(const std::span<const std::byte> data, const std::size_t offset) {
  Value value{};
  std::memcpy(&value, data.subspan(offset, sizeof(Value)).data(), sizeof(Value));
  return value;
}

}  // namespace

std::expected<IamCameraRecord, std::string> parse_iam_camera(
    const std::span<const std::byte> data) {
  if (data.size() < IamCameraRecord::k_serialized_size) {
    return std::expected<IamCameraRecord, std::string>{std::unexpect,
        fmt::format("IAM camera: truncated record ({} bytes, expected {:#x})",
            data.size(),
            IamCameraRecord::k_serialized_size)};
  }

  IamCameraRecord camera;
  for (std::size_t axis{0}; axis < camera.serialized_eye.size(); ++axis) {
    camera.serialized_eye.at(axis) = read_at<std::int32_t>(data, axis * 4U);
    camera.serialized_target.at(axis) = read_at<std::int32_t>(data, 0x0CU + (axis * 4U));
  }
  camera.camera_id = read_at<std::int16_t>(data, 0x18U);
  camera.camera_type = read_at<std::uint16_t>(data, 0x1AU);
  camera.roll_units = read_at<std::int16_t>(data, 0x1CU);
  camera.horizontal_fov_units = read_at<std::int16_t>(data, 0x1EU);
  camera.field_20 = read_at<std::int16_t>(data, 0x20U);
  camera.field_22 = read_at<std::int16_t>(data, 0x22U);
  for (std::size_t index{0}; index < camera.tail_fields.size(); ++index) {
    camera.tail_fields.at(index) = read_at<std::uint16_t>(data, 0x24U + (index * 2U));
  }
  return camera;
}

}  // namespace App::Omikron

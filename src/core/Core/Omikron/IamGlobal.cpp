#include "Core/Omikron/IamGlobal.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Omikron/IamCamera.hpp"

namespace App::Omikron {

namespace {

template <typename Value>
Value read_at(const std::span<const std::byte> data, const std::size_t offset) {
  Value value{};
  std::memcpy(&value, data.subspan(offset, sizeof(Value)).data(), sizeof(value));
  return value;
}

}  // namespace

std::expected<IamGlobal, std::string> IamGlobal::load(const std::span<const std::byte> data) {
  APP_PROFILE_FUNCTION();

  if (data.size() < k_minimum_header_size) {
    return std::expected<IamGlobal, std::string>{std::unexpect,
        fmt::format("IAM/GLOBAL: too small ({} bytes, expected at least {:#x})",
            data.size(),
            k_minimum_header_size)};
  }

  const std::int16_t signed_count{read_at<std::int16_t>(data, k_offset_camera_count)};
  if (signed_count < 0) {
    return std::expected<IamGlobal, std::string>{std::unexpect,
        fmt::format("IAM/GLOBAL: camera table has negative count {}", signed_count)};
  }

  const std::size_t count{static_cast<std::size_t>(signed_count)};
  if (count == 0U) {
    return IamGlobal{std::vector<IamCameraRecord>{}};
  }

  const std::uint32_t serialized_offset{
      read_at<std::uint32_t>(data, k_offset_camera_table)};
  const std::size_t camera_offset{serialized_offset};
  if (camera_offset > data.size()) {
    return std::expected<IamGlobal, std::string>{std::unexpect,
        fmt::format("IAM/GLOBAL: camera table offset {:#x} exceeds the {:#x}-byte file",
            serialized_offset,
            data.size())};
  }
  const std::size_t remaining{data.size() - camera_offset};
  if (count > remaining / IamCameraRecord::k_serialized_size) {
    return std::expected<IamGlobal, std::string>{std::unexpect,
        fmt::format("IAM/GLOBAL: {} camera records at {:#x} exceed the {:#x}-byte file",
            count,
            serialized_offset,
            data.size())};
  }

  std::vector<IamCameraRecord> cameras;
  cameras.reserve(count);
  for (std::size_t index{0}; index < count; ++index) {
    const std::size_t offset{
        camera_offset + (index * IamCameraRecord::k_serialized_size)};
    auto camera{parse_iam_camera(
        data.subspan(offset, IamCameraRecord::k_serialized_size))};
    if (!camera) {
      return std::expected<IamGlobal, std::string>{std::unexpect,
          fmt::format("IAM/GLOBAL: camera {}: {}", index, camera.error())};
    }
    cameras.push_back(std::move(camera).value());
  }
  return IamGlobal{std::move(cameras)};
}

std::optional<IamCameraRecord> IamGlobal::camera_by_id(
    const std::int16_t camera_id) const {
  for (const IamCameraRecord& camera : m_cameras) {
    if (camera.camera_id == camera_id) {
      return camera;
    }
  }
  return std::nullopt;
}

}  // namespace App::Omikron

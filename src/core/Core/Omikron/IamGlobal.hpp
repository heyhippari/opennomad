#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Omikron/IamCamera.hpp"

namespace App::Omikron {

/// Parsed immutable camera subset of the session-wide IAM/GLOBAL resource.
/// Other GLOBAL sections remain opaque and are intentionally not relocated.
class IamGlobal {
 public:
  static constexpr std::size_t k_minimum_header_size{0x20U};
  static constexpr std::size_t k_offset_camera_table{0x14U};
  static constexpr std::size_t k_offset_camera_count{0x1EU};

  [[nodiscard]] static std::expected<IamGlobal, std::string> load(std::span<const std::byte> data);

  [[nodiscard]] std::span<const IamCameraRecord> cameras() const {
    return m_cameras;
  }

  /// Returns the first matching record in serialized table order.
  [[nodiscard]] std::optional<IamCameraRecord> camera_by_id(std::int16_t camera_id) const;

 private:
  explicit IamGlobal(std::vector<IamCameraRecord> cameras) : m_cameras(std::move(cameras)) {}

  std::vector<IamCameraRecord> m_cameras;
};

}  // namespace App::Omikron

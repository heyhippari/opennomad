#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "Core/RuntimeMath.hpp"

namespace App::Omikron {

struct Path3DPPoint {
  std::uint32_t key{0};
  Runtime::Vec3 position{};
  Runtime::Quaternion quaternion{};
};

struct Path3DPSample {
  Runtime::Vec3 position{};
  Runtime::Quaternion quaternion{};
};

/// One named subpath in a 3DP payload. field_14 is intentionally opaque.
struct Path3DPSubpath {
  std::string name;
  std::uint32_t field_14{0};
  std::vector<Path3DPPoint> points;

  /// Runtime interpolation mode 1: linear XYZ and normalized linear wxyz.
  [[nodiscard]] std::expected<Path3DPSample, std::string> sample_mode_1(float parameter) const;
};

/// Immutable decoded SCX 3DP payload.
struct Path3DP {
  std::vector<Path3DPSubpath> subpaths;

  [[nodiscard]] static std::expected<Path3DP, std::string> load(
      std::span<const std::byte> data);
};

}  // namespace App::Omikron

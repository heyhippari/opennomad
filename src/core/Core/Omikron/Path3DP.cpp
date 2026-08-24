#include "Core/Omikron/Path3DP.hpp"

#include <fmt/format.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Omikron/BinaryReader.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Omikron {

namespace {

constexpr std::uint32_t K_MAX_PATH_COUNT{65536};
constexpr std::uint32_t K_MAX_POINT_COUNT{4U * 1024U * 1024U};

std::string read_fixed_string(BinaryReader& reader, const std::size_t length) {
  const std::span<const std::byte> bytes{reader.read_bytes(length)};
  std::string result;
  result.reserve(length);
  for (const std::byte byte : bytes) {
    const char character{static_cast<char>(byte)};
    if (character == '\0') {
      break;
    }
    result.push_back(character);
  }
  return result;
}

Runtime::Quaternion runtime_quaternion_interpolation(
    const Runtime::Quaternion& from, const Runtime::Quaternion& to, const float amount) {
  const float dot{(from.w * to.w) + (from.x * to.x) + (from.y * to.y) + (from.z * to.z)};
  const float angle{std::acos(dot)};
  float from_weight{1.0F - amount};
  float to_weight{amount};
  if (angle > 0.1F) {
    const float sine{std::sin(angle)};
    from_weight = std::sin(angle * (1.0F - amount)) / sine;
    to_weight = std::sin(angle * amount) / sine;
  }
  return Runtime::Quaternion{.w = (from.w * from_weight) + (to.w * to_weight),
      .x = (from.x * from_weight) + (to.x * to_weight),
      .y = (from.y * from_weight) + (to.y * to_weight),
      .z = (from.z * from_weight) + (to.z * to_weight)};
}

}  // namespace

std::expected<Path3DPSample, std::string> Path3DPSubpath::sample_mode_1(
    const float parameter) const {
  if (points.empty()) {
    return std::expected<Path3DPSample, std::string>{
        std::unexpect, fmt::format("3DP subpath '{}' contains no points", name)};
  }
  if (points.size() == 1U || parameter == static_cast<float>(points.front().key)) {
    return Path3DPSample{
        .position = points.front().position, .quaternion = points.front().quaternion};
  }
  if (parameter == static_cast<float>(points.back().key)) {
    return Path3DPSample{
        .position = points.back().position, .quaternion = points.back().quaternion};
  }

  if (parameter < static_cast<float>(points.front().key) ||
      parameter > static_cast<float>(points.back().key)) {
    return std::expected<Path3DPSample, std::string>{std::unexpect,
        fmt::format("3DP subpath '{}' parameter {} is outside [{}, {}]",
            name,
            parameter,
            points.front().key,
            points.back().key)};
  }

  for (std::size_t index{1}; index < points.size(); ++index) {
    const Path3DPPoint& to{points.at(index)};
    if (parameter > static_cast<float>(to.key)) {
      continue;
    }
    const Path3DPPoint& from{points.at(index - 1U)};
    if (to.key <= from.key) {
      return std::expected<Path3DPSample, std::string>{
          std::unexpect, fmt::format("3DP subpath '{}' keys are not strictly increasing", name)};
    }
    const float amount{
        (parameter - static_cast<float>(from.key)) / static_cast<float>(to.key - from.key)};
    return Path3DPSample{
        .position =
            Runtime::Vec3{.x = from.position.x + ((to.position.x - from.position.x) * amount),
                .y = from.position.y + ((to.position.y - from.position.y) * amount),
                .z = from.position.z + ((to.position.z - from.position.z) * amount)},
        .quaternion = runtime_quaternion_interpolation(from.quaternion, to.quaternion, amount)};
  }
  return std::expected<Path3DPSample, std::string>{
      std::unexpect, fmt::format("3DP subpath '{}' cannot bracket parameter {}", name, parameter)};
}

std::expected<Path3DP, std::string> Path3DP::load(const std::span<const std::byte> data) {
  BinaryReader reader{data};
  Path3DP path;
  const std::uint32_t path_count{reader.read_u32()};
  if (reader.has_error()) {
    return std::expected<Path3DP, std::string>{std::unexpect, reader.error()};
  }
  if (path_count > K_MAX_PATH_COUNT) {
    return std::expected<Path3DP, std::string>{
        std::unexpect, fmt::format("implausible 3DP path count {}", path_count)};
  }
  path.subpaths.reserve(path_count);
  for (std::uint32_t index{0}; index < path_count; ++index) {
    Path3DPSubpath subpath;
    subpath.name = read_fixed_string(reader, 20);
    subpath.max_parameter = reader.read_u32();
    const std::uint32_t point_count{reader.read_u32()};
    if (point_count > K_MAX_POINT_COUNT) {
      return std::expected<Path3DP, std::string>{std::unexpect,
          fmt::format(
              "3DP subpath '{}' has implausible point count {}", subpath.name, point_count)};
    }
    subpath.points.reserve(point_count);
    for (std::uint32_t point_index{0}; point_index < point_count; ++point_index) {
      Path3DPPoint point;
      point.key = reader.read_u32();
      point.position =
          Runtime::Vec3{.x = reader.read_f32(), .y = reader.read_f32(), .z = reader.read_f32()};
      point.quaternion = Runtime::Quaternion{.w = reader.read_f32(),
          .x = reader.read_f32(),
          .y = reader.read_f32(),
          .z = reader.read_f32()};
      subpath.points.push_back(point);
    }
    if (reader.has_error()) {
      return std::expected<Path3DP, std::string>{
          std::unexpect, fmt::format("3DP subpath {}: {}", index, reader.error())};
    }
    path.subpaths.push_back(std::move(subpath));
  }
  return path;
}

}  // namespace App::Omikron

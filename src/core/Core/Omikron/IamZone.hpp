#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace App::Omikron {

/// One immutable 0x44-byte IAM table-2 spatial-zone record shared by AREA
/// and SCENE. The three serialized program offsets remain neutral because
/// only event 1's initial qualifying-contact role is recovered.
struct IamZoneRecord {
  std::array<std::uint32_t, 3> event_offsets{};
  std::array<std::array<std::int32_t, 3>, 4> serialized_vertices{};
  std::int16_t orientation_center_units{0};
  std::int16_t orientation_span_units{0};
  std::int16_t zone_id{0};
  std::int16_t unknown_42{0};

  /// Runtime's X/Z even/odd polygon test; Y is deliberately ignored.
  [[nodiscard]] bool contains_xz(std::int32_t x, std::int32_t z) const;
  /// Runtime's independent heading filter; a zero span imposes no limit.
  [[nodiscard]] bool accepts_heading_degrees(float heading_degrees) const;
};

/// Decodes a validated fixed-width IAM table-2 record without relocating its
/// record-relative event offsets into pointers.
inline IamZoneRecord parse_iam_zone_record(const std::span<const std::byte, 0x44> record) {
  IamZoneRecord result;
  std::memcpy(result.event_offsets.data(), record.data(), sizeof(result.event_offsets));
  std::memcpy(result.serialized_vertices.data(),
      record.subspan(0x0CU, sizeof(result.serialized_vertices)).data(),
      sizeof(result.serialized_vertices));
  std::memcpy(&result.orientation_center_units,
      record.subspan(0x3CU).data(),
      sizeof(result.orientation_center_units));
  std::memcpy(&result.orientation_span_units,
      record.subspan(0x3EU).data(),
      sizeof(result.orientation_span_units));
  std::memcpy(&result.zone_id, record.subspan(0x40U).data(), sizeof(result.zone_id));
  std::memcpy(&result.unknown_42, record.subspan(0x42U).data(), sizeof(result.unknown_42));
  return result;
}

inline bool IamZoneRecord::contains_xz(const std::int32_t x, const std::int32_t z) const {
  bool inside{false};
  for (std::size_t current{0}, previous{serialized_vertices.size() - 1U};
      current < serialized_vertices.size();
      previous = current++) {
    const auto& vertex{serialized_vertices.at(current)};
    const auto& prior{serialized_vertices.at(previous)};
    if ((vertex.at(2) > z) == (prior.at(2) > z)) {
      continue;
    }
    const std::int64_t left{(static_cast<std::int64_t>(x) - vertex.at(0)) *
                            (static_cast<std::int64_t>(prior.at(2)) - vertex.at(2))};
    const std::int64_t right{(static_cast<std::int64_t>(prior.at(0)) - vertex.at(0)) *
                             (static_cast<std::int64_t>(z) - vertex.at(2))};
    if ((prior.at(2) > vertex.at(2)) ? (left < right) : (left > right)) {
      inside = !inside;
    }
  }
  return inside;
}

inline bool IamZoneRecord::accepts_heading_degrees(const float heading_degrees) const {
  if (orientation_span_units == 0) {
    return true;
  }
  constexpr float k_degrees_per_unit{360.0F / 4096.0F};
  const float center_degrees{static_cast<float>(orientation_center_units) * k_degrees_per_unit};
  const float half_span_degrees{
      std::abs(static_cast<float>(orientation_span_units) * k_degrees_per_unit) * 0.5F};
  const float delta{std::remainder(heading_degrees - center_degrees, 360.0F)};
  return delta >= -half_span_degrees && delta <= half_span_degrees;
}

}  // namespace App::Omikron

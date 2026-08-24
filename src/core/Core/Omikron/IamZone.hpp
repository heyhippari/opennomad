#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace App::Omikron {

/// One immutable 0x44-byte IAM table-2 zone/trigger record shared by AREA
/// and SCENE. The three serialized program offsets stay neutral because their
/// event meanings have not yet been recovered.
struct IamZoneRecord {
  std::array<std::uint32_t, 3> event_offsets{};
  std::array<std::byte, 0x32> raw_geometry_and_fields{};
  std::int16_t field_3e{0};
  std::int16_t zone_id{0};
  std::array<std::byte, 2> raw_tail{};
};

/// Decodes a validated fixed-width IAM table-2 record without relocating its
/// record-relative event offsets into pointers.
inline IamZoneRecord parse_iam_zone_record(const std::span<const std::byte, 0x44> record) {
  IamZoneRecord result;
  std::memcpy(result.event_offsets.data(), record.data(), sizeof(result.event_offsets));
  std::memcpy(result.raw_geometry_and_fields.data(),
      record.subspan(0x0CU).data(),
      result.raw_geometry_and_fields.size());
  std::memcpy(&result.field_3e, record.subspan(0x3EU).data(), sizeof(result.field_3e));
  std::memcpy(&result.zone_id, record.subspan(0x40U).data(), sizeof(result.zone_id));
  std::memcpy(result.raw_tail.data(), record.subspan(0x42U).data(), result.raw_tail.size());
  return result;
}

}  // namespace App::Omikron

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace App::Omikron {

/// Shared 0x18-byte IAM/AREA and IAM/SCENE table-1 object placement.
///
/// Runtime treats +0x16 as an index into START's packed two-bit persistent
/// state. Bit 0 gates materialization; bit 1 controls whether the materialized
/// object is linked into the resident world hierarchy.
struct IamObjectPlacementRecord {
  std::int16_t runtime_object_slot_seed{0};
  std::int16_t object_id{0};
  std::array<std::int32_t, 3> serialized_position{};
  std::array<std::int16_t, 3> orientation_units{};
  std::int16_t persistent_state_index{0};
};

/// Shared 0x18-byte IAM/AREA and IAM/SCENE table-3 object definition.
///
/// The paired table-1/table-3 records have the same OBJECTS ID. The five
/// intermediate words remain semantically neutral; the final ten bytes are the
/// NUL-padded MESHES/OBJETS resource stem used by Runtime.
struct IamObjectDefinitionRecord {
  std::int16_t object_id{0};
  std::uint16_t type_or_flags{0};
  std::array<std::uint16_t, 5> fields_04_0c{};
  std::string model_resource;
};

[[nodiscard]] IamObjectPlacementRecord parse_iam_object_placement(
    std::span<const std::byte, 0x18> record);

[[nodiscard]] IamObjectDefinitionRecord parse_iam_object_definition(
    std::span<const std::byte, 0x18> record);

}  // namespace App::Omikron
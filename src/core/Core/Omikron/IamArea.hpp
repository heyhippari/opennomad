#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Core/Omikron/IamCamera.hpp"
#include "Core/Omikron/IamCharacterDefinition.hpp"
#include "Core/Omikron/IamObjectPlacement.hpp"
#include "Core/Omikron/IamZone.hpp"

namespace App::Omikron {

/// One 0x14-byte AREA table-0 character-placement record.
///
/// The record identifies a runtime character and provides its AREA-local
/// transform. The final word at +0x12 participates in the character/body
/// definition relationship, but its exact semantics are intentionally left
/// unnamed until that relationship is implemented.
struct IamAreaCharacterRecord {
  std::int16_t field_00{0};                           ///< +0x00, observed -1 for area 118.
  std::int16_t character_id{0};                       ///< +0x02, CHARACTERS ID.
  std::array<std::int32_t, 3> serialized_position{};  ///< +0x04..+0x0C raw integers.
  std::int16_t orientation_units{0};                  ///< +0x10, Runtime angle units.
  /// +0x12. Runtime opcode 0x4E passes this value to the persistent
  /// one-bit state setter at 0x0040AF30. It is not the table-4 character ID.
  std::uint16_t state_bit_index{0};
};

/// AREA table-4 uses the shared complete 0x114-byte authored definition.
using IamAreaCharacterDefinitionRecord = IamCharacterDefinition;

/// AREA table 1/table 3 use the shared object placement/definition formats.
using IamAreaObjectPlacementRecord = IamObjectPlacementRecord;
using IamAreaObjectDefinitionRecord = IamObjectDefinitionRecord;

/// AREA table-2 has the same physical and semantic record layout as SCENE.
using IamAreaZoneRecord = IamZoneRecord;

/// One named 0x10-byte spawn/address entry from AREA table 5.
struct IamAreaAddressRecord {
  std::array<std::int32_t, 3> serialized_position{};
  std::int16_t orientation_units{0};
  std::int16_t address_id{0};
};

/// One 0x2C-byte AREA table-6 camera record.
///
/// Runtime's camera selection handlers copy the first two 3-vectors into the
/// active camera definition. +0x1C is roll and +0x1E is horizontal FOV, both
/// in signed Runtime angle units. Remaining attachment fields stay unresolved.
using IamAreaCameraRecord = IamCameraRecord;

/// Parsed, owning representation of one IAM/AREA record. Serialized offsets
/// remain immutable; runtime values (script span, table views) are computed
/// on demand from the owned bytes, never overwritten into pointers.
class IamAreaRecord {
 public:
  /// Fixed header size of a serialized area record.
  static constexpr std::size_t k_header_size{0xB4};
  /// Width of each fixed dependency-name field.
  static constexpr std::size_t k_name_field_size{9};
  /// Number of offset/count table pairs in the header.
  static constexpr std::size_t k_table_count{8};

  /// Header field offsets (serialized layout only).
  static constexpr std::size_t k_offset_runtime_context{0x00};
  static constexpr std::size_t k_offset_script{0x04};
  static constexpr std::size_t k_offset_related_area_ids{0x08};
  static constexpr std::size_t k_offset_table_offsets{0x28};
  static constexpr std::size_t k_offset_table_counts{0x48};
  static constexpr std::size_t k_offset_model3do_name{0x58};
  static constexpr std::size_t k_offset_scenario_scx_name{0x61};
  static constexpr std::size_t k_offset_map_mpt_name{0x6A};
  static constexpr std::size_t k_offset_options_opt_name{0x73};
  static constexpr std::size_t k_offset_animation_ani_name{0x7C};
  static constexpr std::size_t k_offset_sky_3do_name{0x85};

  /// Parses an area record, validating the fixed header, the script offset
  /// and every known table span. The record owns its byte copy.
  [[nodiscard]] static std::expected<IamAreaRecord, std::string> load(
      std::span<const std::byte> data);

  /// Total record size in bytes.
  [[nodiscard]] std::size_t record_size() const {
    return m_bytes.size();
  }

  /// Runtime context placeholder (+0x00); zero on disk, kept as a runtime
  /// field rather than a pointer.
  [[nodiscard]] std::uint32_t runtime_context() const;

  /// Byte offset of the startup script within the record (+0x04).
  [[nodiscard]] std::uint32_t script_offset() const;

  /// Dependency names (+0x58..+0x8D), trimmed at the first NUL.
  [[nodiscard]] std::string model3do_name() const;
  [[nodiscard]] std::string scenario_scx_name() const;
  [[nodiscard]] std::string map_mpt_name() const;
  [[nodiscard]] std::string options_opt_name() const;
  [[nodiscard]] std::string animation_ani_name() const;
  [[nodiscard]] std::string sky_3do_name() const;

  /// The startup script bytes: `[scriptOffset, record end)`.
  [[nodiscard]] std::span<const std::byte> script_bytes() const;

  /// Complete immutable serialized record used by record-relative zone events.
  [[nodiscard]] std::span<const std::byte> record_bytes() const {
    return m_bytes;
  }

  /// Raw table offset (+0x28 + 4 * index).
  [[nodiscard]] std::uint32_t table_offset(std::size_t index) const;
  /// Raw table count (+0x48 + 2 * index).
  [[nodiscard]] std::uint16_t table_count(std::size_t index) const;

  /// Validated byte view of one table using its known stride. An empty table
  /// resolves to an empty span; a non-empty table whose stride is still
  /// unresolved is a structured error rather than a guessed view.
  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> table_view(
      std::size_t index) const;

  /// Finds a table-0 character-placement record by its signed character ID.
  [[nodiscard]] std::optional<IamAreaCharacterRecord> character_by_id(
      std::int16_t character_id) const;

  /// Finds the authored table-4 record belonging to a character ID.
  [[nodiscard]] std::optional<IamAreaCharacterDefinitionRecord>
  character_definition_by_character_id(std::int16_t character_id) const;

  [[nodiscard]] std::vector<IamAreaObjectPlacementRecord> object_placements() const;
  [[nodiscard]] std::optional<IamAreaObjectPlacementRecord> object_by_id(
      std::int16_t object_id) const;
  [[nodiscard]] std::vector<IamAreaObjectDefinitionRecord> object_definitions() const;
  [[nodiscard]] std::optional<IamAreaObjectDefinitionRecord> object_definition_by_object_id(
      std::int16_t object_id) const;

  /// Finds a table-6 camera by its signed camera ID.
  [[nodiscard]] std::optional<IamAreaCameraRecord> camera_by_id(std::int16_t camera_id) const;

  /// Finds a named table-5 address by its signed address ID.
  [[nodiscard]] std::optional<IamAreaAddressRecord> address_by_id(std::int16_t address_id) const;

  /// Immutable AREA table-2 zone records. Event offsets remain serialized
  /// offsets; trigger semantics are deliberately not inferred here.
  [[nodiscard]] std::vector<IamAreaZoneRecord> zones() const;

  /// Known serialized stride of each recovered AREA table family.
  [[nodiscard]] static std::optional<std::size_t> known_table_stride(
      std::size_t index);

 private:
  explicit IamAreaRecord(std::vector<std::byte> bytes) : m_bytes(std::move(bytes)) {}

  /// Reads a fixed-width name field and trims it at the first NUL.
  [[nodiscard]] std::string name_at(std::size_t offset) const;

  std::vector<std::byte> m_bytes;
};

}  // namespace App::Omikron

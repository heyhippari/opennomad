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
#include "Core/Omikron/IamZone.hpp"

namespace App::Omikron {

/// One 0x14-byte IAM/SCENE table-0 character placement. The layout is shared
/// with IAM/AREA, but SCENE owns a distinct immutable representation.
struct IamSceneCharacterRecord {
  std::int16_t runtime_slot_seed{0};
  std::int16_t character_id{0};
  std::array<std::int32_t, 3> serialized_position{};
  std::int16_t orientation_units{0};
  std::uint16_t state_bit_index{0};
};

/// SCENE table-4 uses the shared complete 0x114-byte authored definition.
using IamSceneCharacterDefinitionRecord = IamCharacterDefinition;

/// One 0x18-byte SCENE table-1 object placement. The last authored word is
/// intentionally neutral until its persistent-state semantics are recovered.
struct IamSceneObjectPlacementRecord {
  std::int16_t runtime_object_slot_seed{0};
  std::int16_t object_id{0};
  std::array<std::int32_t, 3> serialized_position{};
  std::array<std::int16_t, 3> orientation_units{};
  std::uint16_t persistent_state_field{0};
};

/// Minimal 0x18-byte SCENE table-3 object-definition representation.
struct IamSceneObjectDefinitionRecord {
  std::int16_t object_id{0};
  std::array<std::byte, 0x16> raw_tail{};
};

/// SCENE table-2 has the same physical and semantic record layout as AREA.
using IamSceneZoneRecord = IamZoneRecord;

/// One 0x08-byte SCENE table-7 link record. Its higher-level meaning remains
/// unresolved; nonzero program offsets are retained as serialized offsets.
struct IamSceneScriptLinkRecord {
  std::uint32_t program_offset{0};
  std::int32_t field_04{0};
};

/// Parsed, owning representation of one IAM/SCENE record. Offsets stay as
/// immutable serialized values; the parser never relocates them into pointers.
class IamSceneRecord {
 public:
  static constexpr std::size_t k_header_size{0x44};
  static constexpr std::size_t k_table_count{8};
  static constexpr std::size_t k_offset_runtime_context{0x00};
  static constexpr std::size_t k_offset_script{0x04};
  static constexpr std::size_t k_offset_table_offsets{0x08};
  static constexpr std::size_t k_offset_table_counts{0x28};

  [[nodiscard]] static std::expected<IamSceneRecord, std::string> load(
      std::span<const std::byte> data);

  [[nodiscard]] std::size_t record_size() const {
    return m_bytes.size();
  }
  [[nodiscard]] std::uint32_t runtime_context_placeholder() const;
  [[nodiscard]] std::uint32_t script_offset() const;
  [[nodiscard]] std::uint32_t table_offset(std::size_t index) const;
  [[nodiscard]] std::int16_t table_count(std::size_t index) const;
  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> table_view(
      std::size_t index) const;

  /// SCENE top-level code is exactly [script_offset, table6_offset); it never
  /// includes the physically following camera records.
  [[nodiscard]] std::span<const std::byte> script_bytes() const;

  /// Complete immutable serialized record used by record-relative zone events.
  [[nodiscard]] std::span<const std::byte> record_bytes() const {
    return m_bytes;
  }

  [[nodiscard]] std::vector<IamSceneCharacterRecord> character_placements() const;
  [[nodiscard]] std::optional<IamSceneCharacterRecord> character_by_id(
      std::int16_t character_id) const;
  [[nodiscard]] std::optional<IamSceneCharacterDefinitionRecord>
  character_definition_by_character_id(std::int16_t character_id) const;
  [[nodiscard]] std::optional<IamCameraRecord> camera_by_id(std::int16_t camera_id) const;

  [[nodiscard]] std::vector<IamSceneObjectPlacementRecord> object_placements() const;
  [[nodiscard]] std::vector<IamSceneObjectDefinitionRecord> object_definitions() const;
  [[nodiscard]] std::vector<IamSceneZoneRecord> zones() const;
  [[nodiscard]] std::vector<IamSceneScriptLinkRecord> script_links() const;

  [[nodiscard]] static std::optional<std::size_t> table_stride(std::size_t index);

 private:
  explicit IamSceneRecord(std::vector<std::byte> bytes) : m_bytes(std::move(bytes)) {}

  [[nodiscard]] std::optional<std::string> optional_record_string(std::uint32_t offset) const;

  std::vector<std::byte> m_bytes;
};

}  // namespace App::Omikron

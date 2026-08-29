#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Core/Omikron/ScxCameraEditing.hpp"

namespace App::Omikron {

/// 16-byte header of an .SCX scenario/resource package.
struct ScxHeader {
  /// Magic word; must be 0x00DEAD00.
  std::uint32_t magic{0};
  /// Container version; 5 in the inspected retail packages.
  std::uint32_t version{0};
  /// Purpose unresolved; 8 in both aventure.SCX and Grid.SCX.
  std::uint32_t header_word_2{0};
  /// Byte size of the descriptor block that immediately follows the header.
  std::uint32_t descriptor_size{0};
};

/// A self-offset-framed appended payload used by DEAD0000, DEAD0001,
/// DEAD0005 and DEAD000A. Its internal format is section-specific and is not
/// reinterpreted here.
struct ScxEmbeddedResource {
  /// Absolute file offset of [selfOffset, payloadSize].
  std::size_t header_offset{0};
  std::size_t payload_offset{0};
  std::size_t payload_size{0};
};

/// One DEAD0000 path descriptor (0x20 bytes on disk). Runtime consumes one
/// 8-byte-header 3DP payload for each record.
///
/// During retail Runtime loading the live descriptor fields at +0x18/+0x1C
/// are passed to Read3DP as output slots. On success they become the loaded
/// Runtime3DPSubpath** and subpath count respectively. The parsed structure
/// below preserves the serialized pre-load values instead of reproducing that
/// in-place pointer mutation.
struct ScxSection0Record {
  std::string name;  ///< +0x00, fixed 24-byte field.
  /// +0x18. Pointer-shaped serialized placeholder; live Runtime overwrites
  /// this slot with Runtime3DPSubpath**.
  std::uint32_t runtime_paths_placeholder{0};
  /// +0x1C. Serialized expected/count value; live Runtime overwrites this slot
  /// with the subpath count returned by Read3DP.
  std::uint32_t serialized_subpath_count{0};
  std::size_t file_offset{0};
};

/// One DEAD0001 animation descriptor (0x24 bytes on disk). Runtime consumes
/// one 8-byte-header animation payload for each record.
struct ScxAnimationRecord {
  std::string name;                               ///< +0x00, fixed 24-byte field.
  std::uint32_t runtime_resource_placeholder{0};  ///< +0x18.
  std::uint32_t serialized_field_1c{0};           ///< +0x1C, unresolved.
  std::uint32_t animation_id{0};                  ///< +0x20.
  std::size_t file_offset{0};
};

/// One entry of the DEAD0004 sprite/effect-model table (0x24 bytes on disk).
struct ScxSpriteEntry {
  std::string name;                             ///< +0x00, fixed 24-byte field.
  std::uint32_t sprite_id{0};                   ///< +0x20.
  std::uint32_t runtime_sprite_placeholder{0};  ///< +0x18.
  std::uint32_t serialized_field_1c{0};         ///< +0x1C, unresolved.
  std::size_t file_offset{0};
};

/// One entry of the DEAD0003 sound table (0x1A bytes on disk). The original
/// runtime replaces the +0x16 word with the loaded sound handle and writes
/// 0xFFFF on failure. Its serialized value is retained verbatim here.
struct ScxSoundRecord {
  std::string name;                         ///< +0x00, fixed 22-byte field.
  std::uint16_t runtime_sound_id{0xFFFFU};  ///< +0x16.
  std::uint16_t h_id{0};                    ///< +0x18, semantics unresolved.
  std::size_t file_offset{0};
};

/// One embedded RIFF/WAVE resource. Entries are parallel to ScxData::sounds.
struct ScxWaveResource {
  std::size_t header_offset{0};
  std::size_t payload_offset{0};  ///< Absolute offset of "RIFF".
  std::size_t payload_size{0};
};

/// One embedded 3DO package: a 12-byte header, an OD3X core block and its
/// auxiliary texture block. Entries are parallel to ScxData::sprites.
struct ScxModelResource {
  std::size_t header_offset{0};
  std::size_t core_offset{0};  ///< Absolute offset of "OD3X".
  std::size_t core_size{0};
  std::size_t auxiliary_offset{0};
  std::size_t auxiliary_size{0};
};

/// One DEAD0005 external-scene descriptor (0x1C bytes on disk). Runtime
/// consumes one 8-byte-header embedded 3D payload for each record.
struct ScxSceneRecord {
  std::string name;                            ///< +0x00, fixed 24-byte field.
  std::uint32_t runtime_scene_placeholder{0};  ///< +0x18.
  std::size_t file_offset{0};
};

/// Location of an opaque fixed-size descriptor record. This preserves safe
/// inventory and diagnostics without assigning unsupported semantics.
struct ScxOpaqueRecord {
  std::size_t file_offset{0};
  std::size_t serialized_size{0};
};

/// DEAD0007 stores a count in the low 27 bits and flags in the high five.
struct ScxGlobalTable {
  std::uint32_t serialized_count_and_flags{0};
  std::vector<ScxOpaqueRecord> records;  ///< 0x20 bytes each.
};

/// One raw 32-bit argument word of the script value pool.
struct ScriptValue {
  std::uint32_t raw{0};

  [[nodiscard]] std::uint32_t as_unsigned() const {
    return raw;
  }
  [[nodiscard]] std::int32_t as_signed() const {
    return std::bit_cast<std::int32_t>(raw);
  }
  [[nodiscard]] float as_float() const {
    return std::bit_cast<float>(raw);
  }

  void set_unsigned(const std::uint32_t value) {
    raw = value;
  }
  void set_signed(const std::int32_t value) {
    raw = std::bit_cast<std::uint32_t>(value);
  }
  void set_float(const float value) {
    raw = std::bit_cast<std::uint32_t>(value);
  }
};

/// One serialized script command (0x18 bytes on disk).
struct ScxScriptCommand {
  std::uint32_t opcode{0};
  std::uint32_t value_count{0};
  std::uint32_t first_value_index{0};
  /// Index into the script's linked-command array; nullopt for -1.
  std::optional<std::uint32_t> next_linked_command_index;
  std::uint32_t execution_limit{0};
  std::uint32_t initial_execution_count{0};
  std::size_t file_offset{0};
};

struct ScxRelatedScript {
  bool present{false};
  std::string name;
};

struct ScxBindingEntry {
  std::string name;
};

struct ScxBindingTable {
  std::vector<ScxBindingEntry> entries;
};

/// One immutable parsed DEAD0002 script definition.
struct ScxScript {
  std::uint32_t scenario_owner_placeholder{0};            ///< +0x00, overwritten at load.
  std::string name;                                       ///< +0x04, fixed 22-byte field.
  std::uint16_t script_id{0};                             ///< +0x1A.
  std::uint16_t runtime_state{0};                         ///< +0x1C, reset at load.
  std::uint16_t flags{0};                                 ///< +0x1E.
  std::uint32_t root_command_count{0};                    ///< +0x20.
  std::uint32_t current_root_command_index{0};            ///< +0x24.
  std::uint32_t root_commands_placeholder{0};             ///< +0x28.
  std::uint32_t linked_command_count{0};                  ///< +0x2C.
  std::uint32_t linked_commands_placeholder{0};           ///< +0x30.
  std::int32_t repeat_limit{0};                           ///< +0x34, whole-script repeat limit.
  std::uint32_t initial_repeat_index{0};                  ///< +0x38, serialized repeat-index seed.
  std::array<std::uint32_t, 3> binding_table_a_fields{};  ///< +0x3C..0x44.
  std::array<std::uint32_t, 3> binding_table_b_fields{};  ///< +0x48..0x50.
  std::uint32_t related_script_placeholder{0};            ///< +0x54.
  std::uint32_t runtime_field_58{0};                      ///< +0x58.
  std::array<std::uint8_t, 8> tail_fields{};              ///< +0x5C..0x63.
  std::size_t file_offset{0};

  ScxRelatedScript related_script;
  std::vector<ScxScriptCommand> root_commands;
  std::vector<ScxScriptCommand> linked_commands;
  ScxBindingTable binding_table_a;
  ScxBindingTable binding_table_b;
};

/// Parsed, non-borrowing index of an SCX v5 package. Descriptor records and
/// appended resources remain separate parallel arrays because Runtime reads
/// all descriptors first and then consumes the resource stream in descriptor
/// section order.
struct ScxData {
  ScxHeader header;
  std::size_t resource_stream_offset{0};

  /// Descriptor byte ranges skipped by Runtime's word-wise chunk dispatcher.
  /// Their internal semantics are not yet recovered; offsets and sizes are
  /// retained rather than scanning them for guessed resources.
  std::vector<ScxOpaqueRecord> descriptor_gaps;

  /// DEAD0000: path descriptors, parallel to section0_resources.
  std::vector<ScxSection0Record> section0_records;
  std::vector<ScxEmbeddedResource> section0_resources;

  /// DEAD0001: animations, parallel to animation_resources.
  std::vector<ScxAnimationRecord> animations;
  std::vector<ScxEmbeddedResource> animation_resources;

  /// DEAD0002.
  std::vector<ScxScript> scripts;
  std::vector<ScriptValue> shared_values;

  /// DEAD0003: sound descriptors and parallel embedded WAVs.
  std::vector<ScxSoundRecord> sounds;
  std::vector<ScxWaveResource> waves;

  /// DEAD0004: sprite/effect descriptors and parallel embedded 3DO packages.
  std::vector<ScxSpriteEntry> sprites;
  std::vector<ScxModelResource> models;

  /// DEAD0005: external scenes and parallel embedded 3D payloads.
  std::vector<ScxSceneRecord> scenes;
  std::vector<ScxEmbeddedResource> scene_resources;

  /// DEAD0006: 0x318-byte records with semantics still unresolved.
  std::vector<ScxOpaqueRecord> section6_records;
  /// DEAD0007 global table.
  ScxGlobalTable global_table;
  /// DEAD0008 toggles a Runtime global mode/limit; no descriptor payload.
  bool has_global_mode_chunk{false};
  /// DEAD000A owns one appended 8-byte-header extra block when present.
  std::optional<ScxEmbeddedResource> extra_block;
  /// DEAD000A parsed camera-editing timeline when present.
  std::optional<ScxCameraEditingData> camera_editing;
};

/// Parser for SCX v5 scenario/resource packages.
class SCX {
 public:
  static constexpr std::uint32_t k_magic{0x00DEAD00U};
  static constexpr std::uint32_t k_supported_version{5U};

  /// Parses descriptor chunks sequentially and uses them as the authoritative
  /// manifest for the appended resource stream. Every read is bounded and
  /// the returned index stores offsets rather than borrowing input spans.
  [[nodiscard]] static std::expected<ScxData, std::string> load(std::span<const std::byte> data);
};

}  // namespace App::Omikron

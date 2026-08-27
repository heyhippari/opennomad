#include "Core/Omikron/SCX.hpp"

#include <fmt/format.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/BinaryReader.hpp"
#include "Core/Omikron/ScxCameraEditing.hpp"

namespace App::Omikron {

namespace {

constexpr std::size_t K_HEADER_SIZE{16};
constexpr std::size_t K_NAME_24_SIZE{24};
constexpr std::size_t K_SCRIPT_NAME_SIZE{22};
constexpr std::size_t K_SOUND_NAME_SIZE{22};
constexpr std::size_t K_RELATED_SCRIPT_NAME_SIZE{21};
constexpr std::size_t K_BINDING_NAME_SIZE{21};
constexpr std::size_t K_EMBEDDED_HEADER_SIZE{8};
constexpr std::size_t K_MODEL_HEADER_SIZE{12};
constexpr std::size_t K_MIN_OD3X_CORE_SIZE{8};

constexpr std::size_t K_SECTION0_RECORD_SIZE{0x20};
constexpr std::size_t K_ANIMATION_RECORD_SIZE{0x24};
constexpr std::size_t K_SOUND_RECORD_SIZE{0x1A};
constexpr std::size_t K_SPRITE_RECORD_SIZE{0x24};
constexpr std::size_t K_SCENE_RECORD_SIZE{0x1C};
constexpr std::size_t K_SECTION6_RECORD_SIZE{0x318};
constexpr std::size_t K_GLOBAL_RECORD_SIZE{0x20};

constexpr std::uint32_t K_CHUNK_SECTION0{0xDEAD0000U};
constexpr std::uint32_t K_CHUNK_ANIMATIONS{0xDEAD0001U};
constexpr std::uint32_t K_CHUNK_SCRIPTS{0xDEAD0002U};
constexpr std::uint32_t K_CHUNK_SOUNDS{0xDEAD0003U};
constexpr std::uint32_t K_CHUNK_SPRITES{0xDEAD0004U};
constexpr std::uint32_t K_CHUNK_SCENES{0xDEAD0005U};
constexpr std::uint32_t K_CHUNK_SECTION6{0xDEAD0006U};
constexpr std::uint32_t K_CHUNK_GLOBALS{0xDEAD0007U};
constexpr std::uint32_t K_CHUNK_GLOBAL_MODE{0xDEAD0008U};
constexpr std::uint32_t K_CHUNK_UNHANDLED9{0xDEAD0009U};
constexpr std::uint32_t K_CHUNK_EXTRA{0xDEAD000AU};
constexpr std::uint32_t K_CHUNK_END{0xDEADFFFFU};

constexpr std::uint32_t K_RIFF_MAGIC{0x46464952U};  // RIFF
constexpr std::uint32_t K_WAVE_MAGIC{0x45564157U};  // WAVE
constexpr std::uint32_t K_OD3X_MAGIC{0x5833444FU};  // OD3X
constexpr std::uint32_t K_OD3X_VERSION{4U};

constexpr std::uint32_t K_MAX_DESCRIPTOR_RECORD_COUNT{65536};
constexpr std::uint32_t K_MAX_SCRIPT_COUNT{65536};
constexpr std::uint32_t K_MAX_VALUE_COUNT{1U << 20};
constexpr std::uint32_t K_MAX_COMMAND_COUNT{1U << 20};
constexpr std::uint32_t K_MAX_BINDING_ENTRIES{65536};
constexpr std::uint32_t K_GLOBAL_COUNT_MASK{0x07FFFFFFU};

enum class PendingResourceKind : std::uint8_t {
  Section0,
  Animation,
  Sound,
  SpriteModel,
  Scene,
  Extra,
};

struct PendingResource {
  PendingResourceKind kind;
  std::size_t descriptor_index{0};
};

template <typename Resource>
struct ParsedResource {
  Resource resource;
  std::size_t next_offset{0};
};

std::uint32_t read_u32_at(const std::span<const std::byte> data, const std::size_t offset) {
  const std::span<const std::byte> bytes{data.subspan(offset, 4U)};
  std::uint32_t value{0};
  std::memcpy(&value, bytes.data(), 4U);
  return value;
}

std::string fixed_string(const std::span<const std::byte> bytes) {
  const void* raw{bytes.data()};
  const char* begin{static_cast<const char*>(raw)};
  const void* nul{std::memchr(raw, '\0', bytes.size())};
  const std::size_t length{
      nul == nullptr ? bytes.size()
                     : static_cast<std::size_t>(static_cast<const char*>(nul) - begin)};
  return std::string{begin, length};
}

std::expected<void, std::string> require_record_bytes(BinaryReader& reader,
    const std::uint32_t count,
    const std::size_t record_size,
    const std::string_view label) {
  if (count > K_MAX_DESCRIPTOR_RECORD_COUNT) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("{}: implausible count {}", label, count)};
  }
  const std::uint64_t byte_count{static_cast<std::uint64_t>(count) * record_size};
  if (byte_count > reader.remaining()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("{}: {} records require {} bytes, only {} remain in the descriptor",
            label,
            count,
            byte_count,
            reader.remaining())};
  }
  return {};
}

std::expected<std::vector<ScxScriptCommand>, std::string> read_command_array(
    BinaryReader& reader, const std::uint32_t count) {
  if (count > K_MAX_COMMAND_COUNT) {
    return std::expected<std::vector<ScxScriptCommand>, std::string>{std::unexpect,
        fmt::format("script commands: implausible count {}", count)};
  }
  const std::uint64_t byte_count{static_cast<std::uint64_t>(count) * 0x18U};
  if (byte_count > reader.remaining()) {
    return std::expected<std::vector<ScxScriptCommand>, std::string>{std::unexpect,
        fmt::format("script commands: {} records require {} bytes, only {} remain",
            count,
            byte_count,
            reader.remaining())};
  }

  std::vector<ScxScriptCommand> commands;
  commands.reserve(count);
  for (std::uint32_t index{0}; index < count; ++index) {
    const std::size_t record_offset{K_HEADER_SIZE + reader.tell()};
    const std::uint32_t opcode{reader.read_u32()};
    const std::uint32_t value_count{reader.read_u32()};
    const std::uint32_t first_value_index{reader.read_u32()};
    const std::int32_t next_command_index{reader.read_i32()};
    const std::uint32_t execution_limit{reader.read_u32()};
    const std::uint32_t execution_count{reader.read_u32()};
    if (reader.has_error()) {
      return std::expected<std::vector<ScxScriptCommand>, std::string>{std::unexpect,
          fmt::format("script command {}: {}", index, reader.error())};
    }

    std::optional<std::uint32_t> next;
    if (next_command_index == -1) {
      next = std::nullopt;
    } else if (next_command_index < 0) {
      return std::expected<std::vector<ScxScriptCommand>, std::string>{std::unexpect,
          fmt::format("script command {}: negative next-command index {}",
              index,
              next_command_index)};
    } else {
      next = static_cast<std::uint32_t>(next_command_index);
    }

    commands.push_back(ScxScriptCommand{.opcode = opcode,
        .value_count = value_count,
        .first_value_index = first_value_index,
        .next_linked_command_index = next,
        .execution_limit = execution_limit,
        .initial_execution_count = execution_count,
        .file_offset = record_offset});
  }
  return commands;
}

std::expected<ScxBindingTable, std::string> read_binding_table(BinaryReader& reader) {
  const std::uint32_t count{reader.read_u32()};
  if (reader.has_error()) {
    return std::expected<ScxBindingTable, std::string>{
        std::unexpect, fmt::format("binding table: {}", reader.error())};
  }
  if (count > K_MAX_BINDING_ENTRIES) {
    return std::expected<ScxBindingTable, std::string>{std::unexpect,
        fmt::format("binding table: implausible count {}", count)};
  }
  const std::uint64_t slot_bytes{static_cast<std::uint64_t>(count) * 8U};
  const std::uint64_t name_bytes{static_cast<std::uint64_t>(count) * K_BINDING_NAME_SIZE};
  if ((slot_bytes + name_bytes) > reader.remaining()) {
    return std::expected<ScxBindingTable, std::string>{std::unexpect,
        fmt::format("binding table: {} entries require {} bytes, only {} remain",
            count,
            slot_bytes + name_bytes,
            reader.remaining())};
  }
  reader.skip(static_cast<std::size_t>(slot_bytes));

  ScxBindingTable table;
  table.entries.reserve(count);
  for (std::uint32_t index{0}; index < count; ++index) {
    const std::span<const std::byte> name_bytes_span{reader.read_bytes(K_BINDING_NAME_SIZE)};
    if (reader.has_error()) {
      return std::expected<ScxBindingTable, std::string>{std::unexpect,
          fmt::format("binding table entry {}: {}", index, reader.error())};
    }
    table.entries.push_back(ScxBindingEntry{.name = fixed_string(name_bytes_span)});
  }
  return table;
}

std::expected<void, std::string> validate_command(const ScxScriptCommand& command,
    const std::size_t linked_command_count,
    const std::size_t shared_value_count) {
  if (command.first_value_index > shared_value_count) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("first value index {} exceeds the shared value pool size {}",
            command.first_value_index,
            shared_value_count)};
  }
  if (command.value_count > (shared_value_count - command.first_value_index)) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("value slice [{}..{}] exceeds the shared value pool size {}",
            command.first_value_index,
            command.first_value_index + command.value_count,
            shared_value_count)};
  }
  if (command.next_linked_command_index.has_value() &&
      command.next_linked_command_index.value() >= linked_command_count) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("next-command index {} is out of range for {} linked commands",
            command.next_linked_command_index.value(),
            linked_command_count)};
  }
  return {};
}

std::expected<void, std::string> parse_dead0002(BinaryReader& reader,
    std::vector<ScxScript>& scripts,
    std::vector<ScriptValue>& shared_values) {
  const std::uint32_t script_count{reader.read_u32()};
  if (reader.has_error()) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("scripts: {}", reader.error())};
  }
  if (script_count > K_MAX_SCRIPT_COUNT) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("scripts: implausible count {}", script_count)};
  }
  if (auto valid{require_record_bytes(reader, script_count, 0x64U, "scripts")}; !valid) {
    return valid;
  }

  scripts.reserve(script_count);
  for (std::uint32_t index{0}; index < script_count; ++index) {
    ScxScript script;
    script.file_offset = K_HEADER_SIZE + reader.tell();
    script.scenario_owner_placeholder = reader.read_u32();
    script.name = fixed_string(reader.read_bytes(K_SCRIPT_NAME_SIZE));
    script.script_id = reader.read_u16();
    script.runtime_state = reader.read_u16();
    script.flags = reader.read_u16();
    script.root_command_count = reader.read_u32();
    script.current_root_command_index = reader.read_u32();
    script.root_commands_placeholder = reader.read_u32();
    script.linked_command_count = reader.read_u32();
    script.linked_commands_placeholder = reader.read_u32();
    script.repeat_limit = reader.read_i32();
    script.initial_repeat_index = reader.read_u32();
    for (std::uint32_t& field : script.binding_table_a_fields) {
      field = reader.read_u32();
    }
    for (std::uint32_t& field : script.binding_table_b_fields) {
      field = reader.read_u32();
    }
    script.related_script_placeholder = reader.read_u32();
    script.runtime_field_58 = reader.read_u32();
    const std::span<const std::byte> tail{reader.read_bytes(script.tail_fields.size())};
    if (reader.has_error()) {
      return std::expected<void, std::string>{
          std::unexpect, fmt::format("script {}: {}", index, reader.error())};
    }
    for (std::size_t slot{0}; slot < script.tail_fields.size(); ++slot) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      script.tail_fields.at(slot) = std::to_integer<std::uint8_t>(tail[slot]);
    }
    scripts.push_back(std::move(script));
  }

  const std::uint32_t shared_value_count{reader.read_u32()};
  if (reader.has_error()) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("shared values: {}", reader.error())};
  }
  if (shared_value_count > K_MAX_VALUE_COUNT) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("shared values: implausible count {}", shared_value_count)};
  }
  const std::uint64_t value_bytes{static_cast<std::uint64_t>(shared_value_count) * 4U};
  if (value_bytes > reader.remaining()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("shared values: {} entries require {} bytes, only {} remain",
            shared_value_count,
            value_bytes,
            reader.remaining())};
  }
  shared_values.reserve(shared_value_count);
  for (std::uint32_t index{0}; index < shared_value_count; ++index) {
    shared_values.push_back(ScriptValue{.raw = reader.read_u32()});
  }

  for (std::size_t index{0}; index < scripts.size(); ++index) {
    ScxScript& script{scripts.at(index)};
    const std::uint8_t related_present{reader.read_u8()};
    if (reader.has_error()) {
      return std::expected<void, std::string>{
          std::unexpect, fmt::format("script {}: {}", index, reader.error())};
    }
    if (related_present != 0U) {
      script.related_script.present = true;
      script.related_script.name = fixed_string(reader.read_bytes(K_RELATED_SCRIPT_NAME_SIZE));
      if (reader.has_error()) {
        return std::expected<void, std::string>{
            std::unexpect, fmt::format("script {} related block: {}", index, reader.error())};
      }
    }

    auto root_commands{read_command_array(reader, script.root_command_count)};
    if (!root_commands) {
      return std::expected<void, std::string>{
          std::unexpect, fmt::format("script {}: {}", index, root_commands.error())};
    }
    script.root_commands = std::move(root_commands).value();

    auto linked_commands{read_command_array(reader, script.linked_command_count)};
    if (!linked_commands) {
      return std::expected<void, std::string>{
          std::unexpect, fmt::format("script {}: {}", index, linked_commands.error())};
    }
    script.linked_commands = std::move(linked_commands).value();

    auto table_a{read_binding_table(reader)};
    if (!table_a) {
      return std::expected<void, std::string>{
          std::unexpect, fmt::format("script {}: {}", index, table_a.error())};
    }
    script.binding_table_a = std::move(table_a).value();

    auto table_b{read_binding_table(reader)};
    if (!table_b) {
      return std::expected<void, std::string>{
          std::unexpect, fmt::format("script {}: {}", index, table_b.error())};
    }
    script.binding_table_b = std::move(table_b).value();

    for (const ScxScriptCommand& command : script.root_commands) {
      if (auto valid{
              validate_command(command, script.linked_commands.size(), shared_values.size())};
          !valid) {
        return std::expected<void, std::string>{
            std::unexpect, fmt::format("script {}: {}", index, valid.error())};
      }
    }
    for (const ScxScriptCommand& command : script.linked_commands) {
      if (auto valid{
              validate_command(command, script.linked_commands.size(), shared_values.size())};
          !valid) {
        return std::expected<void, std::string>{
            std::unexpect, fmt::format("script {}: {}", index, valid.error())};
      }
    }
  }

  return {};
}

std::expected<void, std::string> parse_dead0000(
    BinaryReader& reader, std::vector<ScxSection0Record>& records) {
  const std::uint32_t count{reader.read_u32()};
  if (reader.has_error()) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("DEAD0000: {}", reader.error())};
  }
  if (auto valid{require_record_bytes(reader, count, K_SECTION0_RECORD_SIZE, "DEAD0000")};
      !valid) {
    return valid;
  }
  records.reserve(count);
  for (std::uint32_t index{0}; index < count; ++index) {
    const std::size_t file_offset{K_HEADER_SIZE + reader.tell()};
    records.push_back(ScxSection0Record{.name = fixed_string(reader.read_bytes(K_NAME_24_SIZE)),        
        .runtime_paths_placeholder = reader.read_u32(),
        .serialized_subpath_count = reader.read_u32(),
        .file_offset = file_offset});
  }
  return {};
}

std::expected<void, std::string> parse_dead0001(
    BinaryReader& reader, std::vector<ScxAnimationRecord>& records) {
  const std::uint32_t count{reader.read_u32()};
  if (reader.has_error()) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("animations: {}", reader.error())};
  }
  if (auto valid{require_record_bytes(reader, count, K_ANIMATION_RECORD_SIZE, "animations")};
      !valid) {
    return valid;
  }
  records.reserve(count);
  for (std::uint32_t index{0}; index < count; ++index) {
    const std::size_t file_offset{K_HEADER_SIZE + reader.tell()};
    records.push_back(ScxAnimationRecord{.name = fixed_string(reader.read_bytes(K_NAME_24_SIZE)),
        .runtime_resource_placeholder = reader.read_u32(),
        .serialized_field_1c = reader.read_u32(),
        .animation_id = reader.read_u32(),
        .file_offset = file_offset});
  }
  return {};
}

std::expected<void, std::string> parse_dead0003(
    BinaryReader& reader, std::vector<ScxSoundRecord>& records) {
  const std::uint32_t count{reader.read_u32()};
  if (reader.has_error()) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("sounds: {}", reader.error())};
  }
  if (auto valid{require_record_bytes(reader, count, K_SOUND_RECORD_SIZE, "sounds")}; !valid) {
    return valid;
  }
  records.reserve(count);
  for (std::uint32_t index{0}; index < count; ++index) {
    const std::size_t file_offset{K_HEADER_SIZE + reader.tell()};
    records.push_back(ScxSoundRecord{.name = fixed_string(reader.read_bytes(K_SOUND_NAME_SIZE)),
        .runtime_sound_id = reader.read_u16(),
        .h_id = reader.read_u16(),
        .file_offset = file_offset});
  }
  return {};
}

std::expected<void, std::string> parse_dead0004(
    BinaryReader& reader, std::vector<ScxSpriteEntry>& records) {
  const std::uint32_t count{reader.read_u32()};
  if (reader.has_error()) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("sprites: {}", reader.error())};
  }
  if (auto valid{require_record_bytes(reader, count, K_SPRITE_RECORD_SIZE, "sprites")}; !valid) {
    return valid;
  }
  records.reserve(count);
  for (std::uint32_t index{0}; index < count; ++index) {
    const std::size_t file_offset{K_HEADER_SIZE + reader.tell()};
    const std::string name{fixed_string(reader.read_bytes(K_NAME_24_SIZE))};
    const std::uint32_t runtime_placeholder{reader.read_u32()};
    const std::uint32_t field_1c{reader.read_u32()};
    const std::uint32_t sprite_id{reader.read_u32()};
    records.push_back(ScxSpriteEntry{.name = name,
        .sprite_id = sprite_id,
        .runtime_sprite_placeholder = runtime_placeholder,
        .serialized_field_1c = field_1c,
        .file_offset = file_offset});
  }
  return {};
}

std::expected<void, std::string> parse_dead0005(
    BinaryReader& reader, std::vector<ScxSceneRecord>& records) {
  const std::uint32_t count{reader.read_u32()};
  if (reader.has_error()) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("scenes: {}", reader.error())};
  }
  if (auto valid{require_record_bytes(reader, count, K_SCENE_RECORD_SIZE, "scenes")}; !valid) {
    return valid;
  }
  records.reserve(count);
  for (std::uint32_t index{0}; index < count; ++index) {
    const std::size_t file_offset{K_HEADER_SIZE + reader.tell()};
    records.push_back(ScxSceneRecord{.name = fixed_string(reader.read_bytes(K_NAME_24_SIZE)),
        .runtime_scene_placeholder = reader.read_u32(),
        .file_offset = file_offset});
  }
  return {};
}

std::expected<void, std::string> parse_opaque_records(BinaryReader& reader,
    std::vector<ScxOpaqueRecord>& records,
    const std::size_t record_size,
    const std::string_view label,
    const std::uint32_t count) {
  if (auto valid{require_record_bytes(reader, count, record_size, label)}; !valid) {
    return valid;
  }
  records.reserve(count);
  for (std::uint32_t index{0}; index < count; ++index) {
    records.push_back(ScxOpaqueRecord{
        .file_offset = K_HEADER_SIZE + reader.tell(), .serialized_size = record_size});
    reader.skip(record_size);
  }
  return {};
}

std::expected<ParsedResource<ScxEmbeddedResource>, std::string> parse_embedded_resource(
    const std::span<const std::byte> data,
    const std::size_t position,
    const std::string_view label) {
  if (position > data.size() || (data.size() - position) < K_EMBEDDED_HEADER_SIZE) {
    return std::expected<ParsedResource<ScxEmbeddedResource>, std::string>{std::unexpect,
        fmt::format("{} at {:#x}: truncated 8-byte resource header", label, position)};
  }
  const std::uint32_t self_offset{read_u32_at(data, position)};
  if (self_offset != position) {
    return std::expected<ParsedResource<ScxEmbeddedResource>, std::string>{std::unexpect,
        fmt::format("{} at {:#x}: self-offset is {:#x}", label, position, self_offset)};
  }
  const std::uint32_t payload_size{read_u32_at(data, position + 4U)};
  const std::uint64_t payload_end{
      static_cast<std::uint64_t>(position) + K_EMBEDDED_HEADER_SIZE + payload_size};
  if (payload_end > data.size()) {
    return std::expected<ParsedResource<ScxEmbeddedResource>, std::string>{std::unexpect,
        fmt::format("{} at {:#x}: {}-byte payload exceeds the {} bytes remaining",
            label,
            position,
            payload_size,
            data.size() - position)};
  }
  return ParsedResource<ScxEmbeddedResource>{
      .resource = ScxEmbeddedResource{.header_offset = position,
          .payload_offset = position + K_EMBEDDED_HEADER_SIZE,
          .payload_size = payload_size},
      .next_offset = static_cast<std::size_t>(payload_end)};
}

std::expected<ParsedResource<ScxWaveResource>, std::string> parse_wave_resource(
    const std::span<const std::byte> data,
    const std::size_t position,
    const std::string_view label) {
  auto embedded{parse_embedded_resource(data, position, label)};
  if (!embedded) {
    return std::expected<ParsedResource<ScxWaveResource>, std::string>{
        std::unexpect, std::move(embedded).error()};
  }
  const ScxEmbeddedResource& raw{embedded->resource};
  if (raw.payload_size < 12U || read_u32_at(data, raw.payload_offset) != K_RIFF_MAGIC ||
      read_u32_at(data, raw.payload_offset + 8U) != K_WAVE_MAGIC) {
    return std::expected<ParsedResource<ScxWaveResource>, std::string>{std::unexpect,
        fmt::format("{} at {:#x}: payload is not RIFF/WAVE", label, position)};
  }
  return ParsedResource<ScxWaveResource>{
      .resource = ScxWaveResource{.header_offset = raw.header_offset,
          .payload_offset = raw.payload_offset,
          .payload_size = raw.payload_size},
      .next_offset = embedded->next_offset};
}

std::expected<ParsedResource<ScxModelResource>, std::string> parse_model_resource(
    const std::span<const std::byte> data,
    const std::size_t position,
    const std::string_view label) {
  if (position > data.size() || (data.size() - position) < K_MODEL_HEADER_SIZE) {
    return std::expected<ParsedResource<ScxModelResource>, std::string>{std::unexpect,
        fmt::format("{} at {:#x}: truncated 12-byte model header", label, position)};
  }
  const std::uint32_t self_offset{read_u32_at(data, position)};
  if (self_offset != position) {
    return std::expected<ParsedResource<ScxModelResource>, std::string>{std::unexpect,
        fmt::format("{} at {:#x}: self-offset is {:#x}", label, position, self_offset)};
  }
  const std::uint32_t core_size{read_u32_at(data, position + 4U)};
  const std::uint32_t auxiliary_size{read_u32_at(data, position + 8U)};
  const std::uint64_t core_end{
      static_cast<std::uint64_t>(position) + K_MODEL_HEADER_SIZE + core_size};
  const std::uint64_t package_end{core_end + auxiliary_size};
  if (core_end > data.size() || package_end > data.size()) {
    return std::expected<ParsedResource<ScxModelResource>, std::string>{std::unexpect,
        fmt::format("{} at {:#x}: core ({}) plus auxiliary ({}) exceeds the {} bytes remaining",
            label,
            position,
            core_size,
            auxiliary_size,
            data.size() - position)};
  }
  if (core_size < K_MIN_OD3X_CORE_SIZE) {
    return std::expected<ParsedResource<ScxModelResource>, std::string>{std::unexpect,
        fmt::format("{} at {:#x}: {}-byte core is too small for OD3X", label, position, core_size)};
  }
  const std::size_t core_offset{position + K_MODEL_HEADER_SIZE};
  if (read_u32_at(data, core_offset) != K_OD3X_MAGIC) {
    return std::expected<ParsedResource<ScxModelResource>, std::string>{std::unexpect,
        fmt::format("{} at {:#x}: core does not begin with OD3X", label, position)};
  }
  const std::uint32_t core_version{read_u32_at(data, core_offset + 4U)};
  if (core_version != K_OD3X_VERSION) {
    return std::expected<ParsedResource<ScxModelResource>, std::string>{std::unexpect,
        fmt::format("{} at {:#x}: OD3X version {} is unsupported (expected {})",
            label,
            position,
            core_version,
            K_OD3X_VERSION)};
  }
  return ParsedResource<ScxModelResource>{
      .resource = ScxModelResource{.header_offset = position,
          .core_offset = core_offset,
          .core_size = core_size,
          .auxiliary_offset = static_cast<std::size_t>(core_end),
          .auxiliary_size = auxiliary_size},
      .next_offset = static_cast<std::size_t>(package_end)};
}

void append_pending(std::vector<PendingResource>& pending,
    const PendingResourceKind kind,
    const std::size_t count) {
  for (std::size_t index{0}; index < count; ++index) {
    pending.push_back(PendingResource{.kind = kind, .descriptor_index = index});
  }
}

void append_descriptor_gap(
    std::vector<ScxOpaqueRecord>& gaps, const std::size_t file_offset, const std::size_t size) {
  if (!gaps.empty()) {
    ScxOpaqueRecord& previous{gaps.back()};
    if (previous.file_offset + previous.serialized_size == file_offset) {
      previous.serialized_size += size;
      return;
    }
  }
  gaps.push_back(ScxOpaqueRecord{.file_offset = file_offset, .serialized_size = size});
}

std::string pending_label(const PendingResource& pending) {
  switch (pending.kind) {
    case PendingResourceKind::Section0:
      return fmt::format("DEAD0000 resource {}", pending.descriptor_index);
    case PendingResourceKind::Animation:
      return fmt::format("animation {}", pending.descriptor_index);
    case PendingResourceKind::Sound:
      return fmt::format("sound {}", pending.descriptor_index);
    case PendingResourceKind::SpriteModel:
      return fmt::format("sprite/model {}", pending.descriptor_index);
    case PendingResourceKind::Scene:
      return fmt::format("scene {}", pending.descriptor_index);
    case PendingResourceKind::Extra:
      return "DEAD000A extra block";
  }
  return "SCX resource";
}

}  // namespace

std::expected<ScxData, std::string> SCX::load(const std::span<const std::byte> data) {
  APP_PROFILE_FUNCTION();

  if (data.size() < K_HEADER_SIZE) {
    return std::expected<ScxData, std::string>{std::unexpect,
        fmt::format("file too small: {} bytes, expected at least {}", data.size(), K_HEADER_SIZE)};
  }

  ScxData scx;
  BinaryReader header_reader{data.first(K_HEADER_SIZE)};
  scx.header.magic = header_reader.read_u32();
  scx.header.version = header_reader.read_u32();
  scx.header.header_word_2 = header_reader.read_u32();
  scx.header.descriptor_size = header_reader.read_u32();
  if (header_reader.has_error()) {
    return std::expected<ScxData, std::string>{std::unexpect, header_reader.error()};
  }
  if (scx.header.magic != k_magic) {
    return std::expected<ScxData, std::string>{std::unexpect,
        fmt::format("invalid magic {:#010x}, expected {:#010x}", scx.header.magic, k_magic)};
  }
  if (scx.header.version != k_supported_version) {
    return std::expected<ScxData, std::string>{std::unexpect,
        fmt::format("unsupported version {}, expected {}",
            scx.header.version,
            k_supported_version)};
  }

  const std::size_t descriptor_size{scx.header.descriptor_size};
  if (descriptor_size > (data.size() - K_HEADER_SIZE)) {
    return std::expected<ScxData, std::string>{std::unexpect,
        fmt::format("descriptor size {:#x} exceeds the {} bytes after the header",
            descriptor_size,
            data.size() - K_HEADER_SIZE)};
  }

  const std::span<const std::byte> descriptor{data.subspan(K_HEADER_SIZE, descriptor_size)};
  BinaryReader reader{descriptor};
  std::vector<PendingResource> pending_resources;
  std::array<bool, 11> seen_chunks{};

  while (true) {
    if (reader.remaining() < 4U) {
      return std::expected<ScxData, std::string>{
          std::unexpect, "descriptor ended before DEADFFFF"};
    }
    const std::size_t tag_offset{reader.tell()};
    const std::uint32_t tag{reader.read_u32()};
    if (tag == K_CHUNK_END) {
      if (reader.remaining() != 0U) {
        return std::expected<ScxData, std::string>{std::unexpect,
            fmt::format("descriptor has {} trailing bytes after DEADFFFF", reader.remaining())};
      }
      break;
    }

    // Runtime has no default failure case here: an unmatched word advances
    // the descriptor cursor by four bytes. Preserve coalesced skipped ranges
    // so they remain visible without assigning speculative structure.
    if (tag < K_CHUNK_SECTION0 || tag > K_CHUNK_EXTRA || tag == K_CHUNK_UNHANDLED9) {
      append_descriptor_gap(scx.descriptor_gaps, K_HEADER_SIZE + tag_offset, 4U);
      continue;
    }
    const std::size_t chunk_index{static_cast<std::size_t>(tag - K_CHUNK_SECTION0)};
    if (seen_chunks.at(chunk_index)) {
      return std::expected<ScxData, std::string>{std::unexpect,
          fmt::format("duplicate descriptor tag {:#010x} at file offset {:#x}",
              tag,
              K_HEADER_SIZE + tag_offset)};
    }
    seen_chunks.at(chunk_index) = true;

    std::expected<void, std::string> parsed{};
    switch (tag) {
      case K_CHUNK_SECTION0:
        parsed = parse_dead0000(reader, scx.section0_records);
        if (parsed) {
          append_pending(
              pending_resources, PendingResourceKind::Section0, scx.section0_records.size());
        }
        break;
      case K_CHUNK_ANIMATIONS:
        parsed = parse_dead0001(reader, scx.animations);
        if (parsed) {
          append_pending(
              pending_resources, PendingResourceKind::Animation, scx.animations.size());
        }
        break;
      case K_CHUNK_SCRIPTS:
        parsed = parse_dead0002(reader, scx.scripts, scx.shared_values);
        break;
      case K_CHUNK_SOUNDS:
        parsed = parse_dead0003(reader, scx.sounds);
        if (parsed) {
          append_pending(pending_resources, PendingResourceKind::Sound, scx.sounds.size());
        }
        break;
      case K_CHUNK_SPRITES:
        parsed = parse_dead0004(reader, scx.sprites);
        if (parsed) {
          append_pending(
              pending_resources, PendingResourceKind::SpriteModel, scx.sprites.size());
        }
        break;
      case K_CHUNK_SCENES:
        parsed = parse_dead0005(reader, scx.scenes);
        if (parsed) {
          append_pending(pending_resources, PendingResourceKind::Scene, scx.scenes.size());
        }
        break;
      case K_CHUNK_SECTION6: {
        const std::uint32_t count{reader.read_u32()};
        if (reader.has_error()) {
          parsed = std::expected<void, std::string>{
              std::unexpect, fmt::format("DEAD0006: {}", reader.error())};
        } else {
          parsed = parse_opaque_records(
              reader, scx.section6_records, K_SECTION6_RECORD_SIZE, "DEAD0006", count);
        }
        break;
      }
      case K_CHUNK_GLOBALS: {
        scx.global_table.serialized_count_and_flags = reader.read_u32();
        if (reader.has_error()) {
          parsed = std::expected<void, std::string>{
              std::unexpect, fmt::format("DEAD0007: {}", reader.error())};
        } else {
          const std::uint32_t count{
              scx.global_table.serialized_count_and_flags & K_GLOBAL_COUNT_MASK};
          parsed = parse_opaque_records(
              reader, scx.global_table.records, K_GLOBAL_RECORD_SIZE, "DEAD0007", count);
        }
        break;
      }
      case K_CHUNK_GLOBAL_MODE:
        scx.has_global_mode_chunk = true;
        break;
      case K_CHUNK_EXTRA:
        pending_resources.push_back(
            PendingResource{.kind = PendingResourceKind::Extra, .descriptor_index = 0});
        break;
      default:
        return std::expected<ScxData, std::string>{
            std::unexpect, "internal SCX descriptor dispatch error"};
    }
    if (!parsed) {
      return std::expected<ScxData, std::string>{std::unexpect, std::move(parsed).error()};
    }
  }

  scx.resource_stream_offset = K_HEADER_SIZE + descriptor_size;
  std::size_t position{scx.resource_stream_offset};
  for (const PendingResource& pending : pending_resources) {
    const std::string label{pending_label(pending)};
    switch (pending.kind) {
      case PendingResourceKind::Section0: {
        auto parsed{parse_embedded_resource(data, position, label)};
        if (!parsed) {
          return std::expected<ScxData, std::string>{
              std::unexpect, std::move(parsed).error()};
        }
        scx.section0_resources.push_back(parsed->resource);
        position = parsed->next_offset;
        break;
      }
      case PendingResourceKind::Animation: {
        auto parsed{parse_embedded_resource(data, position, label)};
        if (!parsed) {
          return std::expected<ScxData, std::string>{
              std::unexpect, std::move(parsed).error()};
        }
        scx.animation_resources.push_back(parsed->resource);
        position = parsed->next_offset;
        break;
      }
      case PendingResourceKind::Sound: {
        auto parsed{parse_wave_resource(data, position, label)};
        if (!parsed) {
          return std::expected<ScxData, std::string>{
              std::unexpect, std::move(parsed).error()};
        }
        scx.waves.push_back(parsed->resource);
        position = parsed->next_offset;
        break;
      }
      case PendingResourceKind::SpriteModel: {
        auto parsed{parse_model_resource(data, position, label)};
        if (!parsed) {
          return std::expected<ScxData, std::string>{
              std::unexpect, std::move(parsed).error()};
        }
        scx.models.push_back(parsed->resource);
        position = parsed->next_offset;
        break;
      }
      case PendingResourceKind::Scene: {
        auto parsed{parse_embedded_resource(data, position, label)};
        if (!parsed) {
          return std::expected<ScxData, std::string>{
              std::unexpect, std::move(parsed).error()};
        }
        scx.scene_resources.push_back(parsed->resource);
        position = parsed->next_offset;
        break;
      }
      case PendingResourceKind::Extra: {
        auto parsed{parse_embedded_resource(data, position, label)};
        if (!parsed) {
          return std::expected<ScxData, std::string>{
              std::unexpect, std::move(parsed).error()};
        }
        scx.extra_block = parsed->resource;

        // Parse DEAD000A camera-editing timeline if present
        if (scx.extra_block.has_value()) {
          const auto payload{data.subspan(
              scx.extra_block->payload_offset,
              scx.extra_block->payload_size)};
          auto camera_editing{ScxCameraEditing::load(payload)};
          if (!camera_editing) {
            return std::expected<ScxData, std::string>{
                std::unexpect, std::move(camera_editing).error()};
          }
          scx.camera_editing = std::move(camera_editing).value();
        }

        position = parsed->next_offset;
        break;
      }
    }
  }

  if (position != data.size()) {
    return std::expected<ScxData, std::string>{std::unexpect,
        fmt::format("resource manifest consumed through {:#x}, but file ends at {:#x} ({} "
                    "unclaimed bytes)",
            position,
            data.size(),
            data.size() - position)};
  }

  App::Log::debug(LogCategory::SCX,
      "SCX parsed — scripts={}, values={}, sounds={}, sprites/models={}, animations={}, scenes={}",
      scx.scripts.size(),
      scx.shared_values.size(),
      scx.sounds.size(),
      scx.sprites.size(),
      scx.animations.size(),
      scx.scenes.size());
  App::Log::trace(LogCategory::SCX,
      "SCX DEAD0000={}, DEAD0006={}, DEAD0007={}, descriptor_gaps={}, extra={}",
      scx.section0_records.size(),
      scx.section6_records.size(),
      scx.global_table.records.size(),
      scx.descriptor_gaps.size(),
      scx.extra_block.has_value());

  for (std::size_t index{0}; index < scx.section0_records.size(); ++index) {
    App::Log::trace(LogCategory::SCX,
        "SCX DEAD0000 {}: '{}' (serialized subpaths {})",
        index,
        scx.section0_records.at(index).name,
        scx.section0_records.at(index).serialized_subpath_count);
  }
  for (std::size_t index{0}; index < scx.animations.size(); ++index) {
    App::Log::trace(LogCategory::SCX,
        "SCX animation {}: '{}' (id {})",
        index,
        scx.animations.at(index).name,
        scx.animations.at(index).animation_id);
  }
  for (std::size_t index{0}; index < scx.sounds.size(); ++index) {
    const ScxSoundRecord& sound{scx.sounds.at(index)};
    App::Log::trace(LogCategory::SCX,
        "SCX sound {}: '{}' (runtime id {:#06x}, hID {:#06x})",
        index,
        sound.name,
        sound.runtime_sound_id,
        sound.h_id);
  }
  for (std::size_t index{0}; index < scx.sprites.size(); ++index) {
    const ScxSpriteEntry& sprite{scx.sprites.at(index)};
    App::Log::trace(LogCategory::SCX,
        "SCX sprite/model {}: '{}' (id {})",
        index,
        sprite.name,
        sprite.sprite_id);
  }
  for (std::size_t index{0}; index < scx.scripts.size(); ++index) {
    const ScxScript& script{scx.scripts.at(index)};
    App::Log::trace(LogCategory::SCX,
        "SCX script {}: '{}' (id {}, state {:#x}, flags {:#x}, roots {}, linked {}, related '{}')",
        index,
        script.name,
        script.script_id,
        script.runtime_state,
        script.flags,
        script.root_command_count,
        script.linked_command_count,
        script.related_script.present ? script.related_script.name : std::string{});
  }

  return scx;
}

}  // namespace App::Omikron

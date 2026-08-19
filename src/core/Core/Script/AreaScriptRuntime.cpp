#include "Core/Script/AreaScriptRuntime.hpp"

#include <fmt/format.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Log.hpp"
#include "Core/Script/AreaScriptOpcode.hpp"
#include "Core/Script/ScriptOpcode.hpp"

namespace App::Script {

namespace {

constexpr std::uint32_t K_OP_SET_GLOBAL_VARIABLE_ONE{0x0D};
constexpr std::uint32_t K_OP_SET_GLOBAL_VARIABLE{0x0E};
constexpr std::uint32_t K_OP_CHARACTER_LOOKUP{0x38};
constexpr std::uint32_t K_OP_CHARACTER_SELECTION_RESET{0x4F};
constexpr std::uint32_t K_OP_ACTIVATE_SUBSYSTEM{0x68};
constexpr std::uint32_t K_OP_OBJECT_ACTIVATE{0x5C};
constexpr std::uint32_t K_OP_SUBSYSTEM_OPERATION{0x83};
constexpr std::uint32_t K_OP_PLAY_MUSIC{0x67};
constexpr std::uint32_t K_OP_PRESENTATION_EFFECT{0x76};
constexpr std::uint32_t K_OP_OPEN_INTERFACE{0x46};

/// Wait state assigned by the interface-open opcode (recovered value 6).
constexpr std::uint16_t K_OPEN_INTERFACE_WAIT_STATE{6};

constexpr std::array<AreaOperandWidth, 1> K_OPERANDS_0D{AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 2> K_OPERANDS_0E{
    AreaOperandWidth::k_int16, AreaOperandWidth::k_int8};
constexpr std::array<AreaOperandWidth, 1> K_OPERANDS_38{AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 1> K_OPERANDS_4F{AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 0> K_OPERANDS_68{};
constexpr std::array<AreaOperandWidth, 1> K_OPERANDS_5C{AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 2> K_OPERANDS_83{
    AreaOperandWidth::k_int16, AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 3> K_OPERANDS_67{
    AreaOperandWidth::k_int16, AreaOperandWidth::k_int16, AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 3> K_OPERANDS_76{
    AreaOperandWidth::k_int32, AreaOperandWidth::k_int16, AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 3> K_OPERANDS_46{
    AreaOperandWidth::k_int16, AreaOperandWidth::k_int16, AreaOperandWidth::k_int16};

constexpr std::array<AreaOpcodeInfo, 10> K_AREA_OPCODE_TABLE{
    AreaOpcodeInfo{.opcode = K_OP_SET_GLOBAL_VARIABLE_ONE,
        .name = "SetGlobalVariableOne",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "sets START/global variable <operand 0> to 1",
        .operands = K_OPERANDS_0D.data(),
        .operand_count = K_OPERANDS_0D.size()},
    AreaOpcodeInfo{.opcode = K_OP_SET_GLOBAL_VARIABLE,
        .name = "SetGlobalVariable",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "sets START/global variable <operand 0> to <operand 1>",
        .operands = K_OPERANDS_0E.data(),
        .operand_count = K_OPERANDS_0E.size()},
    AreaOpcodeInfo{.opcode = K_OP_CHARACTER_LOOKUP,
        .name = "CharacterLookup",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "character-related lookup/activation via area table 0",
        .operands = K_OPERANDS_38.data(),
        .operand_count = K_OPERANDS_38.size()},
    AreaOpcodeInfo{.opcode = K_OP_CHARACTER_SELECTION_RESET,
        .name = "CharacterSelectionReset",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "character-related selection/reset behavior",
        .operands = K_OPERANDS_4F.data(),
        .operand_count = K_OPERANDS_4F.size()},
    AreaOpcodeInfo{.opcode = K_OP_ACTIVATE_SUBSYSTEM,
        .name = "ActivateSubsystem",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "activates a subsystem",
        .operands = K_OPERANDS_68.data(),
        .operand_count = K_OPERANDS_68.size()},
    AreaOpcodeInfo{.opcode = K_OP_OBJECT_ACTIVATE,
        .name = "ObjectActivate",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "object-related activation/load behavior",
        .operands = K_OPERANDS_5C.data(),
        .operand_count = K_OPERANDS_5C.size()},
    AreaOpcodeInfo{.opcode = K_OP_SUBSYSTEM_OPERATION,
        .name = "SubsystemOperation",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "unresolved subsystem operation",
        .operands = K_OPERANDS_83.data(),
        .operand_count = K_OPERANDS_83.size()},
    AreaOpcodeInfo{.opcode = K_OP_PLAY_MUSIC,
        .name = "PlayMusic",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "plays TRACKS/<operand 0>.ADP; operand 1 controls looping; "
                 "operand 2 is preserved with unresolved semantics",
        .operands = K_OPERANDS_67.data(),
        .operand_count = K_OPERANDS_67.size()},
    AreaOpcodeInfo{.opcode = K_OP_PRESENTATION_EFFECT,
        .name = "PresentationEffect",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "presentation/fade/effect behavior",
        .operands = K_OPERANDS_76.data(),
        .operand_count = K_OPERANDS_76.size()},
    AreaOpcodeInfo{.opcode = K_OP_OPEN_INTERFACE,
        .name = "OpenInterface",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "opens interface <operand 0> and waits (wait state 6)",
        .operands = K_OPERANDS_46.data(),
        .operand_count = K_OPERANDS_46.size()},
};

std::uint8_t read_u8_at(const std::span<const std::byte> data, const std::size_t offset) {
  std::uint8_t value{0};
  std::memcpy(&value, data.subspan(offset, 1U).data(), sizeof(value));
  return value;
}

std::int8_t read_i8_at(const std::span<const std::byte> data, const std::size_t offset) {
  std::int8_t value{0};
  std::memcpy(&value, data.subspan(offset, 1U).data(), sizeof(value));
  return value;
}

std::int16_t read_i16_at(const std::span<const std::byte> data, const std::size_t offset) {
  std::int16_t value{0};
  std::memcpy(&value, data.subspan(offset, 2U).data(), sizeof(value));
  return value;
}

std::int32_t read_i32_at(const std::span<const std::byte> data, const std::size_t offset) {
  std::int32_t value{0};
  std::memcpy(&value, data.subspan(offset, 4U).data(), sizeof(value));
  return value;
}

}  // namespace

const AreaOpcodeInfo* area_opcode_info(const std::uint32_t opcode) {
  for (const AreaOpcodeInfo& info : K_AREA_OPCODE_TABLE) {
    if (info.opcode == opcode) {
      return &info;
    }
  }
  return nullptr;
}

const char* area_opcode_name(const std::uint32_t opcode) {
  const AreaOpcodeInfo* info{area_opcode_info(opcode)};
  return info == nullptr ? nullptr : info->name.data();
}

AreaScriptRuntime::AreaScriptRuntime(const std::span<const std::byte> script_bytes)
    : m_script(script_bytes) {}

void AreaScriptRuntime::queue_event(const std::uint16_t event) {
  m_queued_events.push_back(event);
}

void AreaScriptRuntime::activate() {
  m_active = true;
}

void AreaScriptRuntime::set_interface_sink(InterfaceSink sink) {
  m_interface_sink = std::move(sink);
}

void AreaScriptRuntime::set_music_sink(MusicSink sink) {
  m_music_sink = std::move(sink);
}

void AreaScriptRuntime::set_instruction_sink(InstructionSink sink) {
  m_instruction_sink = std::move(sink);
}

std::expected<void, std::string> AreaScriptRuntime::complete_interface_wait(
    const App::InterfaceCompletion& completion) {
  if (m_state != AreaScriptState::k_waiting) {
    return std::expected<void, std::string>{std::unexpect, "area script is not waiting"};
  }
  if (m_wait.kind != AreaWaitKind::k_interface) {
    return std::expected<void, std::string>{
        std::unexpect, "area script is not waiting on an interface"};
  }
  if (!m_wait.interface.has_value() || m_wait.interface.value() != completion.handle) {
    return std::expected<void, std::string>{
        std::unexpect, "interface completion handle does not match the waiting interface"};
  }

  m_completion_result = completion.result;
  m_wait = AreaWaitState{};
  m_wait_state = 0;
  m_state = AreaScriptState::k_running;
  App::Log::info("area script interface wait completed; resuming at offset {:#x}", m_ip);
  return {};
}

std::optional<std::int32_t> AreaScriptRuntime::variable(const std::uint16_t id) const {
  const auto found{m_variables.find(id)};
  if (found == m_variables.end()) {
    return std::nullopt;
  }
  return found->second;
}

AreaScriptState AreaScriptRuntime::run() {
  APP_PROFILE_FUNCTION();

  if (!m_active) {
    return m_state;
  }

  if (m_state != AreaScriptState::k_ready && m_state != AreaScriptState::k_running) {
    return m_state;
  }

  if (m_state == AreaScriptState::k_ready) {
    if (m_queued_events.empty()) {
      return m_state;
    }
    m_queued_events.pop_front();
    m_ip = 0;
    m_state = AreaScriptState::k_running;
  }

  std::size_t budget{k_instruction_budget};
  while (m_state == AreaScriptState::k_running && budget > 0U) {
    if (m_ip >= m_script.size()) {
      m_state = AreaScriptState::k_completed;
      break;
    }
    --budget;
    execute_instruction();
  }

  return m_state;
}

void AreaScriptRuntime::execute_instruction() {
  const std::size_t instruction_offset{m_ip};
  const std::uint32_t opcode{read_u8_at(m_script, m_ip)};
  const AreaOpcodeInfo* info{area_opcode_info(opcode)};

  if (info == nullptr) {
    m_pause_info = AreaPauseInfo{};
    m_pause_info.offset = instruction_offset;
    m_pause_info.opcode = opcode;
    m_pause_info.opcode_name = fmt::format("{:#04x}", opcode);
    m_pause_info.reason_text = fmt::format("unhandled area opcode {:#04x}", opcode);
    m_pause_info.nearby_bytes = nearby_bytes_hex(instruction_offset);
    m_state = AreaScriptState::k_paused_unsupported;
    App::Log::warn("AreaScript.Pause: {} at offset {:#x} opcode {:#04x} bytes [{}]",
        m_pause_info.reason_text,
        instruction_offset,
        opcode,
        m_pause_info.nearby_bytes);
    return;
  }

  // Decode operands with exact recovered widths; the SCX dispatch-table
  // metadata is intentionally not used for lengths here.
  std::size_t cursor{m_ip + 1U};
  std::vector<std::int32_t> operands;
  operands.reserve(info->operand_count);
  const std::span<const AreaOperandWidth> widths{info->operands, info->operand_count};
  for (const AreaOperandWidth width : widths) {
    std::size_t width_bytes{0};
    switch (width) {
      case AreaOperandWidth::k_int8:
        width_bytes = 1;
        break;
      case AreaOperandWidth::k_int16:
        width_bytes = 2;
        break;
      case AreaOperandWidth::k_int32:
        width_bytes = 4;
        break;
    }
    if ((cursor + width_bytes) > m_script.size()) {
      m_pause_info = AreaPauseInfo{};
      m_pause_info.offset = instruction_offset;
      m_pause_info.opcode = opcode;
      m_pause_info.opcode_name = info->name;
      m_pause_info.reason_text =
          fmt::format("{}: truncated operands at offset {:#x}", info->name, cursor);
      m_pause_info.nearby_bytes = nearby_bytes_hex(instruction_offset);
      m_state = AreaScriptState::k_failed;
      App::Log::warn("AreaScript.Failed: {} opcode {:#04x}", m_pause_info.reason_text, opcode);
      return;
    }

    switch (width) {
      case AreaOperandWidth::k_int8:
        operands.push_back(static_cast<std::int32_t>(read_i8_at(m_script, cursor)));
        break;
      case AreaOperandWidth::k_int16:
        operands.push_back(static_cast<std::int32_t>(read_i16_at(m_script, cursor)));
        break;
      case AreaOperandWidth::k_int32:
        operands.push_back(read_i32_at(m_script, cursor));
        break;
    }
    cursor += width_bytes;
  }

  if (m_instruction_sink) {
    m_instruction_sink(opcode, operands);
  }

  AreaInstructionTrace entry;
  entry.offset = instruction_offset;
  entry.opcode = opcode;
  entry.opcode_name = info->name;

  switch (opcode) {
    case K_OP_SET_GLOBAL_VARIABLE_ONE:
      m_variables[static_cast<std::uint16_t>(operands.at(0))] = 1;
      entry.effect =
          fmt::format("set global variable {} to 1", static_cast<std::uint16_t>(operands.at(0)));
      break;
    case K_OP_SET_GLOBAL_VARIABLE:
      m_variables[static_cast<std::uint16_t>(operands.at(0))] = operands.at(1);
      entry.effect = fmt::format("set global variable {} to {}",
          static_cast<std::uint16_t>(operands.at(0)),
          operands.at(1));
      break;
    case K_OP_PLAY_MUSIC: {
      const Audio::MusicTrackRequest request{
          .track_id = static_cast<std::int16_t>(operands.at(0)),
          .loop = operands.at(1) != 0,
          .mode_flag = static_cast<std::int16_t>(operands.at(2)),
      };
      if (m_music_sink) {
        m_music_sink(request);
      }
      entry.effect = fmt::format("play music track {} (loop={}, mode={})",
          request.track_id,
          request.loop,
          request.mode_flag);
      break;
    }
    case K_OP_OPEN_INTERFACE: {
      const std::uint16_t interface_id{static_cast<std::uint16_t>(operands.at(0))};
      const std::int16_t operand_b{static_cast<std::int16_t>(operands.at(1))};
      const std::int16_t operand_c{static_cast<std::int16_t>(operands.at(2))};
      const App::InterfaceOpenRequest request{
          .interface_id = interface_id, .operand_b = operand_b, .operand_c = operand_c};
      m_wait_state = K_OPEN_INTERFACE_WAIT_STATE;
      std::optional<App::InterfaceHandle> handle;
      if (m_interface_sink) {
        auto result{m_interface_sink(request)};
        if (!result) {
          m_pause_info = AreaPauseInfo{};
          m_pause_info.offset = instruction_offset;
          m_pause_info.opcode = opcode;
          m_pause_info.opcode_name = info->name;
          m_pause_info.reason_text =
              fmt::format("interface {} open failed: {}", interface_id, result.error());
          m_pause_info.nearby_bytes = nearby_bytes_hex(instruction_offset);
          m_state = AreaScriptState::k_failed;
          App::Log::error("AreaScript.Failed: {}", m_pause_info.reason_text);
          return;
        }
        handle = result.value();
      }
      m_wait = AreaWaitState{.kind = AreaWaitKind::k_interface,
          .runtime_state = K_OPEN_INTERFACE_WAIT_STATE,
          .interface = handle};
      entry.effect =
          fmt::format("open interface {} (operands {}, {})", interface_id, operand_b, operand_c);
      break;
    }
    default:
      // Provisional compatibility action: decode and record the observed
      // state effect without pretending the subsystem is fully implemented.
      entry.effect = fmt::format("provisional compatibility: {}", info->notes);
      App::Log::debug("AreaScript: {} {:#04x} at offset {:#x}: provisional (no state effect)",
          info->name,
          opcode,
          instruction_offset);
      break;
  }

  entry.operands = std::move(operands);
  append_trace(std::move(entry));

  m_ip = cursor;
  ++m_executed_instruction_count;

  if (opcode == K_OP_OPEN_INTERFACE) {
    m_state = AreaScriptState::k_waiting;
    App::Log::info("area script waiting (wait state {})", m_wait_state);
  }
}

void AreaScriptRuntime::append_trace(AreaInstructionTrace entry) {
  m_trace.push_back(std::move(entry));
  if (m_trace.size() > k_trace_capacity) {
    m_trace.pop_front();
  }
}

std::string AreaScriptRuntime::nearby_bytes_hex(const std::size_t offset) const {
  std::string hex;
  for (std::size_t index{0}; index < 8U && (offset + index) < m_script.size(); ++index) {
    if (!hex.empty()) {
      hex += ' ';
    }
    hex += fmt::format("{:02x}", read_u8_at(m_script, offset + index));
  }
  return hex;
}

}  // namespace App::Script

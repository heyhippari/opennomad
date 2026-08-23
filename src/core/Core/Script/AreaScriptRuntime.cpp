#include "Core/Script/AreaScriptRuntime.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Audio/AudioTypes.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Script/AreaScriptOpcode.hpp"
#include "Core/Script/ScriptOpcode.hpp"

namespace App::Script {

namespace {

constexpr std::uint32_t K_OP_END_EVENT{0x03};
constexpr std::uint32_t K_OP_JUMP_RELATIVE{0x04};
constexpr std::uint32_t K_OP_BRANCH_IF_FALSE{0x06};
constexpr std::uint32_t K_OP_PUSH_INT8{0x07};
constexpr std::uint32_t K_OP_PUSH_GLOBAL_VARIABLE{0x0A};
constexpr std::uint32_t K_OP_SET_GLOBAL_VARIABLE_ONE{0x0D};
constexpr std::uint32_t K_OP_SET_GLOBAL_VARIABLE{0x0E};
constexpr std::uint32_t K_OP_EQUAL{0x19};
constexpr std::uint32_t K_OP_BEGIN_AREA_TRANSITION{0x2F};
constexpr std::uint32_t K_OP_CHARACTER_LOOKUP{0x38};
constexpr std::uint32_t K_OP_START_SCX_SCRIPT{0x39};
constexpr std::uint32_t K_OP_START_SCX_SCRIPT_TRACKED{0x3A};
constexpr std::uint32_t K_OP_START_CHARACTER_SCRIPT{0x3B};
constexpr std::uint32_t K_OP_START_CHARACTER_SCRIPT_TRACKED{0x3C};
constexpr std::uint32_t K_OP_START_DIALOG{0x3D};
constexpr std::uint32_t K_OP_ACTIVATE_CHARACTER{0x4E};
constexpr std::uint32_t K_OP_CHARACTER_SELECTION_RESET{0x4F};
constexpr std::uint32_t K_OP_ACTIVATE_SUBSYSTEM{0x68};
constexpr std::uint32_t K_OP_OBJECT_ACTIVATE{0x5C};
constexpr std::uint32_t K_OP_CAMERA_SELECT{0x5F};
constexpr std::uint32_t K_OP_CAMERA_MOVE_WAIT{0x60};
constexpr std::uint32_t K_OP_SUBSYSTEM_OPERATION{0x83};
constexpr std::uint32_t K_OP_PLAY_MUSIC{0x67};
constexpr std::uint32_t K_OP_PRESENTATION_EFFECT{0x76};
constexpr std::uint32_t K_OP_PRESENTATION_EFFECT_ALT{0x77};
constexpr std::uint32_t K_OP_OPEN_INTERFACE{0x46};
constexpr std::uint32_t K_OP_BEGIN_CINEMATIC_LETTERBOX{0x84};
constexpr std::uint32_t K_OP_END_CINEMATIC_LETTERBOX{0x85};

/// Wait state assigned by the interface-open opcode (recovered value 6).
constexpr std::uint16_t K_OPEN_INTERFACE_WAIT_STATE{6};
/// Wait state assigned by tracked script opcodes 0x3A and 0x3C.
constexpr std::uint16_t K_TRACKED_SCRIPT_WAIT_STATE{4};
/// Wait state assigned by camera opcode 0x60 when its duration is non-zero.
constexpr std::uint16_t K_CAMERA_WAIT_STATE{7};
/// Runtime context state while native AREA transition coordination is active.
constexpr std::uint16_t K_AREA_TRANSITION_WAIT_STATE{10};
/// Runtime's script/scenario time base (1.0 unit = 1/30 second).
constexpr float K_AREA_FRAMES_PER_SECOND{30.0F};
/// Runtime Scalar16 parameter-reference marker. The parameter block is not
/// modeled yet, so 0x3D diagnoses these operands instead of treating them as IDs.
constexpr std::uint16_t K_SCALAR16_PARAMETER_REFERENCE{0x4000U};

constexpr std::array<AreaOperandWidth, 0> K_OPERANDS_NONE{};
constexpr std::array<AreaOperandWidth, 1> K_OPERANDS_I8{AreaOperandWidth::k_int8};
constexpr std::array<AreaOperandWidth, 1> K_OPERANDS_I16{AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 1> K_OPERANDS_0D{AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 2> K_OPERANDS_0E{
    AreaOperandWidth::k_int16, AreaOperandWidth::k_int8};
constexpr std::array<AreaOperandWidth, 1> K_OPERANDS_38{AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 2> K_OPERANDS_4E{
    AreaOperandWidth::k_int16, AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 1> K_OPERANDS_4F{AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 1> K_OPERANDS_5C{AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 3> K_OPERANDS_3X_I16{
    AreaOperandWidth::k_int16, AreaOperandWidth::k_int16, AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 2> K_OPERANDS_83{
    AreaOperandWidth::k_int16, AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 3> K_OPERANDS_67{
    AreaOperandWidth::k_int16, AreaOperandWidth::k_int16, AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 3> K_OPERANDS_PRESENTATION{
    AreaOperandWidth::k_int32, AreaOperandWidth::k_int16, AreaOperandWidth::k_int16};

constexpr std::array<AreaOpcodeInfo, 28> K_AREA_OPCODE_TABLE{
    AreaOpcodeInfo{.opcode = K_OP_END_EVENT,
        .name = "EndEvent",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "terminates the current queued AREA event",
        .operands = K_OPERANDS_NONE.data(),
        .operand_count = K_OPERANDS_NONE.size()},
    AreaOpcodeInfo{.opcode = K_OP_JUMP_RELATIVE,
        .name = "JumpRelative",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "signed relative jump from the post-operand instruction pointer",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_BRANCH_IF_FALSE,
        .name = "BranchIfFalse",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "pops one value and jumps when it is zero",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_PUSH_INT8,
        .name = "PushInt8",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "pushes a signed immediate byte",
        .operands = K_OPERANDS_I8.data(),
        .operand_count = K_OPERANDS_I8.size()},
    AreaOpcodeInfo{.opcode = K_OP_PUSH_GLOBAL_VARIABLE,
        .name = "PushGlobalVariable",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "pushes a START/global variable value (zero when unset)",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_SET_GLOBAL_VARIABLE_ONE,
        .name = "SetGlobalVariableOne",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "sets START/global variable <operand 0> to 1",
        .operands = K_OPERANDS_0D.data(),
        .operand_count = K_OPERANDS_0D.size()},
    AreaOpcodeInfo{.opcode = K_OP_EQUAL,
        .name = "Equal",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "pops rhs/lhs and pushes 1 when equal, otherwise 0",
        .operands = K_OPERANDS_NONE.data(),
        .operand_count = K_OPERANDS_NONE.size()},
    AreaOpcodeInfo{.opcode = K_OP_BEGIN_AREA_TRANSITION,
        .name = "BeginAreaTransition",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "begins a two-slot AREA transition and blocks the calling context in recovered "
                 "Runtime state 10 until the destination area is ready; operands 1/2 variants "
                 "remain unresolved",
        .operands = K_OPERANDS_3X_I16.data(),
        .operand_count = K_OPERANDS_3X_I16.size()},
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
    AreaOpcodeInfo{.opcode = K_OP_START_SCX_SCRIPT,
        .name = "StartScxScript",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "resolves operand 0 against active SCX source script +0x1A and continues",
        .operands = K_OPERANDS_3X_I16.data(),
        .operand_count = K_OPERANDS_3X_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_START_SCX_SCRIPT_TRACKED,
        .name = "StartScxScriptTracked",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "starts a generic SCX script and blocks in Runtime state 4 on its exact instance",
        .operands = K_OPERANDS_3X_I16.data(),
        .operand_count = K_OPERANDS_3X_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_START_CHARACTER_SCRIPT,
        .name = "StartCharacterScript",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "requests an explicit-character script and continues without tracking it",
        .operands = K_OPERANDS_3X_I16.data(),
        .operand_count = K_OPERANDS_3X_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_START_CHARACTER_SCRIPT_TRACKED,
        .name = "StartCharacterScriptTracked",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes =
            "requests an explicit-character script and blocks the AREA context in Runtime state 4",
        .operands = K_OPERANDS_3X_I16.data(),
        .operand_count = K_OPERANDS_3X_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_START_DIALOG,
        .name = "StartDialog",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "starts an IAM/DIALOG conversation and forces dispatcher yield without "
                 "entering a typed AREA wait",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_ACTIVATE_CHARACTER,
        .name = "ActivateCharacter",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "reactivates an AREA table-0 character and optionally applies its "
                 "serialized position/orientation; -1 uses the current-character flag path",
        .operands = K_OPERANDS_4E.data(),
        .operand_count = K_OPERANDS_4E.size()},
    AreaOpcodeInfo{.opcode = K_OP_CHARACTER_SELECTION_RESET,
        .name = "CharacterSelectionReset",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "character-related selection/reset behavior",
        .operands = K_OPERANDS_4F.data(),
        .operand_count = K_OPERANDS_4F.size()},
    AreaOpcodeInfo{.opcode = K_OP_OBJECT_ACTIVATE,
        .name = "ObjectActivate",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "object-related activation/load behavior",
        .operands = K_OPERANDS_5C.data(),
        .operand_count = K_OPERANDS_5C.size()},
    AreaOpcodeInfo{.opcode = K_OP_CAMERA_SELECT,
        .name = "CameraSelect",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "selects/schedules an IAM camera without wait-state 7",
        .operands = K_OPERANDS_3X_I16.data(),
        .operand_count = K_OPERANDS_3X_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_CAMERA_MOVE_WAIT,
        .name = "CameraMoveAndWait",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "selects/schedules an IAM camera; nonzero duration waits in state 7",
        .operands = K_OPERANDS_3X_I16.data(),
        .operand_count = K_OPERANDS_3X_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_SUBSYSTEM_OPERATION,
        .name = "SubsystemOperation",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "unresolved subsystem operation",
        .operands = K_OPERANDS_83.data(),
        .operand_count = K_OPERANDS_83.size()},
    AreaOpcodeInfo{.opcode = K_OP_ACTIVATE_SUBSYSTEM,
        .name = "ActivateSubsystem",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "activates a subsystem",
        .operands = K_OPERANDS_NONE.data(),
        .operand_count = K_OPERANDS_NONE.size()},
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
        .notes = "presentation/fade/effect mode 1",
        .operands = K_OPERANDS_PRESENTATION.data(),
        .operand_count = K_OPERANDS_PRESENTATION.size()},
    AreaOpcodeInfo{.opcode = K_OP_PRESENTATION_EFFECT_ALT,
        .name = "PresentationEffectAlt",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "presentation/fade/effect mode 2",
        .operands = K_OPERANDS_PRESENTATION.data(),
        .operand_count = K_OPERANDS_PRESENTATION.size()},
    AreaOpcodeInfo{.opcode = K_OP_OPEN_INTERFACE,
        .name = "OpenInterface",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "opens interface, waits in state 6, writes result to operand 2 variable",
        .operands = K_OPERANDS_3X_I16.data(),
        .operand_count = K_OPERANDS_3X_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_BEGIN_CINEMATIC_LETTERBOX,
        .name = "BeginCinematicLetterbox",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "begins the recovered top/bottom cinematic mask transition",
        .operands = K_OPERANDS_NONE.data(),
        .operand_count = K_OPERANDS_NONE.size()},
    AreaOpcodeInfo{.opcode = K_OP_END_CINEMATIC_LETTERBOX,
        .name = "EndCinematicLetterbox",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "ends the recovered top/bottom cinematic mask transition",
        .operands = K_OPERANDS_NONE.data(),
        .operand_count = K_OPERANDS_NONE.size()},
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

std::expected<std::int16_t, std::string> resolve_scalar16(
    const std::int32_t operand, const std::string_view semantic) {
  const std::int16_t serialized{static_cast<std::int16_t>(operand)};
  const std::uint16_t bits{static_cast<std::uint16_t>(serialized)};
  if (serialized != -1 && (bits & K_SCALAR16_PARAMETER_REFERENCE) != 0U) {
    return std::expected<std::int16_t, std::string>{std::unexpect,
        fmt::format("{} parameter-indirected Scalar16 {:#06x} is unsupported because the AREA "
                    "parameter block is not modeled",
            semantic,
            bits)};
  }
  return serialized;
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

void AreaScriptRuntime::set_scx_script_sink(ScxScriptSink sink) {
  m_scx_script_sink = std::move(sink);
}

void AreaScriptRuntime::set_character_script_sink(CharacterScriptSink sink) {
  m_character_script_sink = std::move(sink);
}

void AreaScriptRuntime::set_dialog_sink(DialogSink sink) {
  m_dialog_sink = std::move(sink);
}

void AreaScriptRuntime::set_area_transition_sink(AreaTransitionSink sink) {
  m_area_transition_sink = std::move(sink);
}

void AreaScriptRuntime::set_character_activation_sink(CharacterActivationSink sink) {
  m_character_activation_sink = std::move(sink);
}

void AreaScriptRuntime::set_camera_sink(CameraSink sink) {
  m_camera_sink = std::move(sink);
}

void AreaScriptRuntime::set_presentation_sink(PresentationSink sink) {
  m_presentation_sink = std::move(sink);
}

void AreaScriptRuntime::set_cinematic_letterbox_sink(CinematicLetterboxSink sink) {
  m_cinematic_letterbox_sink = std::move(sink);
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
  if (m_wait.interface_result_variable.has_value()) {
    m_variables[m_wait.interface_result_variable.value()] =
        static_cast<std::int32_t>(completion.result);
  }
  m_wait = AreaWaitState{};
  m_wait_state = 0;
  m_state = AreaScriptState::k_running;
  App::Log::debug(LogCategory::Script,
      "area script resumed after interface {} at +{:#x}",
      completion.handle.interface_id,
      m_ip);
  return {};
}

std::expected<void, std::string> AreaScriptRuntime::complete_scx_script_wait(
    const std::size_t instance_id) {
  if (m_state != AreaScriptState::k_waiting) {
    return std::expected<void, std::string>{std::unexpect, "area script is not waiting"};
  }
  if (m_wait.kind != AreaWaitKind::k_scx_script) {
    return std::expected<void, std::string>{
        std::unexpect, "area script is not waiting on an SCX script"};
  }
  if (!m_wait.scx_script_instance.has_value() ||
      m_wait.scx_script_instance.value() != instance_id) {
    return std::expected<void, std::string>{
        std::unexpect, "SCX script completion does not match the waiting instance"};
  }

  m_wait = AreaWaitState{};
  m_wait_state = 0;
  m_state = AreaScriptState::k_running;
  App::Log::debug(LogCategory::Script,
      "area script resumed after SCX script instance {} at +{:#x}",
      instance_id,
      m_ip);
  return {};
}

std::expected<void, std::string> AreaScriptRuntime::complete_character_script_wait(
    const std::size_t instance_id) {
  if (m_state != AreaScriptState::k_waiting) {
    return std::expected<void, std::string>{std::unexpect, "area script is not waiting"};
  }
  if (m_wait.kind != AreaWaitKind::k_character_script) {
    return std::expected<void, std::string>{
        std::unexpect, "area script is not waiting on a character script"};
  }
  if (!m_wait.character_script_instance.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, "character-script wait has no tracked instance"};
  }
  if (m_wait.character_script_instance.value() != instance_id) {
    return std::expected<void, std::string>{
        std::unexpect, "character-script completion does not match the waiting instance"};
  }

  m_wait = AreaWaitState{};
  m_wait_state = 0;
  m_state = AreaScriptState::k_running;

  App::Log::debug(LogCategory::Script,
      "area script resumed after character-script instance {} (Runtime state 4 -> 1) at +{:#x}",
      instance_id,
      m_ip);

  return {};
}

std::expected<void, std::string> AreaScriptRuntime::complete_area_transition(
    const AreaTransitionHandle handle) {
  if (m_state != AreaScriptState::k_waiting) {
    return std::expected<void, std::string>{std::unexpect, "area script is not waiting"};
  }
  if (m_wait.kind != AreaWaitKind::k_area_transition) {
    return std::expected<void, std::string>{
        std::unexpect, "area script is not waiting on an AREA transition"};
  }
  if (!m_wait.area_transition_handle.has_value() ||
      m_wait.area_transition_handle.value() != handle) {
    return std::expected<void, std::string>{
        std::unexpect, "AREA transition completion does not match the waiting generation"};
  }

  m_wait = AreaWaitState{};
  m_wait_state = 0;
  m_state = AreaScriptState::k_running;
  App::Log::debug(LogCategory::Script,
      "area script resumed after AREA transition generation {} at +{:#x}",
      handle.generation,
      m_ip);
  return {};
}

std::optional<std::int32_t> AreaScriptRuntime::variable(const std::uint16_t id) const {
  const auto found{m_variables.find(id)};
  if (found == m_variables.end()) {
    return std::nullopt;
  }
  return found->second;
}

AreaScriptState AreaScriptRuntime::run(const float real_delta_seconds) {
  APP_PROFILE_FUNCTION();

  m_last_run_yielded = false;

  if (!m_active) {
    return m_state;
  }

  // Camera opcode 0x60 uses legacy wait state 7. Until the WorldRenderer owns
  // Runtime's callback path, advance the recovered duration in the same 30 Hz
  // scenario units used by the rest of the script system.
  if (m_state == AreaScriptState::k_waiting && m_wait.kind == AreaWaitKind::k_camera) {
    const float delta_frames{std::max(0.0F, real_delta_seconds) * K_AREA_FRAMES_PER_SECOND};
    m_wait.remaining_scenario_frames =
        std::max(0.0F, m_wait.remaining_scenario_frames - delta_frames);
    if (m_wait.remaining_scenario_frames > 0.0F) {
      return m_state;
    }
    m_wait = AreaWaitState{};
    m_wait_state = 0;
    m_state = AreaScriptState::k_running;
  }

  if (m_state != AreaScriptState::k_ready && m_state != AreaScriptState::k_running) {
    return m_state;
  }

  if (m_state == AreaScriptState::k_ready) {
    if (m_queued_events.empty()) {
      return m_state;
    }
    m_active_event = m_queued_events.front();
    m_queued_events.pop_front();
    m_ip = 0;
    m_evaluation_stack.clear();
    m_state = AreaScriptState::k_running;
  }

  m_yield_requested = false;
  std::size_t budget{k_instruction_budget};
  while (m_state == AreaScriptState::k_running && budget > 0U) {
    if (m_ip >= m_script.size()) {
      m_state = AreaScriptState::k_completed;
      m_active_event.reset();
      break;
    }
    --budget;
    execute_instruction();
    if (m_yield_requested) {
      m_last_run_yielded = true;
      break;
    }
  }

  return m_state;
}

std::expected<std::int32_t, std::string> AreaScriptRuntime::pop_evaluation_value() {
  if (m_evaluation_stack.empty()) {
    return std::expected<std::int32_t, std::string>{
        std::unexpect, "AREA evaluation stack underflow"};
  }
  const std::int32_t value{m_evaluation_stack.back()};
  m_evaluation_stack.pop_back();
  return value;
}

std::expected<std::size_t, std::string> AreaScriptRuntime::relative_target(
    const std::size_t base, const std::int32_t displacement) const {
  const std::int64_t target{
      static_cast<std::int64_t>(base) + static_cast<std::int64_t>(displacement)};
  if (std::cmp_less(target, 0) || std::cmp_greater(target, m_script.size())) {
    return std::expected<std::size_t, std::string>{std::unexpect,
        fmt::format(
            "relative branch target {:#x} outside script size {:#x}", target, m_script.size())};
  }
  return static_cast<std::size_t>(target);
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
    App::Log::warn(LogCategory::Script,
        "area script paused — unsupported opcode={:#04x} offset=+{:#x} bytes=[{}]",
        opcode,
        instruction_offset,
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
      App::Log::warn(LogCategory::Script,
          "area script failed — {} opcode={:#04x}",
          m_pause_info.reason_text,
          opcode);
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
  std::size_t next_ip{cursor};
  bool wait_after_instruction{false};

  switch (opcode) {
    case K_OP_END_EVENT:
      entry.effect = "terminate current AREA event";
      m_evaluation_stack.clear();
      m_active_event.reset();
      m_state = AreaScriptState::k_ready;
      break;
    case K_OP_JUMP_RELATIVE: {
      auto target{relative_target(cursor, operands.at(0))};
      if (!target) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = target.error(),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      next_ip = target.value();
      entry.effect = fmt::format("jump to +{:#x}", next_ip);
      break;
    }
    case K_OP_BRANCH_IF_FALSE: {
      auto condition{pop_evaluation_value()};
      if (!condition) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = condition.error(),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      if (condition.value() == 0) {
        auto target{relative_target(cursor, operands.at(0))};
        if (!target) {
          m_pause_info = AreaPauseInfo{.offset = instruction_offset,
              .opcode = opcode,
              .opcode_name = std::string{info->name},
              .reason_text = target.error(),
              .nearby_bytes = nearby_bytes_hex(instruction_offset)};
          m_state = AreaScriptState::k_failed;
          return;
        }
        next_ip = target.value();
      }
      entry.effect = fmt::format("condition={} next=+{:#x}", condition.value(), next_ip);
      break;
    }
    case K_OP_PUSH_INT8:
      m_evaluation_stack.push_back(operands.at(0));
      entry.effect = fmt::format("push {}", operands.at(0));
      break;
    case K_OP_PUSH_GLOBAL_VARIABLE: {
      const std::uint16_t id{static_cast<std::uint16_t>(operands.at(0))};
      const std::int32_t value{variable(id).value_or(0)};
      m_evaluation_stack.push_back(value);
      entry.effect = fmt::format("push global variable {} ({})", id, value);
      break;
    }
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
    case K_OP_EQUAL: {
      auto rhs{pop_evaluation_value()};
      auto lhs{pop_evaluation_value()};
      if (!rhs || !lhs) {
        const std::string reason{!rhs ? rhs.error() : lhs.error()};
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = reason,
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      const std::int32_t result{lhs.value() == rhs.value() ? 1 : 0};
      m_evaluation_stack.push_back(result);
      entry.effect = fmt::format("{} == {} -> {}", lhs.value(), rhs.value(), result);
      break;
    }
    case K_OP_BEGIN_AREA_TRANSITION: {
      std::array<std::int16_t, 3> resolved{};
      constexpr std::array<std::string_view, 3> k_semantics{
          "BeginAreaTransition target", "BeginAreaTransition operand_b", "BeginAreaTransition operand_c"};
      for (std::size_t index{0}; index < resolved.size(); ++index) {
        auto value{resolve_scalar16(operands.at(index), k_semantics.at(index))};
        if (!value) {
          m_pause_info = AreaPauseInfo{.offset = instruction_offset,
              .opcode = opcode,
              .opcode_name = std::string{info->name},
              .reason_text = value.error(),
              .nearby_bytes = nearby_bytes_hex(instruction_offset)};
          m_state = AreaScriptState::k_failed;
          return;
        }
        resolved.at(index) = value.value();
      }

      const AreaTransitionRequest request{.target_area_id = resolved.at(0),
          .operand_b = resolved.at(1),
          .operand_c = resolved.at(2)};
      m_last_area_transition_request = request;
      if (request.operand_b != -1 || request.operand_c != -1) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = fmt::format(
                "AREA transition variant ({}, {}) is not yet implemented",
                request.operand_b,
                request.operand_c),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      if (!m_area_transition_sink) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = "AREA transition bridge is not wired",
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }

      auto accepted{m_area_transition_sink(request)};
      if (!accepted) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = fmt::format(
                "failed to begin AREA transition to {}: {}", request.target_area_id, accepted.error()),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }

      m_wait_state = K_AREA_TRANSITION_WAIT_STATE;
      m_wait = AreaWaitState{.kind = AreaWaitKind::k_area_transition,
          .runtime_state = K_AREA_TRANSITION_WAIT_STATE,
          .interface = std::nullopt,
          .interface_result_variable = std::nullopt,
          .scx_script_instance = std::nullopt,
          .character_script = std::nullopt,
          .character_script_instance = std::nullopt,
          .area_transition = request,
          .area_transition_handle = accepted.value(),
          .remaining_scenario_frames = 0.0F};
      wait_after_instruction = true;
      entry.effect = fmt::format("begin AREA transition to {} as generation {} and wait in "
                                 "Runtime state 10",
          request.target_area_id,
          accepted->generation);
      break;
    }
    case K_OP_START_SCX_SCRIPT:
    case K_OP_START_SCX_SCRIPT_TRACKED: {
      if (!m_scx_script_sink) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = "SCX script bridge is not wired",
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      const AreaScxScriptRequest request{.script_id = static_cast<std::uint16_t>(operands.at(0)),
          .operand_b = static_cast<std::int16_t>(operands.at(1)),
          .operand_c = static_cast<std::int16_t>(operands.at(2))};
      auto instance{m_scx_script_sink(request)};
      if (!instance) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = fmt::format(
                "failed to start SCX script {}: {}", request.script_id, instance.error()),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      if (opcode == K_OP_START_SCX_SCRIPT_TRACKED) {
        m_wait_state = K_TRACKED_SCRIPT_WAIT_STATE;
        m_wait = AreaWaitState{.kind = AreaWaitKind::k_scx_script,
            .runtime_state = K_TRACKED_SCRIPT_WAIT_STATE,
            .interface = std::nullopt,
            .interface_result_variable = std::nullopt,
            .scx_script_instance = instance.value(),
            .character_script = std::nullopt,
            .character_script_instance = std::nullopt,
            .area_transition = std::nullopt,
            .area_transition_handle = std::nullopt,
            .remaining_scenario_frames = 0.0F};
        wait_after_instruction = true;
        entry.effect = fmt::format("start SCX script {} as instance {} and wait in Runtime state 4",
            request.script_id,
            instance.value());
      } else {
        entry.effect = fmt::format("start SCX script {} as instance {} (fire-and-forget)",
            request.script_id,
            instance.value());
      }
      break;
    }
    case K_OP_START_CHARACTER_SCRIPT:
    case K_OP_START_CHARACTER_SCRIPT_TRACKED: {
      if (!m_character_script_sink) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = "character-script bridge is not wired",
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }

      const bool tracked{opcode == K_OP_START_CHARACTER_SCRIPT_TRACKED};
      const AreaCharacterScriptRequest request{
          .character_id = static_cast<std::int16_t>(operands.at(0)),
          .script_id = static_cast<std::uint16_t>(operands.at(1)),
          .parameter = static_cast<std::int16_t>(operands.at(2)),
          .mode = tracked ? AreaCharacterScriptLaunchMode::k_tracked
                          : AreaCharacterScriptLaunchMode::k_fire_and_forget};

      m_last_character_script_request = request;

      auto instance{m_character_script_sink(request)};
      if (!instance) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = fmt::format("failed to request character {} script {}: {}",
                request.character_id,
                request.script_id,
                instance.error()),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }

      if (tracked) {
        m_wait_state = K_TRACKED_SCRIPT_WAIT_STATE;
        m_wait = AreaWaitState{.kind = AreaWaitKind::k_character_script,
            .runtime_state = K_TRACKED_SCRIPT_WAIT_STATE,
            .interface = std::nullopt,
            .interface_result_variable = std::nullopt,
            .scx_script_instance = std::nullopt,
            .character_script = request,
            .character_script_instance = instance.value(),
            .area_transition = std::nullopt,
            .area_transition_handle = std::nullopt,
            .remaining_scenario_frames = 0.0F};
        wait_after_instruction = true;
        entry.effect = fmt::format(
            "start character {} script {} parameter {} as instance {} and wait in "
            "Runtime state 4",
            request.character_id,
            request.script_id,
            request.parameter,
            instance.value());
      } else {
        entry.effect = fmt::format(
            "start character {} script {} parameter {} as instance {} (fire-and-forget)",
            request.character_id,
            request.script_id,
            request.parameter,
            instance.value());
      }
      break;
    }
    case K_OP_START_DIALOG: {
      auto dialog_id{resolve_scalar16(operands.at(0), "StartDialog")};
      if (!dialog_id) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = dialog_id.error(),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      if (!m_dialog_sink) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = "dialog bridge is not wired",
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }

      const AreaDialogRequest request{.dialog_id = dialog_id.value()};
      m_last_dialog_request = request;
      if (auto started{m_dialog_sink(request)}; !started) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text =
                fmt::format("failed to start dialog {}: {}", request.dialog_id, started.error()),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }

      entry.effect = fmt::format("start dialog {} and yield", request.dialog_id);
      // Runtime returns from the outer AREA dispatcher immediately after
      // 0x3D. The context remains running at the already-advanced IP; global
      // dialog takeover suppresses later AREA ticks until completion.
      m_yield_requested = true;
      break;
    }
    case K_OP_ACTIVATE_CHARACTER: {
      // Runtime handler 0x00403CB0:
      //   operand 0 -> CHARACTERS ID, resolved through AREA table 0
      //   operand 1 -> nonzero: apply table-0 position/orientation
      //
      // A normal character is reactivated through 0x0041CCA0 and its AREA
      // presence bit is set through 0x0040AF30. When operand 1 is nonzero,
      // 0x0041BDF0 applies the table record's x/y/z/orientation transform.
      //
      // character -1 instead operates on the current character and clears
      // model flag bit 0x2 through 0x0041CED0.
      //
      // There is deliberately no wait or dispatcher yield here.
      const AreaCharacterActivationRequest request{
          .character_id = static_cast<std::int16_t>(operands.at(0)),
          .apply_area_transform = operands.at(1) != 0};
      m_last_character_activation_request = request;

      if (m_character_activation_sink) {
        auto activated{m_character_activation_sink(request)};
        if (!activated) {
          m_pause_info = AreaPauseInfo{.offset = instruction_offset,
              .opcode = opcode,
              .opcode_name = std::string{info->name},
              .reason_text = fmt::format(
                  "failed to activate character {}: {}", request.character_id, activated.error()),
              .nearby_bytes = nearby_bytes_hex(instruction_offset)};
          m_state = AreaScriptState::k_failed;
          return;
        }
      }
      entry.effect = request.character_id == -1
                         ? "clear current-character runtime flag 0x2"
                         : fmt::format("activate character {}{}",
                               request.character_id,
                               request.apply_area_transform ? " at AREA transform" : "");
      break;
    }
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
          App::Log::error(LogCategory::Script, "area script failed — {}", m_pause_info.reason_text);
          return;
        }
        handle = result.value();
      }
      m_wait = AreaWaitState{.kind = AreaWaitKind::k_interface,
          .runtime_state = K_OPEN_INTERFACE_WAIT_STATE,
          .interface = handle,
          .interface_result_variable =
              operand_c >= 0 ? std::optional<std::uint16_t>{static_cast<std::uint16_t>(operand_c)}
                             : std::nullopt,
          .scx_script_instance = std::nullopt,
          .character_script = std::nullopt,
          .character_script_instance = std::nullopt,
          .area_transition = std::nullopt,
          .area_transition_handle = std::nullopt,
          .remaining_scenario_frames = 0.0F};
      wait_after_instruction = true;
      entry.effect = fmt::format(
          "open interface {} (operand {}, result variable {})", interface_id, operand_b, operand_c);
      break;
    }
    case K_OP_CAMERA_SELECT:
    case K_OP_CAMERA_MOVE_WAIT: {
      const std::int16_t duration{static_cast<std::int16_t>(operands.at(1))};
      const bool wait{opcode == K_OP_CAMERA_MOVE_WAIT && duration != 0};
      m_last_camera_request =
          AreaCameraRequest{.camera_id = static_cast<std::uint16_t>(operands.at(0)),
              .duration_units = duration,
              .flags = static_cast<std::int16_t>(operands.at(2)),
              .wait_for_completion = wait};
      if (m_camera_sink) {
        m_camera_sink(m_last_camera_request.value());
      }
      entry.effect = fmt::format("camera {} duration={} flags={}{}",
          m_last_camera_request->camera_id,
          duration,
          m_last_camera_request->flags,
          wait ? " and wait" : "");
      if (wait) {
        const float duration_frames{
            duration < 0 ? -static_cast<float>(duration) : static_cast<float>(duration)};
        m_wait_state = K_CAMERA_WAIT_STATE;
        m_wait = AreaWaitState{.kind = AreaWaitKind::k_camera,
            .runtime_state = K_CAMERA_WAIT_STATE,
            .interface = std::nullopt,
            .interface_result_variable = std::nullopt,
            .scx_script_instance = std::nullopt,
            .character_script = std::nullopt,
            .character_script_instance = std::nullopt,
            .area_transition = std::nullopt,
            .area_transition_handle = std::nullopt,
            .remaining_scenario_frames = duration_frames};
        wait_after_instruction = true;
      }
      m_yield_requested = true;
      break;
    }
    case K_OP_PRESENTATION_EFFECT:
    case K_OP_PRESENTATION_EFFECT_ALT: {
      const std::uint8_t mode{
          opcode == K_OP_PRESENTATION_EFFECT ? std::uint8_t{1} : std::uint8_t{2}};
      m_last_presentation_request = AreaPresentationRequest{.mode = mode,
          .color = static_cast<std::uint32_t>(operands.at(0)),
          .operand_b = static_cast<std::int16_t>(operands.at(1)),
          .operand_c = static_cast<std::int16_t>(operands.at(2))};
      if (m_presentation_sink) {
        m_presentation_sink(m_last_presentation_request.value());
      }
      entry.effect = fmt::format("presentation mode={} color={:#010x} args=({}, {})",
          mode,
          m_last_presentation_request->color,
          m_last_presentation_request->operand_b,
          m_last_presentation_request->operand_c);
      // Runtime's all-zero mode-1 bootstrap command is a no-op and continues
      // immediately. Mode 2 and non-empty mode-1 effects set the central
      // dispatcher-yield flag.
      m_yield_requested = opcode == K_OP_PRESENTATION_EFFECT_ALT ||
                          m_last_presentation_request->color != 0U ||
                          m_last_presentation_request->operand_b != 0 ||
                          m_last_presentation_request->operand_c != 0;
      break;
    }
    case K_OP_BEGIN_CINEMATIC_LETTERBOX:
      m_cinematic_letterbox_requested = true;
      if (m_cinematic_letterbox_sink) {
        m_cinematic_letterbox_sink(AreaCinematicLetterboxRequest{.enabled = true});
      }
      entry.effect = "begin cinematic top/bottom mask transition";
      break;
    case K_OP_END_CINEMATIC_LETTERBOX:
      m_cinematic_letterbox_requested = false;
      if (m_cinematic_letterbox_sink) {
        m_cinematic_letterbox_sink(AreaCinematicLetterboxRequest{.enabled = false});
      }
      entry.effect = "end cinematic top/bottom mask transition";
      break;
    default:
      // Provisional compatibility action: decode and record the observed
      // state effect without pretending the subsystem is fully implemented.
      entry.effect = fmt::format("provisional compatibility: {}", info->notes);
      App::Log::debug(LogCategory::Script,
          "{} {:#04x} at +{:#x}: provisional (no state effect)",
          info->name,
          opcode,
          instruction_offset);
      break;
  }

  entry.operands = std::move(operands);
  append_trace(std::move(entry));

  m_ip = next_ip;
  ++m_executed_instruction_count;

  if (wait_after_instruction) {
    m_state = AreaScriptState::k_waiting;
    App::Log::debug(LogCategory::Script,
        "area script waiting (kind={}, wait state={})",
        static_cast<int>(m_wait.kind),
        m_wait_state);
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

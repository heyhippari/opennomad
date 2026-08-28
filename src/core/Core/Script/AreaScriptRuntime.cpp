#include "Core/Script/AreaScriptRuntime.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <expected>
#include <limits>
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
constexpr std::uint32_t K_OP_SET_GLOBAL_VARIABLE_ZERO{0x0C};
constexpr std::uint32_t K_OP_SET_GLOBAL_VARIABLE_ONE{0x0D};
constexpr std::uint32_t K_OP_SET_GLOBAL_VARIABLE{0x0E};
constexpr std::uint32_t K_OP_ADD_STACK_TO_GLOBAL_VARIABLE{0x13};
constexpr std::uint32_t K_OP_SUBTRACT_STACK_FROM_GLOBAL_VARIABLE{0x14};
constexpr std::uint32_t K_OP_MULTIPLY_GLOBAL_VARIABLE_BY_STACK{0x15};
constexpr std::uint32_t K_OP_DIVIDE_GLOBAL_VARIABLE_BY_STACK{0x16};
constexpr std::uint32_t K_OP_AND_GLOBAL_VARIABLE_WITH_STACK{0x17};
constexpr std::uint32_t K_OP_OR_GLOBAL_VARIABLE_WITH_STACK{0x18};
constexpr std::uint32_t K_OP_EQUAL{0x19};
constexpr std::uint32_t K_OP_START_CURRENT_CHARACTER_SCRIPT_TRACKED{0x2E};
constexpr std::uint32_t K_OP_BEGIN_AREA_TRANSITION{0x2F};
constexpr std::uint32_t K_OP_RELEASE_AREA{0x30};
constexpr std::uint32_t K_OP_ADD_OBJECT_TO_PERSISTENT_COLLECTION{0x32};
constexpr std::uint32_t K_OP_SELECT_CURRENT_CHARACTER{0x38};
constexpr std::uint32_t K_OP_START_SCX_SCRIPT{0x39};
constexpr std::uint32_t K_OP_START_SCX_SCRIPT_TRACKED{0x3A};
constexpr std::uint32_t K_OP_START_CHARACTER_SCRIPT{0x3B};
constexpr std::uint32_t K_OP_START_CHARACTER_SCRIPT_TRACKED{0x3C};
constexpr std::uint32_t K_OP_START_DIALOG{0x3D};
constexpr std::uint32_t K_OP_START_CURRENT_CHARACTER_MOVE{0x3F};
constexpr std::uint32_t K_OP_ACTIVATE_ZONE{0x40};
constexpr std::uint32_t K_OP_DEACTIVATE_ZONE{0x41};
constexpr std::uint32_t K_OP_ENABLE_OBJECT_PLACEMENT{0x4C};
constexpr std::uint32_t K_OP_DISABLE_OBJECT_PLACEMENT{0x4D};
constexpr std::uint32_t K_OP_ACTIVATE_CHARACTER{0x4E};
constexpr std::uint32_t K_OP_DEACTIVATE_CHARACTER{0x4F};
constexpr std::uint32_t K_OP_GET_CHARACTER_VALUE_TO_VARIABLE{0x56};
constexpr std::uint32_t K_OP_START_CURRENT_CHARACTER_SCRIPT{0x5A};
constexpr std::uint32_t K_OP_SET_CHARACTER_VALUE_FROM_VARIABLE{0x5D};
constexpr std::uint32_t K_OP_SET_CURRENT_CHARACTER_CONTROLLER_ON{0x68};
constexpr std::uint32_t K_OP_SET_CURRENT_CHARACTER_CONTROLLER_OFF{0x69};
constexpr std::uint32_t K_OP_OBJECT_ACTIVATE{0x5C};
constexpr std::uint32_t K_OP_CAMERA_SELECT{0x5F};
constexpr std::uint32_t K_OP_CAMERA_MOVE_WAIT{0x60};
constexpr std::uint32_t K_OP_SUBSYSTEM_OPERATION{0x83};
constexpr std::uint32_t K_OP_PLAY_MUSIC{0x67};
constexpr std::uint32_t K_OP_PRESENTATION_EFFECT{0x76};
constexpr std::uint32_t K_OP_PRESENTATION_EFFECT_ALT{0x77};
constexpr std::uint32_t K_OP_OPEN_INTERFACE{0x46};
constexpr std::uint32_t K_OP_ATTACH_AREA_SCENE{0x47};
constexpr std::uint32_t K_OP_PLACE_CURRENT_CHARACTER_AT_ADDRESS{0x49};
constexpr std::uint32_t K_OP_SET_ADDRESS_FLAG{0x57};
constexpr std::uint32_t K_OP_CLEAR_ADDRESS_FLAG{0x58};
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
/// Runtime Scalar16 parameter-reference marker.
constexpr std::uint16_t K_SCALAR16_PARAMETER_REFERENCE{0x4000U};
constexpr std::uint16_t K_SCALAR16_PARAMETER_INDEX_MASK{0x3FFFU};

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
constexpr std::array<AreaOperandWidth, 2> K_OPERANDS_CURRENT_CHARACTER_SCRIPT{
    AreaOperandWidth::k_int16, AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 2> K_OPERANDS_83{
    AreaOperandWidth::k_int16, AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 3> K_OPERANDS_67{
    AreaOperandWidth::k_int16, AreaOperandWidth::k_int16, AreaOperandWidth::k_int16};
constexpr std::array<AreaOperandWidth, 3> K_OPERANDS_PRESENTATION{
    AreaOperandWidth::k_int32, AreaOperandWidth::k_int16, AreaOperandWidth::k_int16};

[[nodiscard]] std::int32_t wrapping_add(const std::int32_t lhs, const std::int32_t rhs) {
  return std::bit_cast<std::int32_t>(
      static_cast<std::uint32_t>(lhs) + static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] std::int32_t wrapping_subtract(const std::int32_t lhs, const std::int32_t rhs) {
  return std::bit_cast<std::int32_t>(
      static_cast<std::uint32_t>(lhs) - static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] std::int32_t wrapping_multiply(const std::int32_t lhs, const std::int32_t rhs) {
  return std::bit_cast<std::int32_t>(
      static_cast<std::uint32_t>(lhs) * static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] std::int32_t bitwise_and(const std::int32_t lhs, const std::int32_t rhs) {
  return std::bit_cast<std::int32_t>(
      static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] std::int32_t bitwise_or(const std::int32_t lhs, const std::int32_t rhs) {
  return std::bit_cast<std::int32_t>(
      static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

constexpr std::array<AreaOpcodeInfo, 51> K_AREA_OPCODE_TABLE{
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
        .notes = "pushes a shared session START/global variable value",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_SET_GLOBAL_VARIABLE_ZERO,
        .name = "SetGlobalVariableZero",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "resolves one Scalar16 and sets that shared START/global variable to zero",
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
    AreaOpcodeInfo{.opcode = K_OP_START_CURRENT_CHARACTER_SCRIPT_TRACKED,
        .name = "StartCurrentCharacterScriptTracked",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "starts raw-u16 SCX script ID on the session current character and blocks in "
                 "Runtime state 4 on its exact instance; operand 1 is Scalar16 camera duration",
        .operands = K_OPERANDS_CURRENT_CHARACTER_SCRIPT.data(),
        .operand_count = K_OPERANDS_CURRENT_CHARACTER_SCRIPT.size()},
    AreaOpcodeInfo{.opcode = K_OP_BEGIN_AREA_TRANSITION,
        .name = "BeginAreaTransition",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "begins a two-slot AREA transition and blocks the calling context in recovered "
                 "Runtime state 10 until the destination area is ready; operands 1/2 variants "
                 "remain unresolved",
        .operands = K_OPERANDS_3X_I16.data(),
        .operand_count = K_OPERANDS_3X_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_RELEASE_AREA,
        .name = "ReleaseArea",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "releases the requested resident AREA without waiting",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_ADD_OBJECT_TO_PERSISTENT_COLLECTION,
        .name = "AddObjectToPersistentCollection",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "inserts an OBJECTS ID into persistent collection kind 0, 1, or 2 without waiting",
        .operands = K_OPERANDS_4E.data(),
        .operand_count = K_OPERANDS_4E.size()},
    AreaOpcodeInfo{.opcode = K_OP_SET_GLOBAL_VARIABLE,
        .name = "SetGlobalVariable",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "sets START/global variable <operand 0> to <operand 1>",
        .operands = K_OPERANDS_0E.data(),
        .operand_count = K_OPERANDS_0E.size()},
    AreaOpcodeInfo{.opcode = K_OP_ADD_STACK_TO_GLOBAL_VARIABLE,
        .name = "AddStackToGlobalVariable",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "adds one evaluation-stack dword to a Scalar16 shared global variable",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_SUBTRACT_STACK_FROM_GLOBAL_VARIABLE,
        .name = "SubtractStackFromGlobalVariable",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "subtracts one evaluation-stack dword from a Scalar16 shared global variable",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_MULTIPLY_GLOBAL_VARIABLE_BY_STACK,
        .name = "MultiplyGlobalVariableByStack",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "multiplies a Scalar16 shared global variable by one evaluation-stack dword",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_DIVIDE_GLOBAL_VARIABLE_BY_STACK,
        .name = "DivideGlobalVariableByStack",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "signed-divides a Scalar16 shared global variable by one evaluation-stack dword",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_AND_GLOBAL_VARIABLE_WITH_STACK,
        .name = "AndGlobalVariableWithStack",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "bitwise-ANDs a Scalar16 shared global variable with one evaluation-stack dword",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_OR_GLOBAL_VARIABLE_WITH_STACK,
        .name = "OrGlobalVariableWithStack",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "bitwise-ORs a Scalar16 shared global variable with one evaluation-stack dword",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_SELECT_CURRENT_CHARACTER,
        .name = "SelectCurrentCharacter",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "resolves Scalar16 and selects/materializes the owner AREA or attached SCENE "
                 "character without waiting",
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
        .notes = "requests an explicit-character script and blocks the AREA context in Runtime "
                 "state 4",
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
    AreaOpcodeInfo{.opcode = K_OP_START_CURRENT_CHARACTER_MOVE,
        .name = "StartCurrentCharacterMove",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "selects a current-actor CTL move/control-bank record; CTL execution remains "
                 "deferred",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_ACTIVATE_ZONE,
        .name = "ActivateZone",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "sets a persistent Scalar16 ZONE bit and rebuilds resident active zones",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_DEACTIVATE_ZONE,
        .name = "DeactivateZone",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "clears a persistent Scalar16 ZONE bit and rebuilds resident active zones",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_ENABLE_OBJECT_PLACEMENT,
        .name = "EnableObjectPlacement",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "sets bit 1 of an existing AREA/SCENE table-1 placement and enables its live "
                 "object",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_DISABLE_OBJECT_PLACEMENT,
        .name = "DisableObjectPlacement",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "clears bit 1 of an existing AREA/SCENE table-1 placement and disables its "
                 "live object",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_ACTIVATE_CHARACTER,
        .name = "ActivateCharacter",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "reactivates an AREA table-0 character and optionally applies its "
                 "serialized position/orientation; -1 enables current-body presentation",
        .operands = K_OPERANDS_4E.data(),
        .operand_count = K_OPERANDS_4E.size()},
    AreaOpcodeInfo{.opcode = K_OP_DEACTIVATE_CHARACTER,
        .name = "DeactivateCharacter",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "-1 disables current-body presentation; other IDs deactivate an owner-resident "
                 "non-current body without waiting",
        .operands = K_OPERANDS_4F.data(),
        .operand_count = K_OPERANDS_4F.size()},
    AreaOpcodeInfo{.opcode = K_OP_GET_CHARACTER_VALUE_TO_VARIABLE,
        .name = "GetCharacterValueToVariable",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "reads one session character-profile numeric kind into a shared global variable",
        .operands = K_OPERANDS_3X_I16.data(),
        .operand_count = K_OPERANDS_3X_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_START_CURRENT_CHARACTER_SCRIPT,
        .name = "StartCurrentCharacterScript",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "starts raw-u16 SCX script ID on the session current character without "
                 "tracking it; operand 1 is Scalar16 camera duration",
        .operands = K_OPERANDS_CURRENT_CHARACTER_SCRIPT.data(),
        .operand_count = K_OPERANDS_CURRENT_CHARACTER_SCRIPT.size()},
    AreaOpcodeInfo{.opcode = K_OP_SET_CHARACTER_VALUE_FROM_VARIABLE,
        .name = "SetCharacterValueFromVariable",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "writes one shared global variable into a session character-profile numeric kind",
        .operands = K_OPERANDS_3X_I16.data(),
        .operand_count = K_OPERANDS_3X_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_OBJECT_ACTIVATE,
        .name = "ObjectActivate",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "submits a nonblocking IAM/OBJECT presentation and yields one dispatcher turn",
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
    AreaOpcodeInfo{.opcode = K_OP_SET_CURRENT_CHARACTER_CONTROLLER_ON,
        .name = "SetCurrentCharacterControllerEnabled",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes = "sets the current actor controller boolean true; its gameplay label is unresolved",
        .operands = K_OPERANDS_NONE.data(),
        .operand_count = K_OPERANDS_NONE.size()},
    AreaOpcodeInfo{.opcode = K_OP_SET_CURRENT_CHARACTER_CONTROLLER_OFF,
        .name = "SetCurrentCharacterControllerDisabled",
        .support = OpcodeSupport::k_supported,
        .provisional = true,
        .notes =
            "sets the current actor controller boolean false; its gameplay label is unresolved",
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
    AreaOpcodeInfo{.opcode = K_OP_ATTACH_AREA_SCENE,
        .name = "AttachAreaScene",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "sets AREA->SCENE mapping; a resident target replaces/materializes the attached "
                 "SCENE immediately, while a nonresident target is deferred until AREA load",
        .operands = K_OPERANDS_4E.data(),
        .operand_count = K_OPERANDS_4E.size()},
    AreaOpcodeInfo{.opcode = K_OP_PLACE_CURRENT_CHARACTER_AT_ADDRESS,
        .name = "PlaceCurrentCharacterAtAddress",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "places the established controlled character at a resident AREA address",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_SET_ADDRESS_FLAG,
        .name = "SetAddressFlag",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "sets one persistent ADDRESSES bit without waiting",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
    AreaOpcodeInfo{.opcode = K_OP_CLEAR_ADDRESS_FLAG,
        .name = "ClearAddressFlag",
        .support = OpcodeSupport::k_supported,
        .provisional = false,
        .notes = "clears one persistent ADDRESSES bit without waiting",
        .operands = K_OPERANDS_I16.data(),
        .operand_count = K_OPERANDS_I16.size()},
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
    : m_script_storage{script_bytes.begin(), script_bytes.end()},
      m_script{m_script_storage} {}

void AreaScriptRuntime::queue_event(const std::uint16_t event) {
  if (event == 2U && (m_active_event == event ||
                         std::ranges::find(m_queued_events, event) != m_queued_events.end())) {
    return;
  }
  if (m_queued_events.size() >= k_event_queue_capacity) {
    App::Log::warn(LogCategory::Script,
        "AREA event {} discarded — pending queue reached recovered capacity {}",
        event,
        k_event_queue_capacity);
    return;
  }
  m_queued_events.push_back(event);
}

std::expected<void, std::string> AreaScriptRuntime::set_event_entries(
    const AreaScriptEventEntries entries) {
  for (const std::optional<std::size_t> entry : {entries.event1, entries.event2, entries.event3}) {
    if (entry.has_value() && entry.value() >= m_script.size()) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("AREA event entry {:#x} is outside {:#x}-byte execution storage",
              entry.value(),
              m_script.size())};
    }
  }
  m_event_entries = entries;
  return {};
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

void AreaScriptRuntime::set_object_placement_state_sink(ObjectPlacementStateSink sink) {
  m_object_placement_state_sink = std::move(sink);
}

void AreaScriptRuntime::set_object_activation_sink(ObjectActivationSink sink) {
  m_object_activation_sink = std::move(sink);
}

void AreaScriptRuntime::set_area_transition_sink(AreaTransitionSink sink) {
  m_area_transition_sink = std::move(sink);
}

void AreaScriptRuntime::set_area_release_sink(AreaReleaseSink sink) {
  m_area_release_sink = std::move(sink);
}

void AreaScriptRuntime::set_area_scene_attach_sink(AreaSceneAttachSink sink) {
  m_area_scene_attach_sink = std::move(sink);
}

void AreaScriptRuntime::set_area_address_placement_sink(AreaAddressPlacementSink sink) {
  m_area_address_placement_sink = std::move(sink);
}

void AreaScriptRuntime::set_address_flag_sink(AddressFlagSink sink) {
  m_address_flag_sink = std::move(sink);
}

void AreaScriptRuntime::set_zone_activation_sink(ZoneActivationSink sink) {
  m_zone_activation_sink = std::move(sink);
}

void AreaScriptRuntime::set_persistent_object_collection_sink(PersistentObjectCollectionSink sink) {
  m_persistent_object_collection_sink = std::move(sink);
}

void AreaScriptRuntime::set_global_variable_read_sink(GlobalVariableReadSink sink) {
  m_global_variable_read_sink = std::move(sink);
}

void AreaScriptRuntime::set_global_variable_write_sink(GlobalVariableWriteSink sink) {
  m_global_variable_write_sink = std::move(sink);
}

void AreaScriptRuntime::set_global_variable_snapshot_sink(GlobalVariableSnapshotSink sink) {
  m_global_variable_snapshot_sink = std::move(sink);
}

void AreaScriptRuntime::set_character_value_read_sink(CharacterValueReadSink sink) {
  m_character_value_read_sink = std::move(sink);
}

void AreaScriptRuntime::set_character_value_write_sink(CharacterValueWriteSink sink) {
  m_character_value_write_sink = std::move(sink);
}

void AreaScriptRuntime::set_scalar16_parameters(const std::span<const std::int16_t> parameters) {
  m_scalar16_parameters.assign(parameters.begin(), parameters.end());
}

void AreaScriptRuntime::set_character_activation_sink(CharacterActivationSink sink) {
  m_character_activation_sink = std::move(sink);
}

void AreaScriptRuntime::set_character_selection_sink(CharacterSelectionSink sink) {
  m_character_selection_sink = std::move(sink);
}

void AreaScriptRuntime::set_character_deactivation_sink(CharacterDeactivationSink sink) {
  m_character_deactivation_sink = std::move(sink);
}

void AreaScriptRuntime::set_current_character_move_sink(CurrentCharacterMoveSink sink) {
  m_current_character_move_sink = std::move(sink);
}

void AreaScriptRuntime::set_current_character_controller_sink(CurrentCharacterControllerSink sink) {
  m_current_character_controller_sink = std::move(sink);
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
    auto written{
        write_global_variable(m_wait.interface_result_variable.value(), completion.result)};
    if (!written) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("cannot write interface result to global variable {}: {}",
              m_wait.interface_result_variable.value(),
              written.error())};
    }
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

std::expected<void, std::string> AreaScriptRuntime::complete_camera_wait(
    const AreaCameraOperationHandle handle) {
  if (m_state != AreaScriptState::k_waiting) {
    return std::expected<void, std::string>{std::unexpect, "area script is not waiting"};
  }
  if (m_wait.kind != AreaWaitKind::k_camera) {
    return std::expected<void, std::string>{
        std::unexpect, "area script is not waiting on a camera operation"};
  }
  if (!m_wait.camera_operation.has_value() || m_wait.camera_operation.value() != handle) {
    return std::expected<void, std::string>{
        std::unexpect, "camera completion does not match the waiting operation"};
  }

  m_wait = AreaWaitState{};
  m_wait_state = 0;
  m_state = AreaScriptState::k_running;
  App::Log::debug(LogCategory::Script,
      "area script resumed after camera operation generation {} (Runtime state 7 -> 1) at +{:#x}",
      handle.generation,
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
  auto value{read_global_variable(id)};
  if (!value) {
    return std::nullopt;
  }
  return value.value();
}

std::span<const std::int32_t> AreaScriptRuntime::global_variables() const {
  if (!m_global_variable_snapshot_sink) {
    return {};
  }
  return m_global_variable_snapshot_sink();
}

AreaScriptState AreaScriptRuntime::run(const float /*real_delta_seconds*/) {
  APP_PROFILE_FUNCTION();

  m_last_run_yielded = false;

  if (!m_active) {
    return m_state;
  }

  // Runtime state 7 is released only by the presentation-owned completion for
  // the exact mode-12 operation submitted by 0x60. The camera controller owns
  // the sole transition clock; run() must never reproduce that duration here.
  if (m_state != AreaScriptState::k_ready && m_state != AreaScriptState::k_running) {
    return m_state;
  }

  if (m_state == AreaScriptState::k_ready) {
    while (!m_queued_events.empty()) {
      const std::uint16_t event{m_queued_events.front()};
      m_queued_events.pop_front();
      std::optional<std::size_t> entry;
      switch (event) {
        case 1U:
          entry = m_event_entries.event1;
          break;
        case 2U:
          entry = m_event_entries.event2;
          break;
        case 3U:
          entry = m_event_entries.event3;
          break;
        default:
          break;
      }
      if (!entry.has_value()) {
        continue;  // A missing event entry is a harmless no-bytecode event.
      }
      m_active_event = event;
      m_ip = entry.value();
      m_evaluation_stack.clear();
      m_state = AreaScriptState::k_running;
      break;
    }
    if (m_state == AreaScriptState::k_ready) {
      return m_state;
    }
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

std::expected<std::int16_t, std::string> AreaScriptRuntime::resolve_scalar16(
    const std::int32_t operand, const std::string_view semantic) const {
  const std::int16_t serialized{static_cast<std::int16_t>(operand)};
  const std::uint16_t bits{static_cast<std::uint16_t>(serialized)};
  if (serialized == -1 || (bits & K_SCALAR16_PARAMETER_REFERENCE) == 0U) {
    return serialized;
  }
  const std::size_t parameter_index{
      static_cast<std::size_t>(bits & K_SCALAR16_PARAMETER_INDEX_MASK)};
  if (m_scalar16_parameters.empty()) {
    return std::expected<std::int16_t, std::string>{std::unexpect,
        fmt::format("{} parameter-indirected Scalar16 {:#06x} is unsupported because the AREA "
                    "parameter block is not modeled",
            semantic,
            bits)};
  }
  if (parameter_index >= m_scalar16_parameters.size()) {
    return std::expected<std::int16_t, std::string>{std::unexpect,
        fmt::format("{} Scalar16 parameter index {} is outside the {}-value parameter block",
            semantic,
            parameter_index,
            m_scalar16_parameters.size())};
  }
  return m_scalar16_parameters.at(parameter_index);
}

std::expected<std::int32_t, std::string> AreaScriptRuntime::read_global_variable(
    const std::uint16_t id) const {
  if (!m_global_variable_read_sink) {
    return std::expected<std::int32_t, std::string>{
        std::unexpect, "shared global-variable read bridge is not wired"};
  }
  return m_global_variable_read_sink(id);
}

std::expected<void, std::string> AreaScriptRuntime::write_global_variable(
    const std::uint16_t id, const std::int32_t value) {
  if (!m_global_variable_write_sink) {
    return std::expected<void, std::string>{
        std::unexpect, "shared global-variable write bridge is not wired"};
  }
  return m_global_variable_write_sink(id, value);
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
  const auto fail_instruction = [&](std::string reason) {
    m_pause_info = AreaPauseInfo{.offset = instruction_offset,
        .opcode = opcode,
        .opcode_name = std::string{info->name},
        .reason_text = std::move(reason),
        .nearby_bytes = nearby_bytes_hex(instruction_offset)};
    m_state = AreaScriptState::k_failed;
  };

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
      auto value{read_global_variable(id)};
      if (!value) {
        fail_instruction(fmt::format("cannot read global variable {}: {}", id, value.error()));
        return;
      }
      m_evaluation_stack.push_back(value.value());
      entry.effect = fmt::format("push global variable {} ({})", id, value.value());
      break;
    }
    case K_OP_SET_GLOBAL_VARIABLE_ZERO: {
      auto variable_id{resolve_scalar16(operands.at(0), "SetGlobalVariableZero variable")};
      if (!variable_id) {
        fail_instruction(variable_id.error());
        return;
      }
      const std::uint16_t id{static_cast<std::uint16_t>(variable_id.value())};
      const std::optional<std::int32_t> old_value{variable(id)};
      if (auto written{write_global_variable(id, 0)}; !written) {
        fail_instruction(fmt::format("cannot zero global variable {}: {}", id, written.error()));
        return;
      }
      entry.effect = old_value.has_value()
                         ? fmt::format("set global variable {} from {} to 0", id, old_value.value())
                         : fmt::format("set global variable {} to 0", id);
      break;
    }
    case K_OP_SET_GLOBAL_VARIABLE_ONE: {
      const std::uint16_t id{static_cast<std::uint16_t>(operands.at(0))};
      if (auto written{write_global_variable(id, 1)}; !written) {
        fail_instruction(
            fmt::format("cannot set global variable {} to one: {}", id, written.error()));
        return;
      }
      entry.effect = fmt::format("set global variable {} to 1", id);
      break;
    }
    case K_OP_SET_GLOBAL_VARIABLE: {
      const std::uint16_t id{static_cast<std::uint16_t>(operands.at(0))};
      if (auto written{write_global_variable(id, operands.at(1))}; !written) {
        fail_instruction(fmt::format("cannot set global variable {}: {}", id, written.error()));
        return;
      }
      entry.effect = fmt::format("set global variable {} to {}", id, operands.at(1));
      break;
    }
    case K_OP_ADD_STACK_TO_GLOBAL_VARIABLE:
    case K_OP_SUBTRACT_STACK_FROM_GLOBAL_VARIABLE:
    case K_OP_MULTIPLY_GLOBAL_VARIABLE_BY_STACK:
    case K_OP_DIVIDE_GLOBAL_VARIABLE_BY_STACK:
    case K_OP_AND_GLOBAL_VARIABLE_WITH_STACK:
    case K_OP_OR_GLOBAL_VARIABLE_WITH_STACK: {
      const std::string_view operation{info->name};
      auto variable_id{resolve_scalar16(operands.at(0), operation)};
      if (!variable_id) {
        fail_instruction(variable_id.error());
        return;
      }
      const std::uint16_t id{static_cast<std::uint16_t>(variable_id.value())};
      auto lhs{read_global_variable(id)};
      if (!lhs) {
        fail_instruction(fmt::format("cannot read global variable {}: {}", id, lhs.error()));
        return;
      }
      auto rhs{pop_evaluation_value()};
      if (!rhs) {
        fail_instruction(rhs.error());
        return;
      }

      std::int32_t result{0};
      const char* operator_text{""};
      switch (opcode) {
        case K_OP_ADD_STACK_TO_GLOBAL_VARIABLE:
          result = wrapping_add(lhs.value(), rhs.value());
          operator_text = "+";
          break;
        case K_OP_SUBTRACT_STACK_FROM_GLOBAL_VARIABLE:
          result = wrapping_subtract(lhs.value(), rhs.value());
          operator_text = "-";
          break;
        case K_OP_MULTIPLY_GLOBAL_VARIABLE_BY_STACK:
          result = wrapping_multiply(lhs.value(), rhs.value());
          operator_text = "*";
          break;
        case K_OP_DIVIDE_GLOBAL_VARIABLE_BY_STACK:
          if (rhs.value() == 0) {
            fail_instruction("DivideGlobalVariableByStack: division by zero");
            return;
          }
          if (lhs.value() == std::numeric_limits<std::int32_t>::min() && rhs.value() == -1) {
            fail_instruction("DivideGlobalVariableByStack: signed division overflow");
            return;
          }
          result = lhs.value() / rhs.value();
          operator_text = "/";
          break;
        case K_OP_AND_GLOBAL_VARIABLE_WITH_STACK:
          result = bitwise_and(lhs.value(), rhs.value());
          operator_text = "&";
          break;
        case K_OP_OR_GLOBAL_VARIABLE_WITH_STACK:
          result = bitwise_or(lhs.value(), rhs.value());
          operator_text = "|";
          break;
        default:
          fail_instruction("internal global-stack arithmetic opcode dispatch error");
          return;
      }
      if (auto written{write_global_variable(id, result)}; !written) {
        fail_instruction(fmt::format("cannot write global variable {}: {}", id, written.error()));
        return;
      }

      entry.effect = fmt::format(
          "global {}: {} {} {} -> {}", id, lhs.value(), operator_text, rhs.value(), result);
      break;
    }
    case K_OP_GET_CHARACTER_VALUE_TO_VARIABLE:
    case K_OP_SET_CHARACTER_VALUE_FROM_VARIABLE: {
      std::array<std::int16_t, 3> resolved{};
      const bool read_character{opcode == K_OP_GET_CHARACTER_VALUE_TO_VARIABLE};
      const std::array<std::string_view, 3> semantics{
          read_character ? "GetCharacterValueToVariable character"
                         : "SetCharacterValueFromVariable character",
          read_character ? "GetCharacterValueToVariable kind"
                         : "SetCharacterValueFromVariable kind",
          read_character ? "GetCharacterValueToVariable destination variable"
                         : "SetCharacterValueFromVariable source variable"};
      for (std::size_t index{0}; index < resolved.size(); ++index) {
        auto value{resolve_scalar16(operands.at(index), semantics.at(index))};
        if (!value) {
          fail_instruction(value.error());
          return;
        }
        resolved.at(index) = value.value();
      }

      const AreaCharacterValueRequest request{
          .character_id = resolved.at(0), .value_kind = resolved.at(1)};
      const std::uint16_t variable_id{static_cast<std::uint16_t>(resolved.at(2))};
      if (read_character) {
        if (!m_character_value_read_sink) {
          fail_instruction("session character-value read bridge is not wired");
          return;
        }
        auto value{m_character_value_read_sink(request)};
        if (!value) {
          fail_instruction(fmt::format("cannot read character {} value kind {}: {}",
              request.character_id,
              request.value_kind,
              value.error()));
          return;
        }
        if (auto written{write_global_variable(variable_id, value.value())}; !written) {
          fail_instruction(fmt::format("cannot write character value to global variable {}: {}",
              variable_id,
              written.error()));
          return;
        }
        entry.effect = fmt::format("character {} kind {} -> global variable {} ({})",
            request.character_id,
            request.value_kind,
            variable_id,
            value.value());
        break;
      }

      auto value{read_global_variable(variable_id)};
      if (!value) {
        fail_instruction(
            fmt::format("cannot read source global variable {}: {}", variable_id, value.error()));
        return;
      }
      if (!m_character_value_write_sink) {
        fail_instruction("session character-value write bridge is not wired");
        return;
      }
      if (auto written{m_character_value_write_sink(request, value.value())}; !written) {
        fail_instruction(fmt::format("cannot set character {} value kind {}: {}",
            request.character_id,
            request.value_kind,
            written.error()));
        return;
      }
      entry.effect = fmt::format("global variable {} ({}) -> character {} kind {}",
          variable_id,
          value.value(),
          request.character_id,
          request.value_kind);
      break;
    }
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
      constexpr std::array<std::string_view, 3> k_semantics{"BeginAreaTransition target",
          "BeginAreaTransition operand_b",
          "BeginAreaTransition operand_c"};
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
            .reason_text = fmt::format("AREA transition variant ({}, {}) is not yet implemented",
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
            .reason_text = fmt::format("failed to begin AREA transition to {}: {}",
                request.target_area_id,
                accepted.error()),
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
          .area_transition_handle = accepted.value()};
      wait_after_instruction = true;
      entry.effect = fmt::format(
          "begin AREA transition to {} as generation {} and wait in "
          "Runtime state 10",
          request.target_area_id,
          accepted->generation);
      break;
    }
    case K_OP_RELEASE_AREA: {
      auto area_id{resolve_scalar16(operands.at(0), "ReleaseArea")};
      if (!area_id) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = area_id.error(),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      if (!m_area_release_sink) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = "AREA release bridge is not wired",
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      const AreaReleaseRequest request{.area_id = area_id.value()};
      m_last_area_release_request = request;
      if (auto released{m_area_release_sink(request)}; !released) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text =
                fmt::format("failed to release AREA {}: {}", request.area_id, released.error()),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      entry.effect = fmt::format("release AREA {}", request.area_id);
      break;
    }
    case K_OP_ADD_OBJECT_TO_PERSISTENT_COLLECTION: {
      auto collection_kind{
          resolve_scalar16(operands.at(0), "OBJECTS AddObjectToPersistentCollection kind")};
      auto object_id{
          resolve_scalar16(operands.at(1), "OBJECTS AddObjectToPersistentCollection object")};
      if (!collection_kind || !object_id) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = !collection_kind ? collection_kind.error() : object_id.error(),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      if (!m_persistent_object_collection_sink) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = "persistent object-collection bridge is not wired",
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      const AreaPersistentObjectCollectionRequest request{
          .collection_kind = collection_kind.value(), .object_id = object_id.value()};
      m_last_persistent_object_collection_request = request;
      if (auto added{m_persistent_object_collection_sink(request)}; !added) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = fmt::format("failed to add OBJECTS ID {} to collection {}: {}",
                request.object_id,
                request.collection_kind,
                added.error()),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      entry.effect = fmt::format("add OBJECTS ID {} to persistent collection {}",
          request.object_id,
          request.collection_kind);
      break;
    }
    case K_OP_SELECT_CURRENT_CHARACTER: {
      auto character_id{resolve_scalar16(operands.at(0), "SelectCurrentCharacter")};
      if (!character_id) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = character_id.error(),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      if (!m_character_selection_sink) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = "current-character selection bridge is not wired",
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      const AreaCharacterSelectionRequest request{.character_id = character_id.value()};
      m_last_character_selection_request = request;
      if (auto selected{m_character_selection_sink(request)}; !selected) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = fmt::format("failed to select current character {}: {}",
                request.character_id,
                selected.error()),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      entry.effect = fmt::format("select current character {}", request.character_id);
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
            .area_transition_handle = std::nullopt};
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
    case K_OP_START_CURRENT_CHARACTER_SCRIPT_TRACKED:
    case K_OP_START_CHARACTER_SCRIPT:
    case K_OP_START_CHARACTER_SCRIPT_TRACKED:
    case K_OP_START_CURRENT_CHARACTER_SCRIPT: {
      if (!m_character_script_sink) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = "character-script bridge is not wired",
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }

      const bool current_target{opcode == K_OP_START_CURRENT_CHARACTER_SCRIPT_TRACKED ||
                                opcode == K_OP_START_CURRENT_CHARACTER_SCRIPT};
      const bool tracked{opcode == K_OP_START_CURRENT_CHARACTER_SCRIPT_TRACKED ||
                         opcode == K_OP_START_CHARACTER_SCRIPT_TRACKED};
      const std::size_t camera_duration_operand{current_target ? 1U : 2U};
      auto camera_duration{resolve_scalar16(
          operands.at(camera_duration_operand), "character-script camera duration")};
      if (!camera_duration) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = camera_duration.error(),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }

      const AreaCharacterScriptRequest request{
          .target = current_target ? AreaCharacterScriptTarget::k_current
                                   : AreaCharacterScriptTarget::k_explicit,
          .character_id = current_target ? std::nullopt
                                         : std::optional<std::int16_t>{static_cast<std::int16_t>(
                                               operands.at(0))},
          .script_id = static_cast<std::uint16_t>(operands.at(current_target ? 0U : 1U)),
          .camera_duration_units = camera_duration.value(),
          .mode = tracked ? AreaCharacterScriptLaunchMode::k_tracked
                          : AreaCharacterScriptLaunchMode::k_fire_and_forget};

      m_last_character_script_request = request;

      auto instance{m_character_script_sink(request)};
      if (!instance) {
        const std::string_view target_name{
            current_target ? "current character" : "explicit character"};
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = fmt::format("failed to request {} script {}: {}",
                target_name,
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
            .area_transition_handle = std::nullopt};
        wait_after_instruction = true;
        entry.effect = fmt::format(
            "start {} script {} camera duration {} as instance {} and wait "
            "in Runtime state 4",
            current_target ? "current character" : "explicit character",
            request.script_id,
            request.camera_duration_units,
            instance.value());
      } else {
        entry.effect = fmt::format(
            "start {} script {} camera duration {} as instance {} "
            "(fire-and-forget)",
            current_target ? "current character" : "explicit character",
            request.script_id,
            request.camera_duration_units,
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
    case K_OP_START_CURRENT_CHARACTER_MOVE: {
      auto move_id{resolve_scalar16(operands.at(0), "StartCurrentCharacterMove")};
      if (!move_id) {
        fail_instruction(move_id.error());
        return;
      }
      const AreaCurrentCharacterMoveRequest request{.move_id = move_id.value()};
      m_last_current_character_move_request = request;
      if (m_current_character_move_sink) {
        if (auto selected{m_current_character_move_sink(request)}; !selected) {
          fail_instruction(fmt::format(
              "failed to select current-character move {}: {}", request.move_id, selected.error()));
          return;
        }
      }
      entry.effect =
          fmt::format("select current-character move/control record {}", request.move_id);
      break;
    }
    case K_OP_ACTIVATE_ZONE:
    case K_OP_DEACTIVATE_ZONE: {
      const std::string_view operation{
          opcode == K_OP_ACTIVATE_ZONE ? "ActivateZone" : "DeactivateZone"};
      auto zone_id{resolve_scalar16(operands.at(0), operation)};
      if (!zone_id) {
        fail_instruction(zone_id.error());
        return;
      }
      if (!m_zone_activation_sink) {
        fail_instruction("zone activation bridge is not wired");
        return;
      }
      const AreaZoneActivationRequest request{
          .zone_id = zone_id.value(), .enabled = opcode == K_OP_ACTIVATE_ZONE};
      m_last_zone_activation_request = request;
      if (auto updated{m_zone_activation_sink(request)}; !updated) {
        fail_instruction(fmt::format("failed to {} ZONE {}: {}",
            request.enabled ? "activate" : "deactivate",
            static_cast<std::uint16_t>(request.zone_id),
            updated.error()));
        return;
      }
      entry.effect = fmt::format("{} ZONE {}",
          request.enabled ? "activate" : "deactivate",
          static_cast<std::uint16_t>(request.zone_id));
      break;
    }

    case K_OP_ENABLE_OBJECT_PLACEMENT:
    case K_OP_DISABLE_OBJECT_PLACEMENT: {
      const bool enabled{opcode == K_OP_ENABLE_OBJECT_PLACEMENT};
      const std::string_view operation{
          enabled ? "EnableObjectPlacement" : "DisableObjectPlacement"};
      auto object_id{resolve_scalar16(operands.at(0), operation)};
      if (!object_id) {
        fail_instruction(object_id.error());
        return;
      }
      if (!m_object_placement_state_sink) {
        fail_instruction("object-placement state bridge is not wired");
        return;
      }
      const AreaObjectPlacementStateRequest request{
          .object_id = object_id.value(), .enabled = enabled};
      m_last_object_placement_state_request = request;
      if (auto updated{m_object_placement_state_sink(request)}; !updated) {
        fail_instruction(fmt::format("failed to {} object placement {}: {}",
            enabled ? "enable" : "disable",
            request.object_id,
            updated.error()));
        return;
      }
      entry.effect =
          fmt::format("{} object placement {}", enabled ? "enable" : "disable", request.object_id);
      break;
    }
    case K_OP_ACTIVATE_CHARACTER: {
      // A normal character is reactivated through the compact context's
      // authored AREA/SCENE data. The -1 form acts on the durable selected
      // body and changes presentation only. Neither form waits or yields.
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
                         ? "enable current-character presentation"
                         : fmt::format("activate character {}{}",
                               request.character_id,
                               request.apply_area_transform ? " at AREA transform" : "");
      break;
    }
    case K_OP_DEACTIVATE_CHARACTER: {
      auto character_id{resolve_scalar16(operands.at(0), "DeactivateCharacter")};
      if (!character_id) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = character_id.error(),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      if (!m_character_deactivation_sink) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = "character deactivation bridge is not wired",
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      const AreaCharacterDeactivationRequest request{.character_id = character_id.value()};
      m_last_character_deactivation_request = request;
      if (auto deactivated{m_character_deactivation_sink(request)}; !deactivated) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = fmt::format(
                "failed to deactivate character {}: {}", request.character_id, deactivated.error()),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      entry.effect = request.character_id == -1
                         ? "disable current-character presentation"
                         : fmt::format("deactivate character {}", request.character_id);
      break;
    }
    case K_OP_OBJECT_ACTIVATE: {
      auto object_id{resolve_scalar16(operands.at(0), "ObjectActivate OBJECTS ID")};
      if (!object_id) {
        fail_instruction(object_id.error());
        return;
      }
      if (!m_object_activation_sink) {
        fail_instruction("OBJECTS activation bridge is not wired");
        return;
      }
      const AreaObjectActivationRequest request{.object_id = object_id.value()};
      m_last_object_activation_request = request;
      if (auto activated{m_object_activation_sink(request)}; !activated) {
        fail_instruction(fmt::format(
            "OBJECTS ID {} activation failed: {}", request.object_id, activated.error()));
        return;
      }
      // The recovered handler marks the ordinary compact dispatcher yield
      // flag but does not install a typed wait.  The next scenario tick
      // resumes at the instruction following ObjectActivate.
      m_yield_requested = true;
      entry.effect = request.object_id == -1
                         ? "OBJECTS activation skipped for -1"
                         : fmt::format("activate OBJECTS ID {}", request.object_id);
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
    case K_OP_SET_CURRENT_CHARACTER_CONTROLLER_ON:
    case K_OP_SET_CURRENT_CHARACTER_CONTROLLER_OFF: {
      const AreaCurrentCharacterControllerRequest request{
          .enabled = opcode == K_OP_SET_CURRENT_CHARACTER_CONTROLLER_ON};
      m_last_current_character_controller_request = request;
      if (m_current_character_controller_sink) {
        if (auto updated{m_current_character_controller_sink(request)}; !updated) {
          fail_instruction(fmt::format("failed to set current-character controller {}: {}",
              request.enabled ? "enabled" : "disabled",
              updated.error()));
          return;
        }
      }
      entry.effect = fmt::format(
          "set current-character controller {}", request.enabled ? "enabled" : "disabled");
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
          .area_transition_handle = std::nullopt};
      wait_after_instruction = true;
      entry.effect = fmt::format(
          "open interface {} (operand {}, result variable {})", interface_id, operand_b, operand_c);
      break;
    }
    case K_OP_ATTACH_AREA_SCENE: {
      auto area_id{resolve_scalar16(operands.at(0), "AttachAreaScene area")};
      auto scene_id{resolve_scalar16(operands.at(1), "AttachAreaScene scene")};
      if (!area_id || !scene_id) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = !area_id ? area_id.error() : scene_id.error(),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      if (!m_area_scene_attach_sink) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = "AREA SCENE-attach bridge is not wired",
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      const AreaSceneAttachRequest request{
          .area_id = area_id.value(), .scene_id = scene_id.value()};
      m_last_area_scene_attach_request = request;
      if (auto attached{m_area_scene_attach_sink(request)}; !attached) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = fmt::format("failed to attach SCENE {} to AREA {}: {}",
                request.scene_id,
                request.area_id,
                attached.error()),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      entry.effect = fmt::format("attach SCENE {} to AREA {}", request.scene_id, request.area_id);
      break;
    }
    case K_OP_PLACE_CURRENT_CHARACTER_AT_ADDRESS: {
      auto address_id{resolve_scalar16(operands.at(0), "PlaceCurrentCharacterAtAddress")};
      if (!address_id) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = address_id.error(),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      if (!m_area_address_placement_sink) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = "AREA address-placement bridge is not wired",
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      const AreaAddressPlacementRequest request{.address_id = address_id.value()};
      m_last_area_address_placement_request = request;
      if (auto placed{m_area_address_placement_sink(request)}; !placed) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = fmt::format("failed to place current character at address {}: {}",
                request.address_id,
                placed.error()),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      entry.effect = fmt::format("place current character at address {}", request.address_id);
      break;
    }
    case K_OP_SET_ADDRESS_FLAG:
    case K_OP_CLEAR_ADDRESS_FLAG: {
      auto address_id{resolve_scalar16(operands.at(0), "ADDRESSES address")};
      if (!address_id) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = address_id.error(),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      if (!m_address_flag_sink) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = "persistent ADDRESSES bridge is not wired",
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      const AreaAddressFlagRequest request{
          .address_id = address_id.value(), .enabled = opcode == K_OP_SET_ADDRESS_FLAG};
      m_last_address_flag_request = request;
      if (auto updated{m_address_flag_sink(request)}; !updated) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = fmt::format("failed to {} ADDRESSES ID {}: {}",
                request.enabled ? "set" : "clear",
                request.address_id,
                updated.error()),
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      entry.effect =
          fmt::format("{} ADDRESSES ID {}", request.enabled ? "set" : "clear", request.address_id);
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
      std::optional<AreaCameraOperationHandle> operation;
      if (m_camera_sink) {
        auto submitted{m_camera_sink(m_last_camera_request.value())};
        if (submitted) {
          operation = submitted.value();
        } else if (wait) {
          m_pause_info = AreaPauseInfo{.offset = instruction_offset,
              .opcode = opcode,
              .opcode_name = std::string{info->name},
              .reason_text = fmt::format(
                  "failed to start tracked camera {}: {}",
                  m_last_camera_request->camera_id,
                  submitted.error()),
              .nearby_bytes = nearby_bytes_hex(instruction_offset)};
          m_state = AreaScriptState::k_failed;
          return;
        }
      } else if (wait) {
        m_pause_info = AreaPauseInfo{.offset = instruction_offset,
            .opcode = opcode,
            .opcode_name = std::string{info->name},
            .reason_text = "tracked camera completion bridge is not wired",
            .nearby_bytes = nearby_bytes_hex(instruction_offset)};
        m_state = AreaScriptState::k_failed;
        return;
      }
      entry.effect = fmt::format("camera {} duration={} flags={}{}",
          m_last_camera_request->camera_id,
          duration,
          m_last_camera_request->flags,
          wait && operation.has_value() ? " and wait" : "");
      if (wait && operation.has_value()) {
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
            .camera_operation = operation};
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
          .duration_units = static_cast<std::int16_t>(operands.at(1)),
          .delay_units = static_cast<std::int16_t>(operands.at(2))};
      if (m_presentation_sink) {
        m_presentation_sink(m_last_presentation_request.value());
      }
      entry.effect = fmt::format("presentation mode={} color={:#010x} args=({}, {})",
          mode,
          m_last_presentation_request->color,
          m_last_presentation_request->duration_units,
          m_last_presentation_request->delay_units);
      // Runtime's all-zero mode-1 bootstrap command is a no-op and continues
      // immediately. Mode 2 and non-empty mode-1 effects set the central
      // dispatcher-yield flag.
      m_yield_requested = opcode == K_OP_PRESENTATION_EFFECT_ALT ||
                          m_last_presentation_request->color != 0U ||
                          m_last_presentation_request->duration_units != 0 ||
                          m_last_presentation_request->delay_units != 0;
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
    if (m_wait.kind == AreaWaitKind::k_character_script && m_wait.character_script.has_value() &&
        m_wait.character_script_instance.has_value()) {
      App::Log::debug(LogCategory::Script,
          "tracked character-script wait entered — compactIp=+{:#x} target={} script={} "
          "instance={}",
          m_ip,
          m_wait.character_script->character_id.value_or(-1),
          m_wait.character_script->script_id,
          m_wait.character_script_instance.value());
    }
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

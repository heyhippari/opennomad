#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Core/Script/ScriptOpcode.hpp"

namespace App::Script {

/// Encoded operand width of an AREA bytecode opcode. Operands are read
/// little-endian and sign-extended into 32-bit values.
enum class AreaOperandWidth : std::uint8_t {
  k_int8,
  k_int16,
  k_int32,
};

/// Metadata of one AREA bytecode opcode. The area bytecode is a raw stream
/// (opcode byte followed by inline operands), distinct from the SCX
/// DEAD0002 command format; its lengths come from the recovered handler
/// decoders, never from the SCX dispatch-table metadata.
struct AreaOpcodeInfo {
  std::uint32_t opcode{0};
  std::string_view name;
  OpcodeSupport support{OpcodeSupport::k_unsupported};
  /// True when the complete downstream subsystem is not yet implemented and
  /// the handler is an explicitly provisional compatibility action.
  bool provisional{false};
  std::string_view notes{};
  const AreaOperandWidth* operands{nullptr};
  std::size_t operand_count{0};
};

/// The single source of truth for AREA bytecode opcode knowledge. Returns
/// nullptr for unknown opcodes.
[[nodiscard]] const AreaOpcodeInfo* area_opcode_info(std::uint32_t opcode);

/// Human-readable AREA opcode name, or nullptr when unknown.
[[nodiscard]] const char* area_opcode_name(std::uint32_t opcode);

}  // namespace App::Script

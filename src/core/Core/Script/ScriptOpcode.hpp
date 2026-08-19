#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace App::Script {

/// Semantic parameter types from Runtime's hardcoded opcode parameter lookup
/// (0x0044C090). Used to label arguments in the debugger and to locate
/// sprite-owning arguments during instance creation.
inline constexpr std::uint16_t k_semantic_none{0};
inline constexpr std::uint16_t k_semantic_unknown_7{7};
inline constexpr std::uint16_t k_semantic_unknown_8{8};
inline constexpr std::uint16_t k_semantic_sprite{9};
inline constexpr std::uint16_t k_semantic_xyz_pointer{0x0E};
inline constexpr std::uint16_t k_semantic_duration{0x10};
inline constexpr std::uint16_t k_semantic_progress_elapsed{0x11};
inline constexpr std::uint16_t k_semantic_initial_scale{0x15};
inline constexpr std::uint16_t k_semantic_target_scale{0x16};
inline constexpr std::uint16_t k_semantic_initial_roll{0x17};
inline constexpr std::uint16_t k_semantic_target_roll{0x18};
inline constexpr std::uint16_t k_semantic_frame{0x1F};

/// Support status of one native opcode in the OpenNomad dispatcher.
enum class OpcodeSupport : std::uint8_t {
  k_supported,    ///< Implemented from confirmed evidence.
  k_unsupported,  ///< Known but not implemented; causes a controlled pause.
};

/// One semantic parameter mapping of an opcode (type -> argument index).
struct OpcodeSemanticParam {
  std::uint16_t semantic_type{k_semantic_none};
  std::uint32_t argument_index{0};
};

/// Metadata of one native opcode. This table is the single source of truth
/// driving dispatch and the debugger labels; do not duplicate it elsewhere.
struct OpcodeInfo {
  std::uint32_t opcode{0};
  std::string_view name;
  /// Expected argument count when confirmed; 0 means unconfirmed.
  std::uint32_t expected_argument_count{0};
  const OpcodeSemanticParam* semantic_params{nullptr};
  std::size_t semantic_param_count{0};
  /// True when the opcode owns/clones a sprite during instance creation.
  bool owns_sprite{false};
  OpcodeSupport support{OpcodeSupport::k_unsupported};
  std::string_view notes{};
};

/// The single source of truth for opcode knowledge. Returns nullptr for
/// unknown opcodes.
[[nodiscard]] const OpcodeInfo* opcode_info(std::uint32_t opcode);

/// Human-readable opcode name, or nullptr when unknown.
[[nodiscard]] const char* opcode_name(std::uint32_t opcode);

/// True when the opcode owns/clones a sprite during instance creation.
[[nodiscard]] bool opcode_owns_sprite(std::uint32_t opcode);

}  // namespace App::Script

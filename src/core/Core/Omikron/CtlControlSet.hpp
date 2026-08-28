#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Omikron/Animation3DA.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Omikron {

/// Serialized CTL magic: bytes "CE70" read as a little-endian u32.
inline constexpr std::uint32_t K_CTL_MAGIC{0x30374543U};
/// Fixed CTL header size; the first top-level move record starts here.
inline constexpr std::size_t K_CTL_HEADER_SIZE{0x58U};
/// Fixed top-level move record size.
inline constexpr std::size_t K_CTL_MOVE_SIZE{0x20U};
/// Fixed child-state record size.
inline constexpr std::size_t K_CTL_STATE_SIZE{0x58U};
/// Fixed animation audio-marker record size (animation_mode & 0x08 states).
inline constexpr std::size_t K_CTL_MARKER_SIZE{0x20U};

/// One typed 0x20-byte animation-linked audio marker. Only the ordinary
/// one-shot locomotion mode (one_shot_phase + one_shot_sound_hid) is consumed
/// by the Phase 4.1 controller; the remaining fields are preserved neutrally.
struct CtlAudioMarker {
  float sync_duration{0.0F};
  float active_start{0.0F};
  float active_end{0.0F};
  float one_shot_phase{0.0F};
  std::uint32_t sound_property_raw{0};
  std::uint16_t synced_sound_id{0};
  std::uint16_t one_shot_sound_hid{0};
  std::uint8_t attachment_selector{0};
  std::uint8_t marker_flags{0};
  std::uint16_t raw_1a{0};
  float scalar_1c{0.0F};
};

/// Typed 0x18-byte auxiliary block consumed by states whose flags intersect
/// 0x00000100 | 0x00000040. Only the authored orientation delta at +0x08 has
/// confirmed semantics; the surrounding floats remain neutral.
struct CtlOrientationBlock {
  float raw_00{0.0F};
  float raw_04{0.0F};
  Runtime::Vec3 orientation_delta{};
  float raw_14{0.0F};
};

/// Typed 0x14-byte auxiliary block consumed by states whose flags intersect
/// 0x00000200 | 0x00000080. The local movement delta at +0x08 is transformed
/// through the live actor orientation before it reaches the candidate
/// character position; the two leading floats remain neutral.
struct CtlMovementBlock {
  float raw_00{0.0F};
  float raw_04{0.0F};
  Runtime::Vec3 local_delta{};
};

struct CtlMove;

/// One typed 0x58-byte CTL child state plus its parsed variable sections.
/// Pointer-era serialized fields are replaced by resolved ownership: child
/// and parent references hold direct state pointers, the owner move derives
/// from serialized containment, and the animation pointer refers to the
/// bank's deduplicated embedded 3DA resources.
struct CtlState {
  std::uint32_t state_id{0};
  std::uint32_t input_condition{0};
  std::uint32_t flags{0};
  /// Unresolved serialized field at +0x0C; preserved without semantics.
  std::uint32_t raw_0c{0};
  float window_start{0.0F};
  float window_end{0.0F};
  float transition_value{0.0F};
  /// Unresolved serialized fields at +0x34/+0x3C; preserved without semantics.
  std::uint32_t raw_34{0};
  std::uint32_t raw_3c{0};
  std::uint16_t animation_mode{0};
  /// Animation transition/blend parameter at +0x4E; NOT an animation length.
  std::uint16_t transition_count{0};
  std::uint16_t phase_offset{0};
  std::uint16_t defer_ticks{0};
  std::uint16_t priority{0};

  /// Original 12-byte animation key spelling (empty when the state has none).
  std::string animation_key;
  /// _strupr-equivalent uppercase canonical key used for resource dedup.
  std::string canonical_animation_key;
  /// Deduplicated embedded 3DA resource shared by every state with the same
  /// canonical key; null for key-less states.
  const Animation3DA* animation{nullptr};

  std::optional<CtlOrientationBlock> orientation_block;
  std::optional<CtlMovementBlock> movement_block;
  /// Deferred side-effect routine name (flags & 0x10); never a movement name.
  std::string callback_name;
  /// Neutral 0x28-byte auxiliary block (flags & 0x02000000).
  std::optional<std::array<std::byte, 0x28U>> auxiliary_block_28;
  std::vector<CtlAudioMarker> audio_markers;

  /// Resolved by authored state ID after the complete state array is known.
  std::vector<const CtlState*> child_refs;
  std::vector<const CtlState*> parent_refs;
  /// Serialized goto target (+0x28); null when the authored ID is zero.
  const CtlState* goto_state{nullptr};
  /// Owning top-level move, derived from serialized containment.
  const CtlMove* owner_move{nullptr};
};

/// One typed 0x20-byte top-level CTL move record. `states` lists the move's
/// child states in serialized authored order.
struct CtlMove {
  std::uint32_t move_id{0};
  std::uint32_t flags{0};
  /// Unresolved serialized/runtime-pointer-era fields; preserved neutral.
  std::uint32_t raw_0c{0};
  std::uint32_t raw_10{0};
  std::string name;
  std::vector<const CtlState*> states;
};

/// Immutable parsed CTL character-control/state-machine resource (Runtime
/// loader family 0x0045D970). Instances are shared by every controller using
/// the same authored control set; all mutable control state lives in
/// Character::CtlController.
class CtlControlSet {
 public:
  /// Parses one complete CTL resource. The input is consumed exactly: a
  /// correct generic parse reaches EOF after the embedded 3DA payloads.
  /// Malformed/truncated sections and unresolved state references are
  /// structured errors, never undefined behavior.
  [[nodiscard]] static std::expected<CtlControlSet, std::string> load(
      std::span<const std::byte> data);

  [[nodiscard]] std::uint32_t format_version() const {
    return m_format_version;
  }
  [[nodiscard]] std::span<const CtlMove> moves() const {
    return m_moves;
  }
  [[nodiscard]] std::span<const CtlState> states() const {
    return m_states;
  }
  [[nodiscard]] std::size_t embedded_animation_count() const {
    return m_animations.size();
  }

  /// Exact authored move-ID lookup (Runtime 0x0046ACE0); IDs are authored,
  /// not array indices.
  [[nodiscard]] const CtlMove* move_by_id(std::uint32_t move_id) const;
  /// Exact authored state-ID lookup.
  [[nodiscard]] const CtlState* state_by_id(std::uint32_t state_id) const;

  /// Runtime 0x0046AD90: first move in authored order with flags & 0x1.
  [[nodiscard]] const CtlMove* default_move() const;
  /// Runtime 0x0047DD40: first state of `move` in authored order with
  /// flags & 0x20.
  [[nodiscard]] static const CtlState* default_state(const CtlMove& move);

 private:
  std::uint32_t m_format_version{0};
  /// Unresolved header field at +0x08; preserved without semantics.
  std::uint32_t m_raw_08{0};
  /// Unresolved/pointer-era header fields at +0x10..+0x57, kept for
  /// diagnostics only.
  std::array<std::byte, 0x48U> m_reserved_header{};

  std::vector<CtlMove> m_moves;
  std::vector<CtlState> m_states;
  /// One payload per first occurrence of a unique canonical animation key,
  /// in serialized child order.
  std::vector<Animation3DA> m_animations;
  std::unordered_map<std::uint32_t, std::size_t> m_move_indices;
  std::unordered_map<std::uint32_t, std::size_t> m_state_indices;
};

}  // namespace App::Omikron

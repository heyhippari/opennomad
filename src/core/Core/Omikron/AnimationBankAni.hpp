#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "Core/Omikron/Animation3DA.hpp"

namespace App::Omikron {

/// Animation kinds used by the passerby crowd (ANI node +0x00).
enum class AniAnimationKind : std::int32_t {
  k_walk = 9,
  k_blocked = 11,
  /// Transition into a named activity group.
  k_activity_begin = 14,
  /// Transition out of a named activity group.
  k_activity_end = 16,
};

/// One 0x24-byte ANI animation node.
struct AniAnimation {
  std::int32_t kind{0};  ///< +0x00, see AniAnimationKind.
  std::int32_t id{0};    ///< +0x04, unique within its character entry.
  /// Index into AnimationBankAni::animations of the embedded 3DA (+0x08).
  std::size_t animation_index{0};
  /// +0x0C/+0x10 footstep progress marks; the sign selects the step sound.
  std::array<float, 2> footstep_progress{};
  std::array<std::int16_t, 2> footstep_sound{-1, -1};  ///< +0x14/+0x16.
  /// +0x1C, activity group name shared by an activity's begin/loop/end nodes.
  std::string group;
};

/// One 0x18-byte ANI character entry and its node list in file order.
struct AniCharacter {
  /// +0x00, matched against the IAM character type (1 male, 2 female
  /// passerby, ...). Runtime 0x00434530 searches entries by this value.
  std::int32_t character_type{0};
  std::vector<AniAnimation> nodes;
};

/// Decoded ANIMS/<AREA +0x7C>.ANI animation bank ("3.0V").
///
/// Runtime 0x00434010 relocates each entry's node list (+0x04) and each
/// node's +0x08/+0x18 file offsets, and binds +0x08 to an embedded 3DA
/// payload (0x0046E5F0) whose stream offsets are relative to that payload.
struct AnimationBankAni {
  std::vector<AniCharacter> characters;
  /// Embedded 3DA payloads, deduplicated by file offset.
  std::vector<Animation3DA> animations;

  [[nodiscard]] static std::expected<AnimationBankAni, std::string> load(
      std::span<const std::byte> data);

  /// Runtime 0x00434530: the entry whose type matches, if any.
  [[nodiscard]] const AniCharacter* character(std::int32_t character_type) const;
};

/// Runtime 0x004345E0 candidate list: every node of `kind` in file order.
/// Runtime then selects one with rand() % count.
[[nodiscard]] std::vector<const AniAnimation*> ani_nodes_of_kind(
    const AniCharacter& character, AniAnimationKind kind);

/// Runtime 0x00434630: the node with exactly this ID (0 never matches).
[[nodiscard]] const AniAnimation* ani_node_by_id(const AniCharacter& character, std::int32_t id);

/// Runtime 0x004346C0/0x00434730: the node of `kind` whose group name matches.
[[nodiscard]] const AniAnimation* ani_group_node(
    const AniCharacter& character, const std::string& group, AniAnimationKind kind);

}  // namespace App::Omikron

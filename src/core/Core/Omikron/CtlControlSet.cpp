#include "Core/Omikron/CtlControlSet.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Core/Omikron/Animation3DA.hpp"
#include "Core/Omikron/BinaryReader.hpp"

namespace App::Omikron {

namespace {

/// Fixed-width serialized string field: NUL-terminated when shorter.
[[nodiscard]] std::string fixed_string(const std::span<const std::byte> field) {
  std::string result;
  result.reserve(field.size());
  for (const std::byte value : field) {
    if (value == std::byte{0}) {
      break;
    }
    result.push_back(std::to_integer<char>(value));
  }
  return result;
}

/// Runtime canonicalizes CTL animation keys with the CRT _strupr uppercase.
[[nodiscard]] std::string canonical_animation_key(const std::string_view key) {
  std::string canonical{key};
  std::ranges::transform(canonical, canonical.begin(), [](const char character) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
  });
  return canonical;
}

/// State flags that suppress the serialized 12-byte animation key.
constexpr std::uint32_t K_STATE_NO_ANIMATION_KEY_FLAGS{0x00008002U};
/// State flags that consume the 0x18-byte auxiliary orientation block.
constexpr std::uint32_t K_STATE_ORIENTATION_BLOCK_FLAGS{0x00000100U | 0x00000040U};
/// State flags that consume the 0x14-byte auxiliary local-movement block.
constexpr std::uint32_t K_STATE_MOVEMENT_BLOCK_FLAGS{0x00000200U | 0x00000080U};
/// State flag that consumes the 12-byte deferred callback name.
constexpr std::uint32_t K_STATE_CALLBACK_FLAG{0x00000010U};
/// State flag that consumes the neutral 0x28-byte auxiliary block.
constexpr std::uint32_t K_STATE_AUX28_FLAG{0x02000000U};
/// animation_mode bit that appends the dynamic audio-marker block.
constexpr std::uint16_t K_ANIMATION_MODE_MARKERS{0x0008U};

constexpr std::size_t K_CTL_NAME_WIDTH{12U};

}  // namespace

std::expected<CtlControlSet, std::string> CtlControlSet::load(
    const std::span<const std::byte> data) {
  BinaryReader reader{data};
  if (data.size() < K_CTL_HEADER_SIZE) {
    return std::expected<CtlControlSet, std::string>{std::unexpect,
        fmt::format(
            "CTL resource is {} bytes; header requires {:#x}", data.size(), K_CTL_HEADER_SIZE)};
  }

  const std::uint32_t magic{reader.read_u32()};
  if (magic != K_CTL_MAGIC) {
    return std::expected<CtlControlSet, std::string>{std::unexpect,
        fmt::format("CTL magic {:#010x} does not match {:#010x}", magic, K_CTL_MAGIC)};
  }
  const std::uint32_t format_version{reader.read_u32()};
  constexpr std::uint32_t K_CTL_REQUIRED_VERSION{0x00000101U};
  if (format_version != K_CTL_REQUIRED_VERSION) {
    return std::expected<CtlControlSet, std::string>{std::unexpect,
        fmt::format(
            "CTL format version {:#010x} does not match required {:#010x}", format_version,
            K_CTL_REQUIRED_VERSION)};
  }
  const std::uint32_t raw_08{reader.read_u32()};
  const std::uint32_t move_count{reader.read_u32()};
  const std::span<const std::byte> reserved{reader.read_bytes(0x48U)};
  if (reader.has_error()) {
    return std::expected<CtlControlSet, std::string>{std::unexpect, reader.error()};
  }

  // --- Fixed top-level move records (contiguous 0x20-byte records) ---------
  std::vector<CtlMove> moves;
  moves.reserve(move_count);
  std::size_t total_state_count{0};
  for (std::uint32_t move_index{0}; move_index < move_count; ++move_index) {
    CtlMove move;
    move.move_id = reader.read_u32();
    const std::uint32_t child_state_count{reader.read_u32()};
    move.flags = reader.read_u32();
    move.raw_0c = reader.read_u32();
    move.raw_10 = reader.read_u32();
    move.name = fixed_string(reader.read_bytes(K_CTL_NAME_WIDTH));
    total_state_count += child_state_count;
    // Containment is resolved after the complete state array exists; stash the
    // authored count in the (soon replaced) states vector size for now.
    move.states.resize(child_state_count, nullptr);
    moves.push_back(std::move(move));
  }
  if (reader.has_error()) {
    return std::expected<CtlControlSet, std::string>{
        std::unexpect, fmt::format("CTL move records: {}", reader.error())};
  }

  // --- Fixed child-state records (contiguous 0x58-byte records) ------------
  std::vector<CtlState> states;
  states.reserve(total_state_count);
  std::vector<std::uint32_t> serialized_goto_ids;
  std::vector<std::uint8_t> parent_ref_counts;
  std::vector<std::uint8_t> child_ref_counts;
  serialized_goto_ids.reserve(total_state_count);
  parent_ref_counts.reserve(total_state_count);
  child_ref_counts.reserve(total_state_count);
  for (std::size_t state_index{0}; state_index < total_state_count; ++state_index) {
    CtlState state;
    state.state_id = reader.read_u32();
    state.input_condition = reader.read_u32();
    state.flags = reader.read_u32();
    state.raw_0c = reader.read_u32();
    state.window_start = reader.read_f32();
    state.window_end = reader.read_f32();
    state.transition_value = reader.read_f32();
    reader.skip(0x04U);  // +0x1C dynamic block: pointer-era field
    reader.skip(0x04U);  // +0x20 parent refs: pointer-era field
    reader.skip(0x04U);  // +0x24 child refs: pointer-era field
    serialized_goto_ids.push_back(reader.read_u32());
    reader.skip(0x04U);  // +0x2C: pointer-era field
    reader.skip(0x04U);  // +0x30: pointer-era field
    state.raw_34 = reader.read_u32();
    reader.skip(0x04U);  // +0x38 owner move: Runtime relocates; containment wins
    state.raw_3c = reader.read_u32();
    reader.skip(0x04U);  // +0x40 callback name: pointer-era field
    reader.skip(0x04U);  // +0x44 animation key: pointer-era field
    reader.skip(0x04U);  // +0x48 animation runtime pointer
    state.animation_mode = reader.read_u16();
    state.transition_count = reader.read_u16();
    state.phase_offset = reader.read_u16();
    state.defer_ticks = reader.read_u16();
    state.priority = reader.read_u16();
    parent_ref_counts.push_back(reader.read_u8());
    child_ref_counts.push_back(reader.read_u8());
    states.push_back(std::move(state));
  }
  if (reader.has_error()) {
    return std::expected<CtlControlSet, std::string>{
        std::unexpect, fmt::format("CTL child-state records: {}", reader.error())};
  }

  // --- Variable sections, each walked in serialized child order ------------

  // 12-byte animation keys for key-bearing states.
  for (CtlState& state : states) {
    if ((state.flags & K_STATE_NO_ANIMATION_KEY_FLAGS) != 0U) {
      continue;
    }
    state.animation_key = fixed_string(reader.read_bytes(K_CTL_NAME_WIDTH));
    state.canonical_animation_key = canonical_animation_key(state.animation_key);
  }

  // Child references: authored state IDs, never array indices or offsets.
  std::vector<std::vector<std::uint32_t>> serialized_child_refs;
  serialized_child_refs.reserve(states.size());
  for (std::size_t state_index{0}; state_index < states.size(); ++state_index) {
    std::vector<std::uint32_t> refs;
    refs.reserve(child_ref_counts.at(state_index));
    for (std::uint8_t ref_index{0}; ref_index < child_ref_counts.at(state_index); ++ref_index) {
      refs.push_back(reader.read_u32());
    }
    serialized_child_refs.push_back(std::move(refs));
  }

  // Parent references: authored state IDs.
  std::vector<std::vector<std::uint32_t>> serialized_parent_refs;
  serialized_parent_refs.reserve(states.size());
  for (std::size_t state_index{0}; state_index < states.size(); ++state_index) {
    std::vector<std::uint32_t> refs;
    refs.reserve(parent_ref_counts.at(state_index));
    for (std::uint8_t ref_index{0}; ref_index < parent_ref_counts.at(state_index); ++ref_index) {
      refs.push_back(reader.read_u32());
    }
    serialized_parent_refs.push_back(std::move(refs));
  }

  // 0x18-byte auxiliary orientation blocks.
  for (CtlState& state : states) {
    if ((state.flags & K_STATE_ORIENTATION_BLOCK_FLAGS) == 0U) {
      continue;
    }
    CtlOrientationBlock block;
    block.raw_00 = reader.read_f32();
    block.raw_04 = reader.read_f32();
    block.orientation_delta =
        Runtime::Vec3{.x = reader.read_f32(), .y = reader.read_f32(), .z = reader.read_f32()};
    block.raw_14 = reader.read_f32();
    state.orientation_block = block;
  }

  // 0x14-byte auxiliary local-movement blocks.
  for (CtlState& state : states) {
    if ((state.flags & K_STATE_MOVEMENT_BLOCK_FLAGS) == 0U) {
      continue;
    }
    CtlMovementBlock block;
    block.raw_00 = reader.read_f32();
    block.raw_04 = reader.read_f32();
    block.local_delta =
        Runtime::Vec3{.x = reader.read_f32(), .y = reader.read_f32(), .z = reader.read_f32()};
    state.movement_block = block;
  }

  // 12-byte deferred callback names.
  for (CtlState& state : states) {
    if ((state.flags & K_STATE_CALLBACK_FLAG) == 0U) {
      continue;
    }
    state.callback_name = fixed_string(reader.read_bytes(K_CTL_NAME_WIDTH));
  }

  // Neutral 0x28-byte auxiliary blocks.
  for (CtlState& state : states) {
    if ((state.flags & K_STATE_AUX28_FLAG) == 0U) {
      continue;
    }
    const std::span<const std::byte> block{reader.read_bytes(0x28U)};
    if (!block.empty()) {
      std::array<std::byte, 0x28U> preserved{};
      std::ranges::copy(block, preserved.begin());
      state.auxiliary_block_28 = preserved;
    }
  }

  // Dynamic animation-linked audio-marker blocks (animation_mode & 0x08).
  for (CtlState& state : states) {
    if ((state.animation_mode & K_ANIMATION_MODE_MARKERS) == 0U) {
      continue;
    }
    const std::uint32_t marker_count{reader.read_u32()};
    reader.skip(0x04U);  // runtime pointer placeholder
    state.audio_markers.reserve(marker_count);
    for (std::uint32_t marker_index{0}; marker_index < marker_count; ++marker_index) {
      CtlAudioMarker marker;
      marker.sync_duration = reader.read_f32();
      marker.active_start = reader.read_f32();
      marker.active_end = reader.read_f32();
      marker.one_shot_phase = reader.read_f32();
      marker.sound_property_raw = reader.read_u32();
      marker.synced_sound_id = reader.read_u16();
      marker.one_shot_sound_hid = reader.read_u16();
      marker.attachment_selector = reader.read_u8();
      marker.marker_flags = reader.read_u8();
      marker.raw_1a = reader.read_u16();
      marker.scalar_1c = reader.read_f32();
      state.audio_markers.push_back(marker);
    }
  }
  if (reader.has_error()) {
    return std::expected<CtlControlSet, std::string>{
        std::unexpect, fmt::format("CTL variable sections: {}", reader.error())};
  }

  // --- Embedded 3DA payloads: one per first occurrence of a unique canonical
  // animation key, in serialized child order. -------------------------------
  std::unordered_map<std::string, std::size_t> animation_indices;
  std::vector<Animation3DA> animations;
  {
    std::unordered_set<std::string> unique_keys;
    std::size_t unique_count{0};
    for (const CtlState& state : states) {
      if (!state.canonical_animation_key.empty() &&
          unique_keys.emplace(state.canonical_animation_key).second) {
        ++unique_count;
      }
    }
    animations.reserve(unique_count);
  }
  for (const CtlState& state : states) {
    const std::string& key{state.canonical_animation_key};
    if (key.empty() || animation_indices.contains(key)) {
      continue;
    }
    const std::uint32_t payload_size{reader.read_u32()};
    const std::span<const std::byte> payload{reader.read_bytes(payload_size)};
    if (reader.has_error()) {
      return std::expected<CtlControlSet, std::string>{
          std::unexpect, fmt::format("CTL embedded animation '{}': {}", key, reader.error())};
    }
    auto animation{Animation3DA::load(payload)};
    if (!animation) {
      return std::expected<CtlControlSet, std::string>{
          std::unexpect, fmt::format("CTL embedded animation '{}': {}", key, animation.error())};
    }
    animation_indices.emplace(key, animations.size());
    animations.push_back(std::move(animation).value());
  }
  if (reader.remaining() != 0U) {
    return std::expected<CtlControlSet, std::string>{std::unexpect,
        fmt::format("CTL resource has {} trailing byte(s) after the embedded animations",
            reader.remaining())};
  }

  // --- Link phase: resolve authored IDs into direct references -------------
  std::unordered_map<std::uint32_t, std::size_t> move_indices;
  move_indices.reserve(moves.size());
  for (std::size_t move_index{0}; move_index < moves.size(); ++move_index) {
    if (!move_indices.emplace(moves.at(move_index).move_id, move_index).second) {
      return std::expected<CtlControlSet, std::string>{
          std::unexpect, fmt::format("CTL duplicate move ID {}", moves.at(move_index).move_id)};
    }
  }
  std::unordered_map<std::uint32_t, std::size_t> state_indices;
  state_indices.reserve(states.size());
  for (std::size_t state_index{0}; state_index < states.size(); ++state_index) {
    if (!state_indices.emplace(states.at(state_index).state_id, state_index).second) {
      return std::expected<CtlControlSet, std::string>{
          std::unexpect, fmt::format("CTL duplicate state ID {}", states.at(state_index).state_id)};
    }
  }

  const auto resolve_state =
      [&state_indices, &states](
          const std::uint32_t state_id) -> std::expected<const CtlState*, std::string> {
    const auto found{state_indices.find(state_id)};
    if (found == state_indices.end()) {
      return std::expected<const CtlState*, std::string>{
          std::unexpect, fmt::format("CTL reference to missing state ID {}", state_id)};
    }
    return &states.at(found->second);
  };

  // Owner moves derive from serialized containment, not the +0x38 field.
  std::size_t containment_cursor{0};
  for (CtlMove& move : moves) {
    const std::size_t child_count{move.states.size()};
    if (child_count > states.size() - containment_cursor) {
      return std::expected<CtlControlSet, std::string>{std::unexpect,
          fmt::format("CTL move {} declares {} child states but only {} remain",
              move.move_id,
              child_count,
              states.size() - containment_cursor)};
    }
    for (std::size_t child_index{0}; child_index < child_count; ++child_index) {
      CtlState& child{states.at(containment_cursor + child_index)};
      child.owner_move = &move;
      move.states.at(child_index) = &child;
    }
    containment_cursor += child_count;
  }

  for (std::size_t state_index{0}; state_index < states.size(); ++state_index) {
    CtlState& state{states.at(state_index)};
    state.child_refs.reserve(serialized_child_refs.at(state_index).size());
    for (const std::uint32_t ref_id : serialized_child_refs.at(state_index)) {
      auto resolved{resolve_state(ref_id)};
      if (!resolved) {
        return std::expected<CtlControlSet, std::string>{std::unexpect,
            fmt::format("CTL state {} child reference: {}", state.state_id, resolved.error())};
      }
      state.child_refs.push_back(resolved.value());
    }
    state.parent_refs.reserve(serialized_parent_refs.at(state_index).size());
    for (const std::uint32_t ref_id : serialized_parent_refs.at(state_index)) {
      auto resolved{resolve_state(ref_id)};
      if (!resolved) {
        return std::expected<CtlControlSet, std::string>{std::unexpect,
            fmt::format("CTL state {} parent reference: {}", state.state_id, resolved.error())};
      }
      state.parent_refs.push_back(resolved.value());
    }
    const std::uint32_t goto_id{serialized_goto_ids.at(state_index)};
    if (goto_id != 0U) {
      auto resolved{resolve_state(goto_id)};
      if (!resolved) {
        return std::expected<CtlControlSet, std::string>{std::unexpect,
            fmt::format("CTL state {} goto reference: {}", state.state_id, resolved.error())};
      }
      state.goto_state = resolved.value();
    }
    if (!state.canonical_animation_key.empty()) {
      state.animation = &animations.at(animation_indices.at(state.canonical_animation_key));
    }
  }

  CtlControlSet control_set;
  control_set.m_format_version = format_version;
  control_set.m_raw_08 = raw_08;
  std::ranges::copy(reserved, control_set.m_reserved_header.begin());
  control_set.m_moves = std::move(moves);
  control_set.m_states = std::move(states);
  control_set.m_animations = std::move(animations);
  control_set.m_move_indices = std::move(move_indices);
  control_set.m_state_indices = std::move(state_indices);
  return control_set;
}

const CtlMove* CtlControlSet::move_by_id(const std::uint32_t move_id) const {
  const auto found{m_move_indices.find(move_id)};
  return found == m_move_indices.end() ? nullptr : &m_moves.at(found->second);
}

const CtlState* CtlControlSet::state_by_id(const std::uint32_t state_id) const {
  const auto found{m_state_indices.find(state_id)};
  return found == m_state_indices.end() ? nullptr : &m_states.at(found->second);
}

const CtlMove* CtlControlSet::default_move() const {
  const auto found{std::ranges::find_if(m_moves, [](const CtlMove& move) {
    return (move.flags & 0x1U) != 0U;
  })};
  return found == m_moves.end() ? nullptr : &(*found);
}

const CtlState* CtlControlSet::default_state(const CtlMove& move) {
  const auto found{std::ranges::find_if(move.states, [](const CtlState* state) {
    return (state->flags & 0x20U) != 0U;
  })};
  return found == move.states.end() ? nullptr : *found;
}

}  // namespace App::Omikron

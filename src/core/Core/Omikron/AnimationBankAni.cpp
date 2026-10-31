#include "Core/Omikron/AnimationBankAni.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Omikron/Animation3DA.hpp"
#include "Core/Omikron/BinaryReader.hpp"

namespace App::Omikron {

namespace {

constexpr std::array<std::byte, 4> K_MAGIC{
    std::byte{'3'}, std::byte{'.'}, std::byte{'0'}, std::byte{'V'}};
constexpr std::size_t K_ENTRY_TABLE_OFFSET{0x08};
constexpr std::size_t K_ENTRY_STRIDE{0x18};
constexpr std::size_t K_NODE_SIZE{0x24};
constexpr std::size_t K_GROUP_NAME_SIZE{8};
constexpr std::uint32_t K_MAX_ENTRY_COUNT{4096};
constexpr std::size_t K_MAX_NODES_PER_ENTRY{4096};

std::string read_fixed_string(BinaryReader& reader, const std::size_t length) {
  const std::span<const std::byte> bytes{reader.read_bytes(length)};
  std::string result;
  for (const std::byte byte : bytes) {
    const char character{static_cast<char>(byte)};
    if (character == '\0') {
      break;
    }
    result.push_back(character);
  }
  return result;
}

}  // namespace

std::expected<AnimationBankAni, std::string> AnimationBankAni::load(
    const std::span<const std::byte> data) {
  using Result = std::expected<AnimationBankAni, std::string>;
  if (data.size() < K_ENTRY_TABLE_OFFSET ||
      !std::ranges::equal(data.first(K_MAGIC.size()), K_MAGIC)) {
    return Result{std::unexpect, "ANI header is missing its 3.0V signature"};
  }
  BinaryReader header{data.subspan(K_MAGIC.size(), 4)};
  const std::uint32_t entry_count{header.read_u32()};
  if (entry_count > K_MAX_ENTRY_COUNT ||
      entry_count * K_ENTRY_STRIDE > data.size() - K_ENTRY_TABLE_OFFSET) {
    return Result{std::unexpect, fmt::format("ANI entry count {} is invalid", entry_count)};
  }

  AnimationBankAni bank;
  std::map<std::uint32_t, std::size_t> animation_by_offset;
  bank.characters.reserve(entry_count);
  for (std::uint32_t entry_index{0}; entry_index < entry_count; ++entry_index) {
    BinaryReader entry{
        data.subspan(K_ENTRY_TABLE_OFFSET + (entry_index * K_ENTRY_STRIDE), K_ENTRY_STRIDE)};
    AniCharacter character;
    character.character_type = entry.read_i32();
    std::uint32_t node_offset{entry.read_u32()};
    while (node_offset != 0U) {
      if (character.nodes.size() >= K_MAX_NODES_PER_ENTRY || node_offset > data.size() ||
          K_NODE_SIZE > data.size() - node_offset) {
        return Result{std::unexpect,
            fmt::format("ANI entry {} node at {:#x} is out of bounds", entry_index, node_offset)};
      }
      BinaryReader node_reader{data.subspan(node_offset, K_NODE_SIZE)};
      AniAnimation node;
      node.kind = node_reader.read_i32();
      node.id = node_reader.read_i32();
      const std::uint32_t animation_offset{node_reader.read_u32()};
      node.footstep_progress.at(0) = node_reader.read_f32();
      node.footstep_progress.at(1) = node_reader.read_f32();
      node.footstep_sound.at(0) = static_cast<std::int16_t>(node_reader.read_u16());
      node.footstep_sound.at(1) = static_cast<std::int16_t>(node_reader.read_u16());
      node_offset = node_reader.read_u32();
      node.group = read_fixed_string(node_reader, K_GROUP_NAME_SIZE);

      const auto known{animation_by_offset.find(animation_offset)};
      if (known != animation_by_offset.end()) {
        node.animation_index = known->second;
      } else {
        if (animation_offset == 0U || animation_offset >= data.size()) {
          return Result{std::unexpect,
              fmt::format(
                  "ANI node {} animation offset {:#x} is invalid", node.id, animation_offset)};
        }
        auto animation{Animation3DA::load(data.subspan(animation_offset))};
        if (!animation) {
          return Result{
              std::unexpect, fmt::format("ANI node {} animation: {}", node.id, animation.error())};
        }
        node.animation_index = bank.animations.size();
        animation_by_offset.emplace(animation_offset, node.animation_index);
        bank.animations.push_back(std::move(animation).value());
      }
      character.nodes.push_back(std::move(node));
    }
    bank.characters.push_back(std::move(character));
  }
  return bank;
}

const AniCharacter* AnimationBankAni::character(const std::int32_t character_type) const {
  const auto found{std::ranges::find(characters, character_type, &AniCharacter::character_type)};
  return found == characters.end() ? nullptr : &(*found);
}

std::vector<const AniAnimation*> ani_nodes_of_kind(
    const AniCharacter& character, const AniAnimationKind kind) {
  std::vector<const AniAnimation*> nodes;
  for (const AniAnimation& node : character.nodes) {
    if (node.kind == static_cast<std::int32_t>(kind)) {
      nodes.push_back(&node);
    }
  }
  return nodes;
}

const AniAnimation* ani_node_by_id(const AniCharacter& character, const std::int32_t id) {
  if (id == 0) {
    return nullptr;
  }
  const auto found{std::ranges::find(character.nodes, id, &AniAnimation::id)};
  return found == character.nodes.end() ? nullptr : &(*found);
}

const AniAnimation* ani_group_node(
    const AniCharacter& character, const std::string& group, const AniAnimationKind kind) {
  const auto found{std::ranges::find_if(character.nodes, [&](const AniAnimation& node) {
    return node.kind == static_cast<std::int32_t>(kind) && node.group == group;
  })};
  return found == character.nodes.end() ? nullptr : &(*found);
}

}  // namespace App::Omikron

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "Core/Omikron/IamStart.hpp"

namespace App::Tests {

inline void write_start_u32(
    std::vector<std::byte>& data, const std::size_t offset, const std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

inline void write_start_i16(
    std::vector<std::byte>& data, const std::size_t offset, const std::int16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

/// Canonical synthetic retail-geometry START with no established current body.
inline std::vector<std::byte> make_canonical_start(
    const std::int16_t current_area = 118, const std::int16_t linked_area = -1) {
  using Omikron::IamStart;
  std::vector<std::byte> data(IamStart::k_retail_size, std::byte{});
  write_start_u32(data, IamStart::k_format_revision_offset, 103);
  write_start_u32(data, IamStart::k_build_date_offset, 19'991'004);
  write_start_u32(data, IamStart::k_global_variables_begin_offset, 0x058C);
  write_start_u32(data, IamStart::k_global_variables_end_offset, 0x1064);
  write_start_u32(data, IamStart::k_packed_state_offset, 0x126C);
  write_start_u32(data, IamStart::k_character_flags_offset, 0x1314);
  write_start_u32(data, IamStart::k_address_flags_begin_offset, 0x1398);
  write_start_u32(data, IamStart::k_address_flags_end_offset, 0x13FC);

  std::fill(data.begin() + static_cast<std::ptrdiff_t>(IamStart::k_current_character_offset),
      data.begin() + static_cast<std::ptrdiff_t>(
                         IamStart::k_current_character_offset + IamStart::k_current_character_size),
      std::byte{0xFF});

  constexpr std::array<std::pair<std::size_t, std::size_t>, 3> k_collections{{
      {IamStart::k_object_collection_0_offset, IamStart::k_object_collection_0_capacity},
      {IamStart::k_object_collection_1_offset, IamStart::k_object_collection_1_capacity},
      {IamStart::k_object_collection_2_offset, IamStart::k_object_collection_2_capacity},
  }};
  for (const auto [offset, capacity] : k_collections) {
    for (std::size_t index{0}; index < capacity; ++index) {
      write_start_i16(data, offset + (index * sizeof(std::int16_t)), -1);
    }
  }
  for (std::size_t index{0}; index < 260U; ++index) {
    write_start_i16(data, 0x1064U + (index * sizeof(std::int16_t)), -1);
  }
  write_start_i16(data, IamStart::k_initial_area_offset, current_area);
  write_start_i16(data, IamStart::k_linked_area_offset, linked_area);
  return data;
}

}  // namespace App::Tests

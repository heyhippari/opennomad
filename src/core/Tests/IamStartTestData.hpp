#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

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
  std::vector<std::byte> data(0x1636U, std::byte{});
  write_start_u32(data, 0x00U, 103U);
  write_start_u32(data, 0x04U, 19'991'004U);
  write_start_u32(data, 0x08U, 0x058CU);
  write_start_u32(data, 0x0CU, 0x1064U);
  write_start_u32(data, 0x10U, 0x126CU);
  write_start_u32(data, 0x14U, 0x1314U);
  write_start_u32(data, 0x18U, 0x1398U);
  write_start_u32(data, 0x1CU, 0x13FCU);

  std::fill(data.begin() + 0x3CU, data.begin() + 0x150U, std::byte{0xFF});

  constexpr std::array<std::pair<std::size_t, std::size_t>, 3> k_collections{{
      {0x350U, 18U},
      {0x374U, 256U},
      {0x574U, 9U},
  }};
  for (const auto [offset, capacity] : k_collections) {
    for (std::size_t index{0}; index < capacity; ++index) {
      write_start_i16(data, offset + (index * sizeof(std::int16_t)), -1);
    }
  }
  for (std::size_t index{0}; index < 260U; ++index) {
    write_start_i16(data, 0x1064U + (index * sizeof(std::int16_t)), -1);
  }
  write_start_i16(data, 0x586U, current_area);
  write_start_i16(data, 0x588U, linked_area);
  return data;
}

/// Minimum valid required IAM/GLOBAL fixture with an empty camera table.
inline std::vector<std::byte> make_empty_iam_global() {
  return std::vector<std::byte>(0x20U, std::byte{});
}

}  // namespace App::Tests

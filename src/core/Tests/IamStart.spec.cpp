#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic, readability-suspicious-call-argument)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "Core/Omikron/IamStart.hpp"

namespace {

using App::Omikron::IamStart;

void write_u32(std::vector<std::byte>& data, const std::size_t offset, const std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_i16(std::vector<std::byte>& data, const std::size_t offset, const std::int16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

/// Builds a valid START buffer with initial area 118 and linked area -1.
std::vector<std::byte> make_start() {
  std::vector<std::byte> data(IamStart::k_min_size, std::byte{});
  write_u32(data, IamStart::k_area_mapping_offset, 0x10);
  write_i16(data, IamStart::k_initial_area_offset, 118);
  write_i16(data, IamStart::k_linked_area_offset, -1);
  return data;
}

}  // namespace

TEST_SUITE("Core::Omikron::IamStart") {
  TEST_CASE("START parsing returns initial area 118 and linked area -1") {
    const std::vector<std::byte> data{make_start()};
    const auto start{IamStart::load(data)};
    REQUIRE(start.has_value());
    CHECK_EQ(start->initial_area_id(), 118);
    CHECK_EQ(start->linked_area_id(), -1);
    CHECK_EQ(start->area_mapping_table_offset(), 0x10U);
  }

  TEST_CASE("START rejects files shorter than 0x58A") {
    const std::vector<std::byte> data(IamStart::k_min_size - 1U, std::byte{});
    const auto start{IamStart::load(data)};
    REQUIRE_FALSE(start.has_value());
    CHECK(start.error().find("too small") != std::string::npos);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic, readability-suspicious-call-argument)

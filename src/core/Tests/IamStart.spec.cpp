#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic, readability-suspicious-call-argument)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "Core/GameState.hpp"
#include "Core/Omikron/IamStart.hpp"

namespace {

using App::Omikron::IamStart;

void write_u32(std::vector<std::byte>& data, const std::size_t offset, const std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_i16(std::vector<std::byte>& data, const std::size_t offset, const std::int16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void fill_empty_object_collections(std::vector<std::byte>& data) {
  constexpr std::array<std::pair<std::size_t, std::size_t>, 3> k_collections{{
      {IamStart::k_object_collection_0_offset, IamStart::k_object_collection_0_capacity},
      {IamStart::k_object_collection_1_offset, IamStart::k_object_collection_1_capacity},
      {IamStart::k_object_collection_2_offset, IamStart::k_object_collection_2_capacity},
  }};
  for (const auto [offset, capacity] : k_collections) {
    for (std::size_t index{0}; index < capacity; ++index) {
      write_i16(data, offset + (index * sizeof(std::int16_t)), -1);
    }
  }
}

/// Builds a valid START buffer with initial area 118 and linked area -1.
std::vector<std::byte> make_start() {
  constexpr std::size_t k_address_byte_count{100};
  std::vector<std::byte> data(IamStart::k_min_size + k_address_byte_count, std::byte{});
  write_u32(data, IamStart::k_area_mapping_offset, 0x10);
  write_u32(data, IamStart::k_address_flags_begin_offset, IamStart::k_min_size);
  write_u32(data,
      IamStart::k_address_flags_end_offset,
      IamStart::k_min_size + k_address_byte_count);
  write_i16(data, IamStart::k_initial_area_offset, 118);
  write_i16(data, IamStart::k_linked_area_offset, -1);
  fill_empty_object_collections(data);
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

  TEST_CASE("START exposes ADDRESS bytes selected by its header bounds") {
    std::vector<std::byte> data{make_start()};
    data.at(IamStart::k_min_size) = std::byte{0x84};
    const auto start{IamStart::load(data)};
    REQUIRE(start.has_value());
    const auto flags{start->address_flags()};
    REQUIRE(flags.has_value());
    CHECK_EQ(flags->size(), 100U);
    CHECK_EQ(std::to_integer<std::uint8_t>(flags->front()), 0x84U);
    const auto state{App::GameState::from_start(start.value())};
    REQUIRE(state.has_value());
    CHECK_EQ(state->address_flags_raw()[0], 0x84U);
  }

  TEST_CASE("START rejects invalid ADDRESS header bounds") {
    std::vector<std::byte> data{make_start()};
    write_u32(data, IamStart::k_address_flags_begin_offset, 0x600);
    write_u32(data, IamStart::k_address_flags_end_offset, 0x5FF);
    const auto start{IamStart::load(data)};
    REQUIRE(start.has_value());
    const auto flags{start->address_flags()};
    REQUIRE_FALSE(flags.has_value());
    CHECK(flags.error().find("reversed") != std::string::npos);
  }

  TEST_CASE("GameState copies START ADDRESS bytes and updates checked bits") {
    std::vector<std::byte> data{make_start()};
    const auto start{IamStart::load(data)};
    REQUIRE(start.has_value());
    auto state{App::GameState::from_start(start.value())};
    REQUIRE(state.has_value());
    REQUIRE_EQ(state->address_flags_raw().size(), 100U);
    CHECK_EQ(state->address_flags_raw()[0], 0U);

    REQUIRE(state->set_address_flag(0, true).has_value());
    CHECK_EQ(state->address_flags_raw()[0], 0x01U);
    REQUIRE(state->set_address_flag(7, true).has_value());
    CHECK_EQ(state->address_flags_raw()[0], 0x81U);
    REQUIRE(state->set_address_flag(8, true).has_value());
    CHECK_EQ(state->address_flags_raw()[0], 0x81U);
    CHECK_EQ(state->address_flags_raw()[1], 0x01U);
    REQUIRE(state->set_address_flag(7, false).has_value());
    CHECK_EQ(state->address_flags_raw()[0], 0x01U);
    CHECK_FALSE(state->set_address_flag(800, true).has_value());
  }

  TEST_CASE("GameState preserves START object slots and inserts newest first") {
    std::vector<std::byte> data{make_start()};
    write_i16(data, IamStart::k_object_collection_2_offset, 10);
    write_i16(data, IamStart::k_object_collection_2_offset + 2U, 20);
    write_i16(data, IamStart::k_object_collection_2_offset + 4U, 30);
    const auto start{IamStart::load(data)};
    REQUIRE(start.has_value());
    auto state{App::GameState::from_start(start.value())};
    REQUIRE(state.has_value());
    auto kind2{state->persistent_object_collection(2)};
    REQUIRE(kind2.has_value());
    CHECK_EQ(kind2->size(), 9U);
    CHECK_EQ((*kind2)[0], 10);
    CHECK_EQ((*kind2)[3], -1);

    CHECK(state->add_object_to_collection(2, 314).value());
    kind2 = state->persistent_object_collection(2);
    REQUIRE(kind2.has_value());
    CHECK_EQ((*kind2)[0], 314);
    CHECK_EQ((*kind2)[1], 10);
    CHECK_EQ((*kind2)[3], 30);
    CHECK_FALSE(state->add_object_to_collection(2, 314).value());
    CHECK_EQ((*state->persistent_object_collection(2))[0], 314);

    CHECK(state->add_object_to_collection(0, 12).value());
    CHECK(state->add_object_to_collection(0, 12).value());
    const auto kind0{state->persistent_object_collection(0)};
    REQUIRE(kind0.has_value());
    CHECK_EQ((*kind0)[0], 12);
    CHECK_EQ((*kind0)[1], 12);
    CHECK_FALSE(state->add_object_to_collection(3, 314).has_value());
  }

  TEST_CASE("GameState leaves a full object collection unchanged") {
    std::vector<std::byte> data{make_start()};
    for (std::size_t index{0}; index < IamStart::k_object_collection_2_capacity; ++index) {
      write_i16(data,
          IamStart::k_object_collection_2_offset + (index * sizeof(std::int16_t)),
          static_cast<std::int16_t>(index + 100U));
    }
    const auto start{IamStart::load(data)};
    REQUIRE(start.has_value());
    auto state{App::GameState::from_start(start.value())};
    REQUIRE(state.has_value());
    CHECK_FALSE(state->add_object_to_collection(2, 314).value());
    const auto kind2{state->persistent_object_collection(2)};
    REQUIRE(kind2.has_value());
    CHECK_EQ((*kind2)[0], 100);
    CHECK_EQ((*kind2)[8], 108);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic, readability-suspicious-call-argument)

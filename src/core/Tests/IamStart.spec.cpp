#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/GameState.hpp"
#include "Core/Omikron/IamCharacterDefinition.hpp"
#include "Core/Omikron/IamStart.hpp"
#include "IamStartTestData.hpp"

namespace {

using App::Omikron::IamStart;

void write_u32(std::vector<std::byte>& data, const std::size_t offset, const std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_i16(std::vector<std::byte>& data, const std::size_t offset, const std::int16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u16(std::vector<std::byte>& data, const std::size_t offset, const std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_i32(std::vector<std::byte>& data, const std::size_t offset, const std::int32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_text(
    std::vector<std::byte>& data, const std::size_t offset, const std::string_view value) {
  std::memcpy(data.data() + offset, value.data(), value.size());
}

void populate_current_character(std::vector<std::byte>& data) {
  constexpr std::size_t k_record{IamStart::k_current_character_offset};
  std::fill(data.begin() + static_cast<std::ptrdiff_t>(k_record),
      data.begin() + static_cast<std::ptrdiff_t>(k_record + IamStart::k_current_character_size),
      std::byte{});
  write_u32(data, k_record + 0x00U, 0xDEADBEEFU);  // ignored live-pointer representation.
  write_u32(data, k_record + 0x04U, 0xCAFEBABEU);  // ignored live-pointer representation.
  write_text(data, k_record + 0x008U, "KAY'L 669");
  write_text(data, k_record + 0x028U, "Investigating Agent");
  write_text(data, k_record + 0x048U, "H1AVNT");
  write_text(data, k_record + 0x05AU, "H1CMBT");
  write_text(data, k_record + 0x06CU, "M");
  write_text(data, k_record + 0x074U, "Green");
  write_text(data, k_record + 0x07CU, "K-");
  write_text(data, k_record + 0x080U, "178");
  write_text(data, k_record + 0x088U, "80");
  write_text(data, k_record + 0x090U, "HO1_FN");
  write_i16(data, k_record + 0x09AU, 30);
  write_i16(data, k_record + 0x09CU, 22);
  write_i16(data, k_record + 0x09EU, 33);
  write_i16(data, k_record + 0x0A0U, 44);
  write_i16(data, k_record + 0x0A2U, 55);
  write_i16(data, k_record + 0x0A4U, 66);
  write_i16(data, k_record + 0x0A6U, 77);
  write_i16(data, k_record + 0x0A8U, 88);
  write_i16(data, k_record + 0x0AAU, 99);
  write_u16(data, k_record + 0x0ACU, 50'000);
  write_i16(data, k_record + 0x0AEU, 12);
  write_u32(
      data, k_record + 0x0B0U, static_cast<std::uint32_t>(App::Omikron::CharacterType::Incarnable));
  for (std::size_t index{0}; index < 11U; ++index) {
    write_i16(data, k_record + 0x0B4U + (index * 2U), static_cast<std::int16_t>(100 + index));
  }
  write_i16(data, k_record + 0x0CAU, 201);
  write_i16(data, k_record + 0x0E2U, 301);
  write_i16(data, k_record + 0x0FAU, 401);
  write_i16(data, k_record + 0x104U, 501);
  write_i16(data, k_record + 0x10EU, -1);
  write_i16(data, k_record + 0x110U, 136);
  write_i16(data, k_record + 0x112U, -7);
  write_text(data, IamStart::k_signs_offset, "Loyal and determined");
  write_text(data, IamStart::k_interests_offset, "Justice and honor");
}

}  // namespace

TEST_SUITE("Core::Omikron::IamStart") {
  TEST_CASE("START validates canonical region geometry and exposes every recovered region") {
    const std::vector<std::byte> data{App::Tests::make_canonical_start()};
    const auto start{IamStart::load(data)};
    REQUIRE(start.has_value());
    CHECK_EQ(start->format_revision(), 103U);
    CHECK_EQ(start->build_date(), 19'991'004U);
    CHECK_EQ(start->initial_area_id(), 118);
    CHECK_EQ(start->linked_area_id(), -1);
    CHECK_EQ(start->area_mapping_table_offset(), 0x1064U);
    CHECK_EQ(start->global_variables()->size(), 694U * sizeof(std::int32_t));
    CHECK_EQ(start->area_mappings()->size(), 260U * sizeof(std::int16_t));
    CHECK_EQ(start->packed_state_bytes()->size(), 168U);
    CHECK_EQ(start->character_flags()->size(), 132U);
    CHECK_EQ(start->address_flags()->size(), 100U);
    CHECK_EQ(start->zone_flags()->size(), 570U);
    REQUIRE(start->current_character().has_value());
    CHECK_FALSE(start->current_character()->has_value());
  }

  TEST_CASE("START rejects truncated, reversed, out-of-file and misaligned region chains") {
    const std::vector<std::byte> truncated(IamStart::k_min_size - 1U, std::byte{});
    CHECK_FALSE(IamStart::load(truncated).has_value());

    auto reversed{App::Tests::make_canonical_start()};
    write_u32(reversed, IamStart::k_packed_state_offset, 0x1000U);
    const auto reversed_result{IamStart::load(reversed)};
    REQUIRE_FALSE(reversed_result.has_value());
    CHECK(reversed_result.error().find("boundary") != std::string::npos);

    auto outside{App::Tests::make_canonical_start()};
    write_u32(outside,
        IamStart::k_address_flags_end_offset,
        static_cast<std::uint32_t>(outside.size() + 1U));
    const auto outside_result{IamStart::load(outside)};
    REQUIRE_FALSE(outside_result.has_value());
    CHECK(outside_result.error().find("logical end") != std::string::npos);

    auto globals{App::Tests::make_canonical_start()};
    write_u32(globals, IamStart::k_global_variables_end_offset, 0x1063U);
    const auto globals_result{IamStart::load(globals)};
    REQUIRE_FALSE(globals_result.has_value());
    CHECK(globals_result.error().find("global-variable") != std::string::npos);

    auto area_map{App::Tests::make_canonical_start()};
    write_u32(area_map, IamStart::k_packed_state_offset, 0x126BU);
    const auto area_map_result{IamStart::load(area_map)};
    REQUIRE_FALSE(area_map_result.has_value());
    CHECK(area_map_result.error().find("area-map") != std::string::npos);
  }

  TEST_CASE("START decodes fixed state, opaque bytes and a populated current character") {
    auto data{App::Tests::make_canonical_start(222, 55)};
    data.at(IamStart::k_opaque_header_offset) = std::byte{0xA1};
    data.at(IamStart::k_opaque_area_offset + 1U) = std::byte{0xB2};
    write_i32(data, IamStart::k_saved_position_offset, -1234);
    write_i32(data, IamStart::k_saved_position_offset + 4U, 5678);
    write_i32(data, IamStart::k_saved_position_offset + 8U, -90);
    write_i32(data, IamStart::k_saved_orientation_offset, 4095);
    populate_current_character(data);

    const auto start{IamStart::load(data)};
    REQUIRE(start.has_value());
    CHECK_EQ(start->saved_position(), std::array<std::int32_t, 3>{-1234, 5678, -90});
    CHECK_EQ(start->saved_orientation(), 4095);
    CHECK_EQ(start->opaque_header_state().front(), std::byte{0xA1});
    CHECK_EQ(start->opaque_area_state().back(), std::byte{0xB2});
    const auto current{start->current_character()};
    REQUIRE(current.has_value());
    REQUIRE(current->has_value());
    CHECK_EQ(current->value().name, "KAY'L 669");
    CHECK_EQ(current->value().job, "Investigating Agent");
    CHECK_EQ(current->value().signs, std::optional<std::string>{"Loyal and determined"});
    CHECK_EQ(current->value().interests, std::optional<std::string>{"Justice and honor"});
    CHECK_EQ(current->value().values.energy, 99);
    CHECK_EQ(current->value().values.seteks, 50'000U);
    CHECK_EQ(current->value().linked_object_id, -1);
    CHECK_EQ(current->value().character_id, 136);
    CHECK_EQ(current->value().unknown_112, -7);
    CHECK_EQ(current->value().behavior_parameters.at(10), 110);
    CHECK_EQ(current->value().ammunition.at(0), 501);
  }

  TEST_CASE("GameState copies and checks global, area-map and fixed scalar state") {
    auto data{App::Tests::make_canonical_start(222, 55)};
    write_i32(data, 0x058CU, 1234);
    write_i32(data, 0x0590U, -55);
    write_i16(data, 0x1064U + (222U * 2U), -1);
    write_i16(data, 0x1064U + (7U * 2U), 99);
    write_i32(data, IamStart::k_saved_position_offset, -333);
    data.at(IamStart::k_opaque_header_offset + 3U) = std::byte{0x44};
    const auto start{IamStart::load(data)};
    REQUIRE(start.has_value());
    auto state{App::GameState::from_start(start.value())};
    REQUIRE(state.has_value());
    CHECK_EQ(state->format_revision(), 103U);
    CHECK_EQ(state->current_area(), 222);
    CHECK_EQ(state->linked_area(), 55);
    CHECK_EQ(state->saved_position().at(0), -333);
    CHECK_EQ(state->opaque_header_state().at(3), std::byte{0x44});
    CHECK_EQ(state->global_variables().size(), 694U);
    CHECK_EQ(state->global_variable(0).value(), 1234);
    CHECK_EQ(state->global_variable(1).value(), -55);
    REQUIRE(state->set_global_variable(1, -900).has_value());
    CHECK_EQ(state->global_variable(1).value(), -900);
    CHECK_FALSE(state->set_global_variable(694, 1).has_value());
    CHECK_EQ(state->area_mappings().size(), 260U);
    CHECK_EQ(state->area_mapping(222).value(), -1);
    CHECK_EQ(state->area_mapping(7).value(), 99);
    REQUIRE(state->set_area_mapping(222, 42).has_value());
    CHECK_EQ(state->area_mapping(222).value(), 42);
    CHECK_FALSE(state->set_area_mapping(-1, 0).has_value());
    CHECK_FALSE(state->set_area_mapping(260, 0).has_value());
  }

  TEST_CASE("GameState packed state and all persistent bitsets use recovered ordering") {
    auto data{App::Tests::make_canonical_start()};
    data.at(0x126CU) = std::byte{0xE4};
    data.at(0x1314U) = std::byte{0x80};
    data.at(0x1398U) = std::byte{0x01};
    data.at(0x13FCU) = std::byte{0x20};
    const auto start{IamStart::load(data)};
    REQUIRE(start.has_value());
    auto state{App::GameState::from_start(start.value())};
    REQUIRE(state.has_value());

    CHECK_EQ(state->packed_state(0).value(), 0U);
    CHECK_EQ(state->packed_state(1).value(), 1U);
    CHECK_EQ(state->packed_state(2).value(), 2U);
    CHECK_EQ(state->packed_state(3).value(), 3U);
    for (std::size_t index{0}; index < 4U; ++index) {
      REQUIRE(state->set_packed_state(index, static_cast<std::uint8_t>(3U - index)).has_value());
      CHECK_EQ(state->packed_state(index).value(), 3U - index);
    }
    CHECK_FALSE(state->set_packed_state(0, 4).has_value());
    CHECK_FALSE(state->packed_state(672).has_value());
    CHECK_FALSE(state->set_packed_state(672, 0).has_value());

    CHECK(state->character_flag(7).value());
    REQUIRE(state->set_character_flag(0, true).has_value());
    CHECK_EQ(state->character_flags_raw().front(), 0x81U);
    CHECK_FALSE(state->set_character_flag(1056, true).has_value());
    CHECK(state->address_flag(0));
    REQUIRE(state->set_address_flag(7, true).has_value());
    CHECK_EQ(state->address_flags_raw().front(), 0x81U);
    CHECK_FALSE(state->set_address_flag(800, true).has_value());
    CHECK(state->zone_flag(5).value());
    CHECK(state->zone_flag(0x8005U).value());
    REQUIRE(state->set_zone_flag(0x8005U, false).has_value());
    CHECK_FALSE(state->zone_flag(5).value());
    CHECK_FALSE(state->set_zone_flag(4560, true).has_value());
  }

  TEST_CASE("GameState preserves object collection behavior") {
    auto data{App::Tests::make_canonical_start()};
    write_i16(data, IamStart::k_object_collection_2_offset, 10);
    write_i16(data, IamStart::k_object_collection_2_offset + 2U, 20);
    write_i16(data, IamStart::k_object_collection_2_offset + 4U, 30);
    const auto start{IamStart::load(data)};
    REQUIRE(start.has_value());
    auto state{App::GameState::from_start(start.value())};
    REQUIRE(state.has_value());
    CHECK(state->add_object_to_collection(2, 314).value());
    CHECK_FALSE(state->add_object_to_collection(2, 314).value());
    const auto kind2{state->persistent_object_collection(2)};
    REQUIRE(kind2.has_value());
    CHECK_EQ((*kind2)[0], 314);
    CHECK_EQ((*kind2)[1], 10);
    CHECK_EQ((*kind2)[3], 30);
    CHECK(state->add_object_to_collection(0, 12).value());
    CHECK(state->add_object_to_collection(0, 12).value());
    CHECK(state->add_object_to_collection(1, 44).value());
    CHECK(state->add_object_to_collection(1, 44).value());
    CHECK_FALSE(state->add_object_to_collection(3, 12).has_value());
  }

  TEST_CASE("GameState leaves a full persistent object collection unchanged") {
    auto data{App::Tests::make_canonical_start()};
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

  TEST_CASE("Current CHARACTER VALUE writes use canonical persistent character fields") {
    auto data{App::Tests::make_canonical_start()};
    populate_current_character(data);
    const auto start{IamStart::load(data)};
    REQUIRE(start.has_value());
    auto state{App::GameState::from_start(start.value())};
    REQUIRE(state.has_value());
    REQUIRE(state->current_character().has_value());
    CHECK_EQ(state->character_value(136, 1).value(), 99);
    CHECK_EQ(state->character_value(136, 4).value(), 50'000);
    CHECK_EQ(state->character_value(136, 5).value(), 12);
    REQUIRE(state->set_character_value(136, 4, 777).has_value());
    REQUIRE(state->set_character_value(136, 5, 33).has_value());
    CHECK_EQ(state->current_character()->values.seteks, 777U);
    CHECK_EQ(state->current_character()->values.rings, 33);
  }

  TEST_CASE(
      "Current-character promotion preserves profiles and synchronizes the body left behind") {
    const std::vector<std::byte> data{App::Tests::make_canonical_start()};
    const auto start{IamStart::load(data)};
    REQUIRE(start.has_value());
    auto state{App::GameState::from_start(start.value())};
    REQUIRE(state.has_value());
    App::Omikron::IamCharacterDefinition first;
    first.character_id = 10;
    first.values.rings = 5;
    App::Omikron::IamCharacterValueInitialState accumulated{first.values};
    accumulated.rings = 91;
    state->ensure_character_profile(10, accumulated);
    state->establish_current_character(first);
    REQUIRE(state->current_character().has_value());
    CHECK_EQ(state->current_character()->values.rings, 91);
    REQUIRE(state->set_character_value(10, 5, 42).has_value());

    App::Omikron::IamCharacterDefinition second;
    second.character_id = 20;
    second.values.rings = 7;
    state->establish_current_character(second);
    CHECK_EQ(state->current_character()->character_id, 20);
    CHECK_EQ(state->character_value(10, 5).value(), 42);
  }

  TEST_CASE("CharacterValueState retains recovered kind clamps") {
    App::CharacterValueState state{App::Omikron::IamCharacterValueInitialState{.mana = 2,
        .speed = 3,
        .attack = 16,
        .body_resistance = 17,
        .dodge = 18,
        .fight_experience = 19,
        .unknown_characteristic_a8 = 20,
        .energy = 1,
        .seteks = 4,
        .rings = 5}};
    for (const std::int16_t kind : {std::int16_t{1},
             std::int16_t{2},
             std::int16_t{3},
             std::int16_t{16},
             std::int16_t{17},
             std::int16_t{18},
             std::int16_t{19},
             std::int16_t{20}}) {
      REQUIRE(state.set(kind, 250).has_value());
      CHECK_EQ(state.get(kind).value(), 200);
    }
    REQUIRE(state.set(4, 70'000).has_value());
    CHECK_EQ(state.get(4).value(), 65'535);
    REQUIRE(state.set(5, 65'535).has_value());
    CHECK_EQ(state.get(5).value(), -1);
    CHECK_FALSE(state.get(35).has_value());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

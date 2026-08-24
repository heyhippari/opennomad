#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamCharacterDefinition.hpp"

namespace {

using App::Omikron::IamAreaRecord;

void write_i16(std::vector<std::byte>& data, const std::size_t offset, const std::int16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u16(std::vector<std::byte>& data, const std::size_t offset, const std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_i32(std::vector<std::byte>& data, const std::size_t offset, const std::int32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u32(std::vector<std::byte>& data, const std::size_t offset, const std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

/// Writes a fixed nine-byte, NUL-padded name field.
void write_name(
    std::vector<std::byte>& data, const std::size_t offset, const std::string_view name) {
  for (std::size_t index{0}; index < IamAreaRecord::k_name_field_size; ++index) {
    data[offset + index] = index < name.size() ? static_cast<std::byte>(name[index]) : std::byte{};
  }
}

/// Builds a header-only 0x9C0 fixture with AREA 118's script/name geometry.
std::vector<std::byte> make_area_118() {
  std::vector<std::byte> data(0x9C0, std::byte{});
  write_u32(data, IamAreaRecord::k_offset_script, 0x3FC);
  write_name(data, IamAreaRecord::k_offset_model3do_name, "GRID");
  write_name(data, IamAreaRecord::k_offset_scenario_scx_name, "GRID");
  return data;
}

}  // namespace

TEST_SUITE("Core::Omikron::IamAreaRecord") {
  TEST_CASE("A header-only area-118-shaped fixture reports size, script offset and names") {
    const std::vector<std::byte> data{make_area_118()};
    const auto record{IamAreaRecord::load(data)};
    REQUIRE(record.has_value());
    CHECK_EQ(record->record_size(), 0x9C0U);
    CHECK_EQ(record->script_offset(), 0x3FCU);
    CHECK_EQ(record->model3do_name(), "GRID");
    CHECK_EQ(record->scenario_scx_name(), "GRID");
    CHECK_EQ(record->script_bytes().size(), 0x9C0U - 0x3FCU);
  }

  TEST_CASE("AREA rejects records shorter than 0xB4") {
    const std::vector<std::byte> data(IamAreaRecord::k_header_size - 1U, std::byte{});
    const auto record{IamAreaRecord::load(data)};
    REQUIRE_FALSE(record.has_value());
    CHECK(record.error().find("too small") != std::string::npos);
  }

  TEST_CASE("AREA rejects a script offset outside the record") {
    std::vector<std::byte> data(0xB4, std::byte{});
    write_u32(data, IamAreaRecord::k_offset_script, 0x1000);
    const auto record{IamAreaRecord::load(data)};
    REQUIRE_FALSE(record.has_value());
    CHECK(record.error().find("script offset") != std::string::npos);
  }

  TEST_CASE("AREA validates known table spans") {
    std::vector<std::byte> data(0xB4, std::byte{});
    write_u32(data, IamAreaRecord::k_offset_table_offsets, 0xB0);  // near the end.
    write_u16(data, IamAreaRecord::k_offset_table_counts, 2);      // stride 0x14 -> overflow.
    const auto record{IamAreaRecord::load(data)};
    REQUIRE_FALSE(record.has_value());
    CHECK(record.error().find("table 0") != std::string::npos);
  }

  TEST_CASE("Known table strides match the recovered values") {
    CHECK_EQ(IamAreaRecord::known_table_stride(0), std::optional<std::size_t>{0x14});
    CHECK_EQ(IamAreaRecord::known_table_stride(1), std::optional<std::size_t>{0x18});
    CHECK_EQ(IamAreaRecord::known_table_stride(2), std::optional<std::size_t>{0x44});
    CHECK_EQ(IamAreaRecord::known_table_stride(3), std::nullopt);
    CHECK_EQ(IamAreaRecord::known_table_stride(4), std::optional<std::size_t>{0x114});
    CHECK_EQ(IamAreaRecord::known_table_stride(5), std::optional<std::size_t>{0x10});
    CHECK_EQ(IamAreaRecord::known_table_stride(6), std::optional<std::size_t>{0x2C});
    CHECK_EQ(IamAreaRecord::known_table_stride(7), std::optional<std::size_t>{0x08});
  }

  TEST_CASE("AREA table 0 exposes recovered character records by signed ID") {
    constexpr std::size_t k_character_offset{IamAreaRecord::k_header_size};
    constexpr std::size_t k_character_stride{0x14};
    std::vector<std::byte> data(k_character_offset + k_character_stride, std::byte{});

    write_u32(data, IamAreaRecord::k_offset_script, static_cast<std::uint32_t>(data.size()));
    write_u32(data, IamAreaRecord::k_offset_table_offsets, k_character_offset);
    write_u16(data, IamAreaRecord::k_offset_table_counts, 1);

    // Retail area 118, table-0 character 310.
    write_i16(data, k_character_offset + 0x00U, -1);
    write_i16(data, k_character_offset + 0x02U, 310);
    write_i32(data, k_character_offset + 0x04U, -2588);
    write_i32(data, k_character_offset + 0x08U, -271);
    write_i32(data, k_character_offset + 0x0CU, -816);
    write_i16(data, k_character_offset + 0x10U, 4084);
    write_u16(data, k_character_offset + 0x12U, 468);

    const auto record{IamAreaRecord::load(data)};
    REQUIRE(record.has_value());

    const auto character{record->character_by_id(310)};
    REQUIRE(character.has_value());
    CHECK_EQ(character->field_00, -1);
    CHECK_EQ(character->character_id, 310);
    CHECK_EQ(character->serialized_position.at(0), -2588);
    CHECK_EQ(character->serialized_position.at(1), -271);
    CHECK_EQ(character->serialized_position.at(2), -816);
    CHECK_EQ(character->orientation_units, 4084);
    CHECK_FALSE(record->character_by_id(999).has_value());
  }

  TEST_CASE("AREA character identity resolves a generic table-4 character body") {
    constexpr std::size_t k_character_offset{IamAreaRecord::k_header_size};
    constexpr std::size_t k_definition_offset{k_character_offset + 0x14U};
    constexpr std::uint16_t k_definition_id{468};
    constexpr std::size_t k_signs_offset{k_definition_offset + 0x114U};
    constexpr std::size_t k_interests_offset{k_signs_offset + 16U};
    std::vector<std::byte> data(k_interests_offset + 16U, std::byte{});

    write_u32(data, IamAreaRecord::k_offset_script, static_cast<std::uint32_t>(data.size()));
    write_u32(data, IamAreaRecord::k_offset_table_offsets, k_character_offset);
    write_u16(data, IamAreaRecord::k_offset_table_counts, 1);
    write_i16(data, k_character_offset + 0x02U, 310);
    write_u16(data, k_character_offset + 0x12U, k_definition_id);

    write_u32(data, IamAreaRecord::k_offset_table_offsets + (4U * 4U), k_definition_offset);
    write_u16(data, IamAreaRecord::k_offset_table_counts + (4U * 2U), 1);
    constexpr std::string_view k_name{"KAY'L 669"};
    std::memcpy(data.data() + k_definition_offset + 0x08U, k_name.data(), k_name.size());
    constexpr std::string_view k_model{"HO1_FNM"};
    std::memcpy(data.data() + k_definition_offset + 0x90U, k_model.data(), k_model.size());
    write_i16(data, k_definition_offset + 0x9CU, 22);
    write_i16(data, k_definition_offset + 0xAAU, 11);
    write_u16(data, k_definition_offset + 0xACU, 44);
    write_i16(data, k_definition_offset + 0xAEU, 55);
    write_i16(data, k_definition_offset + 0x10EU, -1);
    write_i16(data, k_definition_offset + 0x110U, 310);
    write_i16(data, k_definition_offset + 0x112U, -9);
    write_u32(data, k_definition_offset + 0x00U, k_signs_offset);
    write_u32(data, k_definition_offset + 0x04U, k_interests_offset);
    constexpr std::string_view k_signs{"Brave and loyal"};
    constexpr std::string_view k_interests{"Justice"};
    std::memcpy(data.data() + k_signs_offset, k_signs.data(), k_signs.size());
    std::memcpy(data.data() + k_interests_offset, k_interests.data(), k_interests.size());

    const auto record{IamAreaRecord::load(data)};
    REQUIRE(record.has_value());
    const auto placement{record->character_by_id(310)};
    REQUIRE(placement.has_value());
    const auto definition{record->character_definition_by_character_id(310)};
    REQUIRE(definition.has_value());
    CHECK_EQ(definition->name, "KAY'L 669");
    CHECK_EQ(definition->model_resource, "HO1_FNM");
    CHECK_EQ(definition->signs, std::optional<std::string>{"Brave and loyal"});
    CHECK_EQ(definition->interests, std::optional<std::string>{"Justice"});
    CHECK_EQ(definition->values.energy, 11);
    CHECK_EQ(definition->values.mana, 22);
    CHECK_EQ(definition->values.seteks, 44);
    CHECK_EQ(definition->values.rings, 55);
    CHECK_EQ(definition->linked_object_id, -1);
    CHECK_EQ(definition->character_id, 310);
    CHECK_EQ(definition->unknown_112, -9);
  }

  TEST_CASE("Shared character-definition parser accepts only established type values") {
    std::vector<std::byte> definition(0x114U, std::byte{});
    write_u32(definition, 0x0B0U, 0xFFFFFFFFU);

    const auto unspecified{
        App::Omikron::parse_iam_character_definition(definition, std::nullopt, std::nullopt)};
    REQUIRE(unspecified.has_value());
    CHECK(unspecified->character_type == App::Omikron::CharacterType::Unspecified);

    for (const std::uint32_t invalid_type : {14U, 0xFFFFFFFEU}) {
      write_u32(definition, 0x0B0U, invalid_type);
      const auto invalid{
          App::Omikron::parse_iam_character_definition(definition, std::nullopt, std::nullopt)};
      REQUIRE_FALSE(invalid.has_value());
      CHECK(invalid.error().find("character type") != std::string::npos);
    }
  }

  TEST_CASE("AREA preserves the real area-118 sentinel character definition") {
    constexpr std::size_t k_table0_offset{0x0B4U};
    constexpr std::size_t k_table4_offset{0x0DCU};
    std::vector<std::byte> data(0x9C0U, std::byte{});
    write_u32(data, IamAreaRecord::k_offset_script, 0x3FCU);
    write_u32(data, IamAreaRecord::k_offset_table_offsets, k_table0_offset);
    write_u16(data, IamAreaRecord::k_offset_table_counts, 2);
    write_u32(data, IamAreaRecord::k_offset_table_offsets + (4U * 4U), k_table4_offset);
    write_u16(data, IamAreaRecord::k_offset_table_counts + (4U * 2U), 2);

    write_i16(data, k_table0_offset + 0x00U, -1);
    write_i16(data, k_table0_offset + 0x02U, 310);
    write_i32(data, k_table0_offset + 0x04U, -2588);
    write_i32(data, k_table0_offset + 0x08U, -271);
    write_i32(data, k_table0_offset + 0x0CU, -816);
    write_i16(data, k_table0_offset + 0x10U, 4084);
    write_u16(data, k_table0_offset + 0x12U, 468);

    constexpr std::string_view k_model{"HO1_FNM"};
    std::memcpy(data.data() + k_table4_offset + 0x90U, k_model.data(), k_model.size());
    write_u32(data, k_table4_offset + 0x0B0U, 0xFFFFFFFFU);
    write_i16(data, k_table4_offset + 0x10EU, -1);
    write_i16(data, k_table4_offset + 0x110U, 310);

    const auto record{IamAreaRecord::load(data)};
    REQUIRE(record.has_value());
    REQUIRE(record->character_by_id(310).has_value());
    const auto definition{record->character_definition_by_character_id(310)};
    REQUIRE(definition.has_value());
    CHECK_EQ(definition->model_resource, "HO1_FNM");
    CHECK_EQ(definition->character_id, 310);
    CHECK(definition->character_type == App::Omikron::CharacterType::Unspecified);
  }

  TEST_CASE("AREA authored strings accept zero and reject invalid record-relative offsets") {
    constexpr std::size_t k_definition_offset{IamAreaRecord::k_header_size};
    std::vector<std::byte> absent(k_definition_offset + 0x114U, std::byte{});
    write_u32(absent, IamAreaRecord::k_offset_script, static_cast<std::uint32_t>(absent.size()));
    write_u32(absent, IamAreaRecord::k_offset_table_offsets + (4U * 4U), k_definition_offset);
    write_u16(absent, IamAreaRecord::k_offset_table_counts + (4U * 2U), 1);
    write_i16(absent, k_definition_offset + 0x110U, 7);
    const auto absent_record{IamAreaRecord::load(absent)};
    REQUIRE(absent_record.has_value());
    const auto definition{absent_record->character_definition_by_character_id(7)};
    REQUIRE(definition.has_value());
    CHECK_FALSE(definition->signs.has_value());
    CHECK_FALSE(definition->interests.has_value());

    auto outside{absent};
    write_u32(outside, k_definition_offset, static_cast<std::uint32_t>(outside.size()));
    const auto outside_record{IamAreaRecord::load(outside)};
    REQUIRE_FALSE(outside_record.has_value());
    CHECK(outside_record.error().find("string") != std::string::npos);

    auto unterminated{absent};
    unterminated.push_back(std::byte{'x'});
    write_u32(
        unterminated, k_definition_offset, static_cast<std::uint32_t>(unterminated.size() - 1U));
    const auto unterminated_record{IamAreaRecord::load(unterminated)};
    REQUIRE_FALSE(unterminated_record.has_value());
    CHECK(unterminated_record.error().find("NUL") != std::string::npos);
  }

  TEST_CASE("AREA table 6 exposes recovered camera records by signed ID") {
    constexpr std::size_t k_camera_offset{IamAreaRecord::k_header_size};
    std::vector<std::byte> data(k_camera_offset + 0x2CU, std::byte{});
    write_u32(data, IamAreaRecord::k_offset_script, IamAreaRecord::k_header_size);
    write_u32(data, IamAreaRecord::k_offset_table_offsets + (6U * 4U), k_camera_offset);
    write_u16(data, IamAreaRecord::k_offset_table_counts + (6U * 2U), 1);

    write_i32(data, k_camera_offset + 0x00U, -3287);
    write_i32(data, k_camera_offset + 0x04U, -159);
    write_i32(data, k_camera_offset + 0x08U, -1701);
    write_i32(data, k_camera_offset + 0x0CU, -3214);
    write_i32(data, k_camera_offset + 0x10U, -269);
    write_i32(data, k_camera_offset + 0x14U, -944);
    write_i16(data, k_camera_offset + 0x18U, 2172);
    write_u16(data, k_camera_offset + 0x1AU, 12);
    write_i16(data, k_camera_offset + 0x1CU, 0);
    write_i16(data, k_camera_offset + 0x1EU, 853);
    write_i16(data, k_camera_offset + 0x20U, -1);
    write_i16(data, k_camera_offset + 0x22U, -1);

    const auto record{IamAreaRecord::load(data)};
    REQUIRE(record.has_value());
    const auto camera{record->camera_by_id(2172)};
    REQUIRE(camera.has_value());
    CHECK_EQ(camera->serialized_eye.at(0), -3287);
    CHECK_EQ(camera->serialized_eye.at(1), -159);
    CHECK_EQ(camera->serialized_eye.at(2), -1701);
    CHECK_EQ(camera->serialized_target.at(2), -944);
    CHECK_EQ(camera->camera_type, 12U);
    CHECK_EQ(camera->horizontal_fov_units, 853);
    CHECK_FALSE(record->camera_by_id(999).has_value());
  }

  TEST_CASE("AREA table 5 resolves named addresses by signed ID") {
    constexpr std::size_t k_address_offset{IamAreaRecord::k_header_size};
    std::vector<std::byte> data(k_address_offset + 0x10U, std::byte{});
    write_u32(data, IamAreaRecord::k_offset_script, static_cast<std::uint32_t>(data.size()));
    write_u32(data, IamAreaRecord::k_offset_table_offsets + (5U * 4U), k_address_offset);
    write_u16(data, IamAreaRecord::k_offset_table_counts + (5U * 2U), 1);
    write_i32(data, k_address_offset + 0x00U, 43922);
    write_i32(data, k_address_offset + 0x04U, 2592);
    write_i32(data, k_address_offset + 0x08U, 19656);
    write_i16(data, k_address_offset + 0x0CU, 0);
    write_i16(data, k_address_offset + 0x0EU, 654);

    const auto record{IamAreaRecord::load(data)};
    REQUIRE(record.has_value());
    const auto address{record->address_by_id(654)};
    REQUIRE(address.has_value());
    CHECK_EQ(address->serialized_position.at(0), 43922);
    CHECK_EQ(address->serialized_position.at(1), 2592);
    CHECK_EQ(address->serialized_position.at(2), 19656);
    CHECK_EQ(address->orientation_units, 0);
    CHECK_FALSE(record->address_by_id(999).has_value());
  }

  TEST_CASE("AREA table 2 decodes shared 0x44-byte IAM zone records") {
    constexpr std::size_t k_zone_offset{IamAreaRecord::k_header_size};
    constexpr std::size_t k_zone_stride{0x44};
    std::vector<std::byte> data(k_zone_offset + (2U * k_zone_stride), std::byte{});
    write_u32(data, IamAreaRecord::k_offset_script, static_cast<std::uint32_t>(data.size()));
    write_u32(data, IamAreaRecord::k_offset_table_offsets + (2U * 4U), k_zone_offset);
    write_u16(data, IamAreaRecord::k_offset_table_counts + (2U * 2U), 2);

    write_u32(data, k_zone_offset + 0x00U, 0x10203040U);
    write_u32(data, k_zone_offset + 0x04U, 0x50607080U);
    write_u32(data, k_zone_offset + 0x08U, 0x90A0B0C0U);
    data.at(k_zone_offset + 0x0CU) = std::byte{0x12};
    data.at(k_zone_offset + 0x3DU) = std::byte{0x34};
    write_i16(data, k_zone_offset + 0x3EU, -12);
    write_i16(data, k_zone_offset + 0x40U, 9);
    data.at(k_zone_offset + 0x42U) = std::byte{0x56};
    data.at(k_zone_offset + 0x43U) = std::byte{0x78};

    const std::size_t second{k_zone_offset + k_zone_stride};
    write_u32(data, second + 0x00U, 0xD0E0F001U);
    write_i16(data, second + 0x3EU, 33);
    write_i16(data, second + 0x40U, static_cast<std::int16_t>(0x8005U));

    const auto record{IamAreaRecord::load(data)};
    REQUIRE(record.has_value());
    const std::vector<App::Omikron::IamAreaZoneRecord> zones{record->zones()};
    REQUIRE_EQ(zones.size(), 2U);
    CHECK_EQ(zones.at(0).event_offsets.at(0), 0x10203040U);
    CHECK_EQ(zones.at(0).event_offsets.at(1), 0x50607080U);
    CHECK_EQ(zones.at(0).event_offsets.at(2), 0x90A0B0C0U);
    CHECK_EQ(zones.at(0).raw_geometry_and_fields.at(0), std::byte{0x12});
    CHECK_EQ(zones.at(0).raw_geometry_and_fields.at(0x31U), std::byte{0x34});
    CHECK_EQ(zones.at(0).field_3e, -12);
    CHECK_EQ(zones.at(0).zone_id, 9);
    CHECK_EQ(zones.at(0).raw_tail.at(0), std::byte{0x56});
    CHECK_EQ(zones.at(0).raw_tail.at(1), std::byte{0x78});
    CHECK_EQ(zones.at(1).event_offsets.at(0), 0xD0E0F001U);
    CHECK_EQ(zones.at(1).field_3e, 33);
    CHECK_EQ(static_cast<std::uint16_t>(zones.at(1).zone_id), 0x8005U);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

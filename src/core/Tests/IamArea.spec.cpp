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

/// Builds a 0x9C0 area record matching the recovered area-118 shape.
std::vector<std::byte> make_area_118() {
  std::vector<std::byte> data(0x9C0, std::byte{});
  write_u32(data, IamAreaRecord::k_offset_script, 0x3FC);
  write_name(data, IamAreaRecord::k_offset_model3do_name, "GRID");
  write_name(data, IamAreaRecord::k_offset_scenario_scx_name, "GRID");
  return data;
}

}  // namespace

TEST_SUITE("Core::Omikron::IamAreaRecord") {
  TEST_CASE("A representative area-118 fixture reports size, script offset and names") {
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
    std::vector<std::byte> data(k_definition_offset + 0x114U, std::byte{});

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
    write_u16(data, k_definition_offset + 0x110U, 310);

    const auto record{IamAreaRecord::load(data)};
    REQUIRE(record.has_value());
    const auto placement{record->character_by_id(310)};
    REQUIRE(placement.has_value());
    const auto definition{record->character_definition_by_character_id(310)};
    REQUIRE(definition.has_value());
    CHECK_EQ(definition->name, "KAY'L 669");
    CHECK_EQ(definition->model_resource, "HO1_FNM");
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
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

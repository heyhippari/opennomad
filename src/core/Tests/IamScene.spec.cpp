#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "Core/Omikron/IamScene.hpp"

namespace {

using App::Omikron::IamSceneRecord;

template <typename Value>
void write(std::vector<std::byte>& data, const std::size_t offset, const Value value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void table(std::vector<std::byte>& data,
    const std::size_t index,
    const std::uint32_t offset,
    const std::int16_t count) {
  write(data, IamSceneRecord::k_offset_table_offsets + (index * 4U), offset);
  write(data, IamSceneRecord::k_offset_table_counts + (index * 2U), count);
}

/// One valid record with all recovered nonempty table families represented.
std::vector<std::byte> valid_scene() {
  constexpr std::size_t k_table0{0x44};
  constexpr std::size_t k_table1{k_table0 + 0x14U};
  constexpr std::size_t k_table2{k_table1 + 0x18U};
  constexpr std::size_t k_table3{k_table2 + 0x44U};
  constexpr std::size_t k_table4{k_table3 + 0x18U};
  constexpr std::size_t k_table7{k_table4 + 0x114U};
  constexpr std::size_t k_script{k_table7 + 0x08U};
  constexpr std::size_t k_table6{k_script + 0x02U};
  std::vector<std::byte> data(k_table6 + 0x2CU, std::byte{});

  table(data, 0, k_table0, 1);
  table(data, 1, k_table1, 1);
  table(data, 2, k_table2, 1);
  table(data, 3, k_table3, 1);
  table(data, 4, k_table4, 1);
  table(data, 5, 0, 0);
  table(data, 6, k_table6, 1);
  table(data, 7, k_table7, 1);
  write(data, IamSceneRecord::k_offset_script, static_cast<std::uint32_t>(k_script));

  write(data, k_table0 + 0x00U, static_cast<std::int16_t>(-1));
  write(data, k_table0 + 0x02U, static_cast<std::int16_t>(57));
  write(data, k_table0 + 0x04U, static_cast<std::int32_t>(49457));
  write(data, k_table0 + 0x08U, static_cast<std::int32_t>(-511));
  write(data, k_table0 + 0x0CU, static_cast<std::int32_t>(19386));
  write(data, k_table0 + 0x10U, static_cast<std::int16_t>(4073));

  write(data, k_table2 + 0x00U, static_cast<std::uint32_t>(k_script));
  write(data, k_table2 + 0x40U, static_cast<std::int16_t>(9));
  write(data, k_table4 + 0x10EU, static_cast<std::int16_t>(-1));
  write(data, k_table4 + 0x110U, static_cast<std::int16_t>(57));
  constexpr char k_name[]{"LOCAL CHARACTER"};
  constexpr char k_model[]{"DE1_FN"};
  std::memcpy(data.data() + k_table4 + 0x08U, k_name, sizeof(k_name));
  std::memcpy(data.data() + k_table4 + 0x90U, k_model, sizeof(k_model));
  write(data, k_table4 + 0xA0U, static_cast<std::int16_t>(16));
  write(data, k_table4 + 0xA8U, static_cast<std::int16_t>(20));
  write(data, k_table4 + 0xACU, static_cast<std::uint16_t>(444));
  write(data, k_table7 + 0x00U, static_cast<std::uint32_t>(k_script));
  write(data, k_table7 + 0x04U, static_cast<std::int32_t>(3));
  data.at(k_script) = std::byte{0x57};
  data.at(k_script + 1U) = std::byte{0x03};
  write(data, k_table6 + 0x18U, static_cast<std::int16_t>(2172));
  return data;
}

}  // namespace

TEST_SUITE("Core::Omikron::IamSceneRecord") {
  TEST_CASE("Minimum header represents a no-script empty SCENE") {
    const std::vector<std::byte> data(IamSceneRecord::k_header_size, std::byte{});
    const auto scene{IamSceneRecord::load(data)};
    REQUIRE(scene.has_value());
    CHECK(scene->script_bytes().empty());
  }

  TEST_CASE("Known SCENE table strides are exact") {
    CHECK_EQ(IamSceneRecord::table_stride(0), std::optional<std::size_t>{0x14});
    CHECK_EQ(IamSceneRecord::table_stride(1), std::optional<std::size_t>{0x18});
    CHECK_EQ(IamSceneRecord::table_stride(2), std::optional<std::size_t>{0x44});
    CHECK_EQ(IamSceneRecord::table_stride(3), std::optional<std::size_t>{0x18});
    CHECK_EQ(IamSceneRecord::table_stride(4), std::optional<std::size_t>{0x114});
    CHECK_EQ(IamSceneRecord::table_stride(5), std::nullopt);
    CHECK_EQ(IamSceneRecord::table_stride(6), std::optional<std::size_t>{0x2C});
    CHECK_EQ(IamSceneRecord::table_stride(7), std::optional<std::size_t>{0x08});
  }

  TEST_CASE("Parser retains data and terminates script before cameras") {
    const auto scene{IamSceneRecord::load(valid_scene())};
    REQUIRE(scene.has_value());
    REQUIRE_EQ(scene->script_bytes().size(), 2U);
    CHECK_EQ(scene->script_bytes()[0], std::byte{0x57});  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const auto character{scene->character_by_id(57)};
    REQUIRE(character.has_value());
    CHECK_EQ(character->serialized_position.at(0), 49457);
    CHECK_EQ(character->orientation_units, 4073);
    const auto definition{scene->character_definition_by_character_id(57)};
    REQUIRE(definition.has_value());
    CHECK_EQ(definition->name, "LOCAL CHARACTER");
    CHECK_EQ(definition->model_resource, "DE1_FN");
    CHECK_EQ(definition->initial_values.field_a0, 16);
    CHECK_EQ(definition->initial_values.field_a8, 20);
    CHECK_EQ(definition->initial_values.field_ac, 444);
    const std::vector<App::Omikron::IamSceneZoneRecord> zones{scene->zones()};
    REQUIRE_EQ(zones.size(), 1U);
    CHECK_EQ(zones.front().event_offsets.at(0), scene->script_offset());
    CHECK_EQ(zones.front().zone_id, 9);
    const std::vector<App::Omikron::IamSceneScriptLinkRecord> links{scene->script_links()};
    REQUIRE_EQ(links.size(), 1U);
    CHECK_EQ(links.front().program_offset, scene->script_offset());
    CHECK_EQ(links.front().field_04, 3);
    REQUIRE(scene->camera_by_id(2172).has_value());
  }

  TEST_CASE("Negative counts and out-of-bounds table spans are rejected") {
    auto negative{valid_scene()};
    table(negative, 2, 0, -1);
    const auto negative_scene{IamSceneRecord::load(negative)};
    REQUIRE_FALSE(negative_scene.has_value());
    CHECK(negative_scene.error().find("negative") != std::string::npos);

    auto overflow{valid_scene()};
    table(overflow, 3, static_cast<std::uint32_t>(overflow.size() - 1U), 1);
    const auto overflow_scene{IamSceneRecord::load(overflow)};
    REQUIRE_FALSE(overflow_scene.has_value());
    CHECK(overflow_scene.error().find("span") != std::string::npos);

    auto arithmetic_overflow{valid_scene()};
    table(arithmetic_overflow,
        0,
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::int16_t>::max());
    const auto arithmetic_overflow_scene{IamSceneRecord::load(arithmetic_overflow)};
    REQUIRE_FALSE(arithmetic_overflow_scene.has_value());
    CHECK(arithmetic_overflow_scene.error().find("span") != std::string::npos);
  }

  TEST_CASE("Unsupported table 5 and script after camera table are rejected") {
    auto table5{valid_scene()};
    table(table5, 5, 0x44, 1);
    const auto table5_scene{IamSceneRecord::load(table5)};
    REQUIRE_FALSE(table5_scene.has_value());
    CHECK(table5_scene.error().find("table 5") != std::string::npos);

    auto script{valid_scene()};
    write(script, IamSceneRecord::k_offset_script, static_cast<std::uint32_t>(script.size()));
    const auto script_scene{IamSceneRecord::load(script)};
    REQUIRE_FALSE(script_scene.has_value());
    CHECK(script_scene.error().find("script offset") != std::string::npos);
  }

  TEST_CASE("Table-4 strings and program offsets are bounded and NUL terminated") {
    auto string{valid_scene()};
    constexpr std::size_t k_table4{0xCC};
    write(string, k_table4 + 0x00U, static_cast<std::uint32_t>(string.size() - 1U));
    string.back() = std::byte{'x'};
    const auto string_scene{IamSceneRecord::load(string)};
    REQUIRE_FALSE(string_scene.has_value());
    CHECK(string_scene.error().find("NUL") != std::string::npos);

    auto link{valid_scene()};
    constexpr std::size_t k_table7{0x1E0};
    write(link, k_table7, static_cast<std::uint32_t>(link.size()));
    const auto link_scene{IamSceneRecord::load(link)};
    REQUIRE_FALSE(link_scene.has_value());
    CHECK(link_scene.error().find("script link") != std::string::npos);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

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

void write_u16(std::vector<std::byte>& data, const std::size_t offset, const std::uint16_t value) {
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
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

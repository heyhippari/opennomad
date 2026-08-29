#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/IamStart.hpp"

namespace {

std::uint32_t read_u32(const std::span<const std::byte> data, const std::size_t offset) {
  std::uint32_t value{};
  std::memcpy(&value, data.subspan(offset, sizeof(value)).data(), sizeof(value));
  return value;
}

}  // namespace

TEST_CASE("[RETAIL] IAM/START matches the recovered retail layout") {
  const auto file{App::load_game_file("IAM/START")};
  REQUIRE_MESSAGE(file.has_value(), file.error());
  REQUIRE_EQ(file->bytes.size(), 0x1636U);

  const std::span<const std::byte> bytes{file->bytes};
  CHECK_EQ(read_u32(bytes, 0x08U), 0x058CU);
  CHECK_EQ(read_u32(bytes, 0x0CU), 0x1064U);
  CHECK_EQ(read_u32(bytes, 0x10U), 0x126CU);
  CHECK_EQ(read_u32(bytes, 0x14U), 0x1314U);
  CHECK_EQ(read_u32(bytes, 0x18U), 0x1398U);
  CHECK_EQ(read_u32(bytes, 0x1CU), 0x13FCU);

  const auto start{App::Omikron::IamStart::load(bytes)};
  REQUIRE_MESSAGE(start.has_value(), start.error());
  CHECK_EQ(start->format_revision(), 103U);
  CHECK_EQ(start->build_date(), 19'991'004U);
  CHECK_EQ(start->initial_area_id(), 118);
  CHECK_EQ(start->linked_area_id(), -1);
  CHECK_EQ(start->global_variables()->size(), 694U * sizeof(std::int32_t));
  CHECK_EQ(start->area_mappings()->size(), 260U * sizeof(std::int16_t));
  CHECK_EQ(start->packed_state_bytes()->size(), 168U);
  CHECK_EQ(start->character_flags()->size(), 132U);
  CHECK_EQ(start->address_flags()->size(), 100U);
  CHECK_EQ(start->zone_flags()->size(), 570U);
  CHECK_EQ(start->persistent_object_collection(0)->size(), 18U * sizeof(std::int16_t));
  CHECK_EQ(start->persistent_object_collection(1)->size(), 256U * sizeof(std::int16_t));
  CHECK_EQ(start->persistent_object_collection(2)->size(), 9U * sizeof(std::int16_t));
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)
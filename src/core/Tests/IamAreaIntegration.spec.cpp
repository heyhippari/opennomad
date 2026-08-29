#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/IamArchive.hpp"
#include "Core/Omikron/IamArea.hpp"

namespace {

std::uint32_t read_u32(const std::span<const std::byte> data, const std::size_t offset) {
  std::uint32_t value{};
  std::memcpy(&value, data.subspan(offset, sizeof(value)).data(), sizeof(value));
  return value;
}

}  // namespace

TEST_CASE("[RETAIL] IAM/AREA archive and AREA 118 match recovered facts") {
  const auto file{App::load_game_file("IAM/AREA")};
  REQUIRE_MESSAGE(file.has_value(), file.error());
  REQUIRE_EQ(file->bytes.size(), 0x132000U);

  const App::Omikron::IamIndexedArchive archive{file->bytes};
  for (std::uint32_t id{0}; id <= 258U; ++id) {
    CAPTURE(id);
    CHECK(archive.read_record(id).has_value());
  }
  CHECK_FALSE(archive.read_record(259U).has_value());

  constexpr std::size_t k_area_118_entry{118U * 8U};
  CHECK_EQ(read_u32(file->bytes, k_area_118_entry), 0x8D800U);
  CHECK_EQ(read_u32(file->bytes, k_area_118_entry + 4U), 0x09C0U);

  const auto record_bytes{archive.read_record(118U)};
  REQUIRE_MESSAGE(record_bytes.has_value(), record_bytes.error());
  const auto area{App::Omikron::IamAreaRecord::load(record_bytes.value())};
  REQUIRE_MESSAGE(area.has_value(), area.error());
  CHECK_EQ(area->record_size(), 0x09C0U);
  CHECK_EQ(area->primary_event_offset(), 0x03FCU);
  CHECK_EQ(area->model3do_name(), "GRID");
  CHECK_EQ(area->scenario_scx_name(), "GRID");
  CHECK_EQ(area->map_mpt_name(), "");
  CHECK_EQ(area->options_opt_name(), "");
  CHECK_EQ(area->animation_ani_name(), "");
  CHECK_EQ(area->sky_3do_name(), "");

  constexpr std::array<std::uint32_t, 8> k_table_offsets{
      0x00B4U, 0x00DCU, 0x00DCU, 0x00DCU, 0x00DCU, 0x03ECU, 0x051CU, 0x03FCU};
  constexpr std::array<std::uint16_t, 8> k_table_counts{2U, 0U, 0U, 0U, 2U, 1U, 27U, 0U};
  for (std::size_t index{0}; index < k_table_offsets.size(); ++index) {
    CHECK_EQ(area->table_offset(index), k_table_offsets.at(index));
    CHECK_EQ(area->table_count(index), k_table_counts.at(index));
  }

  CHECK_EQ(area->bytecode_pool_offset(), 0x03FCU);
  CHECK_EQ(area->bytecode_pool().size(), 0x0120U);
  const auto character{area->character_by_id(310)};
  REQUIRE(character.has_value());
  CHECK_EQ(character->serialized_position, std::array<std::int32_t, 3>{-2588, -271, -816});
  CHECK_EQ(character->orientation_units, 4084);
  CHECK(area->character_by_id(136).has_value());
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)
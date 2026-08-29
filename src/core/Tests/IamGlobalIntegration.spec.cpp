#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/IamGlobal.hpp"

namespace {

std::uint32_t read_u32(const std::span<const std::byte> data, const std::size_t offset) {
  std::uint32_t value{};
  std::memcpy(&value, data.subspan(offset, sizeof(value)).data(), sizeof(value));
  return value;
}

std::int16_t read_i16(const std::span<const std::byte> data, const std::size_t offset) {
  std::int16_t value{};
  std::memcpy(&value, data.subspan(offset, sizeof(value)).data(), sizeof(value));
  return value;
}

}  // namespace

TEST_CASE("[RETAIL] IAM/GLOBAL camera subset matches the recovered sample") {
  const auto file{App::load_game_file("IAM/GLOBAL")};
  REQUIRE_MESSAGE(file.has_value(), file.error());
  REQUIRE_EQ(file->bytes.size(), 0x1A68U);
  CHECK_EQ(read_u32(file->bytes, 0x14U), 0x19B8U);
  CHECK_EQ(read_i16(file->bytes, 0x1EU), 4);

  const auto global{App::Omikron::IamGlobal::load(file->bytes)};
  REQUIRE_MESSAGE(global.has_value(), global.error());
  REQUIRE_EQ(global->cameras().size(), 4U);
  constexpr std::array<std::int16_t, 4> k_camera_ids{0, 6, 11, 35};
  for (std::size_t index{0}; index < k_camera_ids.size(); ++index) {
    CHECK_EQ(global->cameras()[index].camera_id, k_camera_ids.at(index));
    CHECK(global->camera_by_id(k_camera_ids.at(index)).has_value());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)
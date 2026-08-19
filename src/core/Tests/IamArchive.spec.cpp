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

#include "Core/Omikron/IamArchive.hpp"

namespace {

using App::Omikron::IamIndexedArchive;

void write_u32(std::vector<std::byte>& data, const std::size_t offset, const std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::vector<std::byte> to_vector(const std::span<const std::byte> bytes) {
  return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

/// Builds a single-page archive whose entry for `id` points at
/// `record_offset`/`record_size`, with the record bytes at that offset.
std::vector<std::byte> make_archive(const std::uint32_t id,
    const std::uint32_t record_offset,
    const std::uint32_t record_size,
    const std::span<const std::byte> record) {
  std::vector<std::byte> data(IamIndexedArchive::k_index_page_size + record.size(), std::byte{});
  const std::size_t entry{
      static_cast<std::size_t>(id & 0xFFU) * IamIndexedArchive::k_index_entry_size};
  write_u32(data, entry, record_offset);
  write_u32(data, entry + 4U, record_size);
  if (!record.empty()) {
    std::memcpy(data.data() + IamIndexedArchive::k_index_page_size, record.data(), record.size());
  }
  return data;
}

}  // namespace

TEST_SUITE("Core::Omikron::IamIndexedArchive") {
  TEST_CASE("Index math for IDs below and above 255") {
    CHECK_EQ(IamIndexedArchive::index_entry_offset(0).value(), 0U);
    CHECK_EQ(IamIndexedArchive::index_entry_offset(118).value(), 118U * 8U);
    CHECK_EQ(IamIndexedArchive::index_entry_offset(255).value(), 255U * 8U);
    CHECK_EQ(IamIndexedArchive::index_entry_offset(256).value(), 0x800U);
    CHECK_EQ(IamIndexedArchive::index_entry_offset(300).value(), 0x800U + (44U * 8U));
  }

  TEST_CASE("Record 118 selects entry 118 of page 0") {
    const std::array<std::byte, 4> record{
        std::byte{0x33}, std::byte{0x22}, std::byte{0x11}, std::byte{0x00}};
    const std::vector<std::byte> data{make_archive(118, 0x800, 4, record)};

    const IamIndexedArchive archive{data};
    const auto result{archive.read_record(118)};
    REQUIRE(result.has_value());
    CHECK_EQ(to_vector(result.value()), std::vector<std::byte>(record.begin(), record.end()));
  }

  TEST_CASE("Absolute offsets and sizes decode little-endian") {
    const std::array<std::byte, 3> record{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
    // A non-page-aligned offset exercises the little-endian decode of the
    // entry's {offset, size} pair (0x0801, 2 -> bytes {0x22, 0x33}).
    const std::vector<std::byte> data{make_archive(3, 0x0801, 2, record)};

    const IamIndexedArchive archive{data};
    const auto result{archive.read_record(3)};
    REQUIRE(result.has_value());
    CHECK_EQ(to_vector(result.value()), std::vector<std::byte>{std::byte{0x22}, std::byte{0x33}});
  }

  TEST_CASE("Truncated index page fails safely") {
    std::vector<std::byte> data(0x100, std::byte{});
    const IamIndexedArchive archive{data};
    const auto result{archive.read_record(118)};
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("118") != std::string::npos);
  }

  TEST_CASE("Out-of-range data range fails safely") {
    const std::array<std::byte, 1> record{std::byte{0x01}};
    const std::vector<std::byte> data{make_archive(118, 0x7FC, 0x1000, record)};
    const IamIndexedArchive archive{data};
    const auto result{archive.read_record(118)};
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("outside") != std::string::npos);
  }

  TEST_CASE("Zero-sized record is reported as absent") {
    const std::vector<std::byte> data{make_archive(118, 0x800, 0, std::span<const std::byte>{})};
    const IamIndexedArchive archive{data};
    const auto result{archive.read_record(118)};
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("absent") != std::string::npos);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic, readability-suspicious-call-argument)

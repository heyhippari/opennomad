#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Core/Omikron/IamObject.hpp"

namespace {

void write_u16(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint16_t value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void write_string(std::vector<std::byte>& bytes, const std::size_t offset, const char* value) {
  std::memcpy(bytes.data() + offset, value, std::strlen(value));
}

std::vector<std::byte> make_object_record(
    const std::uint16_t type, const char* const stem, const char* const subtitle) {
  std::vector<std::byte> record(App::Omikron::IamObjectRecord::k_serialized_size, std::byte{});
  write_u16(record, App::Omikron::IamObjectRecord::k_offset_type, type);
  write_string(record, App::Omikron::IamObjectRecord::k_offset_audio_stem, stem);
  write_string(record, App::Omikron::IamObjectRecord::k_offset_subtitle, subtitle);
  return record;
}

std::vector<std::byte> make_archive(
    const std::uint16_t object_id, const std::vector<std::byte>& record) {
  const std::size_t record_offset{
      static_cast<std::size_t>(object_id) * App::Omikron::IamObjectRecord::k_archive_stride};
  std::vector<std::byte> archive(
      record_offset + App::Omikron::IamObjectRecord::k_archive_stride, std::byte{});
  std::memcpy(archive.data() + record_offset, record.data(), record.size());
  return archive;
}

}  // namespace

TEST_SUITE("Core::Omikron::IamObject") {
  TEST_CASE("reads fixed voice-over fields through Runtime's fixed-stride IAM/OBJECT storage") {
    const std::vector<std::byte> record{make_object_record(
        0U, "ZVO M010 Agression", "You have been victim of a violent attack, Go home...")};
    const auto parsed{
        App::Omikron::IamObjectRecord::load_from_archive(make_archive(141U, record), 141U)};
    REQUIRE(parsed.has_value());
    CHECK_EQ(parsed->object_type(), 0U);
    CHECK_EQ(parsed->audio_stem(), "ZVO M010 Agression");
    CHECK_EQ(parsed->subtitle(), "You have been victim of a violent attack, Go home...");
    CHECK_EQ(parsed->bytes().size(), App::Omikron::IamObjectRecord::k_serialized_size);
  }

  TEST_CASE("high object ID 997 resolves from slot 997 times 0x800") {
    const std::vector<std::byte> record{make_object_record(0U, "", "")};
    const auto parsed{
        App::Omikron::IamObjectRecord::load_from_archive(make_archive(997U, record), 997U)};
    REQUIRE(parsed.has_value());
    CHECK_EQ(parsed->object_type(), 0U);
    CHECK(parsed->audio_stem().empty());
    CHECK(parsed->subtitle().empty());
  }

  TEST_CASE("rejects wrong fixed sizes and unterminated fields") {
    std::vector<std::byte> short_record(0x517U, std::byte{});
    CHECK_FALSE(App::Omikron::IamObjectRecord::load(short_record).has_value());

    std::vector<std::byte> bad_audio{make_object_record(0U, "stem", "subtitle")};
    std::fill(bad_audio.begin() +
                  static_cast<std::ptrdiff_t>(App::Omikron::IamObjectRecord::k_offset_audio_stem),
        bad_audio.begin() +
            static_cast<std::ptrdiff_t>(App::Omikron::IamObjectRecord::k_offset_subtitle),
        std::byte{'x'});
    CHECK_FALSE(App::Omikron::IamObjectRecord::load(bad_audio).has_value());

    std::vector<std::byte> bad_subtitle{make_object_record(0U, "stem", "subtitle")};
    std::fill(bad_subtitle.begin() +
                  static_cast<std::ptrdiff_t>(App::Omikron::IamObjectRecord::k_offset_subtitle),
        bad_subtitle.end(),
        std::byte{'x'});
    CHECK_FALSE(App::Omikron::IamObjectRecord::load(bad_subtitle).has_value());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

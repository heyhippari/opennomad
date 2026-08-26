#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "Core/Omikron/IamCamera.hpp"
#include "Core/Omikron/IamGlobal.hpp"

namespace {

using App::Omikron::IamCameraRecord;
using App::Omikron::IamGlobal;

template <typename Value>
void write_at(std::vector<std::byte>& data, const std::size_t offset, const Value value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

struct CameraFixture {
  std::array<std::int32_t, 3> eye{};
  std::array<std::int32_t, 3> target{};
  std::int16_t camera_id{0};
  std::uint16_t camera_type{12};
  std::int16_t roll_units{0};
  std::int16_t horizontal_fov_units{853};
  std::int16_t target_selector{0};
  std::int16_t eye_selector{0};
  std::array<std::uint16_t, 4> tail{};
};

void write_camera(
    std::vector<std::byte>& data, const std::size_t offset, const CameraFixture& camera) {
  for (std::size_t axis{0}; axis < camera.eye.size(); ++axis) {
    write_at(data, offset + (axis * 4U), camera.eye.at(axis));
    write_at(data, offset + 0x0CU + (axis * 4U), camera.target.at(axis));
  }
  write_at(data, offset + 0x18U, camera.camera_id);
  write_at(data, offset + 0x1AU, camera.camera_type);
  write_at(data, offset + 0x1CU, camera.roll_units);
  write_at(data, offset + 0x1EU, camera.horizontal_fov_units);
  write_at(data, offset + 0x20U, camera.target_selector);
  write_at(data, offset + 0x22U, camera.eye_selector);
  for (std::size_t index{0}; index < camera.tail.size(); ++index) {
    write_at(data, offset + 0x24U + (index * 2U), camera.tail.at(index));
  }
}

std::vector<std::byte> make_global(const std::span<const CameraFixture> cameras,
    const std::size_t camera_offset = IamGlobal::k_minimum_header_size) {
  const std::size_t size{
      camera_offset + (cameras.size() * IamCameraRecord::k_serialized_size)};
  std::vector<std::byte> data(size, std::byte{});
  write_at(data,
      IamGlobal::k_offset_camera_table,
      static_cast<std::uint32_t>(camera_offset));
  write_at(data,
      IamGlobal::k_offset_camera_count,
      static_cast<std::int16_t>(cameras.size()));
  for (std::size_t index{0}; index < cameras.size(); ++index) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) -- span has no at().
    write_camera(data,
        camera_offset + (index * IamCameraRecord::k_serialized_size),
        cameras[index]);
  }
  return data;
}

}  // namespace

TEST_SUITE("Core::Omikron::IamGlobal") {
  TEST_CASE("Accepts a minimum header with no cameras") {
    const std::vector<std::byte> data(IamGlobal::k_minimum_header_size, std::byte{});
    const auto global{IamGlobal::load(data)};
    REQUIRE(global.has_value());
    CHECK(global->cameras().empty());
    CHECK_FALSE(global->camera_by_id(0).has_value());
  }

  TEST_CASE("Parses every shared IAM camera field") {
    const CameraFixture fixture{.eye = {1, -2, 3},
        .target = {-4, 5, -6},
        .camera_id = 42,
        .camera_type = 12,
        .roll_units = -123,
        .horizontal_fov_units = 910,
        .target_selector = 6,
        .eye_selector = -1,
        .tail = {7, 8, 9, 10}};
    const std::array cameras{fixture};
    const auto global{IamGlobal::load(make_global(cameras))};
    REQUIRE(global.has_value());
    REQUIRE_EQ(global->cameras().size(), 1U);
    const IamCameraRecord& camera{global->cameras().front()};
    CHECK_EQ(camera.serialized_eye, fixture.eye);
    CHECK_EQ(camera.serialized_target, fixture.target);
    CHECK_EQ(camera.camera_id, fixture.camera_id);
    CHECK_EQ(camera.camera_type, fixture.camera_type);
    CHECK_EQ(camera.roll_units, fixture.roll_units);
    CHECK_EQ(camera.horizontal_fov_units, fixture.horizontal_fov_units);
    CHECK_EQ(camera.target_attachment_selector, fixture.target_selector);
    CHECK_EQ(camera.eye_attachment_selector, fixture.eye_selector);
    CHECK_EQ(camera.tail_fields, fixture.tail);
  }

  TEST_CASE("Parses the four supplied retail positive-control camera records") {
    const std::array cameras{CameraFixture{.eye = {0, 179, -768},
                                 .target = {0, 99, -4},
                                 .camera_id = 0,
                                 .tail = {0, 8, 16, 0}},
        CameraFixture{.eye = {1, 177, -573},
            .target = {11, 38, 0},
            .camera_id = 6,
            .tail = {4, 6, 6, 0}},
        CameraFixture{.eye = {5, -13, 268},
            .target = {5, -25, 107},
            .camera_id = 11,
            .horizontal_fov_units = 910,
            .target_selector = 1,
            .eye_selector = 1},
        CameraFixture{
            .eye = {10, 105, 288}, .target = {5, 105, -480}, .camera_id = 35}};
    const auto global{IamGlobal::load(make_global(cameras, 0x80U))};
    REQUIRE(global.has_value());
    CHECK_EQ(global->cameras().size(), 4U);
    for (const std::int16_t id :
        {std::int16_t{0}, std::int16_t{6}, std::int16_t{11}, std::int16_t{35}}) {
      CHECK(global->camera_by_id(id).has_value());
    }
    const auto camera_11{global->camera_by_id(11)};
    REQUIRE(camera_11.has_value());
    CHECK_EQ(camera_11->target_attachment_selector, 1);
  }

  TEST_CASE("Rejects malformed header count and camera spans") {
    SUBCASE("truncated header") {
      CHECK_FALSE(IamGlobal::load(std::vector<std::byte>(0x1FU, std::byte{})).has_value());
    }
    SUBCASE("negative count") {
      std::vector<std::byte> data(IamGlobal::k_minimum_header_size, std::byte{});
      write_at(data, IamGlobal::k_offset_camera_count, std::int16_t{-1});
      CHECK_FALSE(IamGlobal::load(data).has_value());
    }
    SUBCASE("offset outside file") {
      std::vector<std::byte> data(IamGlobal::k_minimum_header_size, std::byte{});
      write_at(data, IamGlobal::k_offset_camera_table, std::uint32_t{0x100U});
      write_at(data, IamGlobal::k_offset_camera_count, std::int16_t{1});
      CHECK_FALSE(IamGlobal::load(data).has_value());
    }
    SUBCASE("table truncated") {
      const std::array cameras{CameraFixture{.camera_id = 1}};
      std::vector<std::byte> data{make_global(cameras)};
      write_at(data, IamGlobal::k_offset_camera_count, std::int16_t{2});
      CHECK_FALSE(IamGlobal::load(data).has_value());
    }
    SUBCASE("extreme signed count is overflow safe") {
      std::vector<std::byte> data(IamGlobal::k_minimum_header_size, std::byte{});
      write_at(data, IamGlobal::k_offset_camera_table, std::uint32_t{0x20U});
      write_at(data, IamGlobal::k_offset_camera_count, std::int16_t{32767});
      CHECK_FALSE(IamGlobal::load(data).has_value());
    }
  }

  TEST_CASE("Duplicate lookup returns the first serialized camera") {
    const std::array cameras{CameraFixture{.eye = {1, 2, 3}, .camera_id = 42},
        CameraFixture{.eye = {9, 8, 7}, .camera_id = 42}};
    const auto global{IamGlobal::load(make_global(cameras))};
    REQUIRE(global.has_value());
    const auto camera{global->camera_by_id(42)};
    REQUIRE(camera.has_value());
    CHECK_EQ(camera->serialized_eye.at(0), 1);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)

#include "Core/Omikron/SFX.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

#include "OmikronTestBuffer.hpp"

namespace {

Buffer& definition(Buffer& buffer) {
  return buffer.i32(17)
      .i32(-2)
      .u32(0xABCD1234U)
      .u32(0x76543210U)
      .f32(1.25F)
      .f32(2.5F)
      .f32(3.75F)
      .f32(4.25F)
      .f32(5.5F)
      .f32(6.75F)
      .f32(7.25F)
      .f32(8.5F)
      .u32(0x00123456U)
      .u32(0x00654321U)
      .f32(10.25F)
      .f32(11.5F)
      .f32(12.75F)
      .u16(0xFFFEU)
      .chars("particle", 8U)
      .u8(6U)
      .u8(0xA5U);
}

Buffer& node(Buffer& buffer) {
  return buffer.i32(21)
      .chars("node", 4U)
      .i32(22)
      .i32(23)
      .i32(24)
      .u32(0x11111111U)
      .u32(0x22222222U)
      .f32(25.25F)
      .f32(26.5F)
      .f32(27.75F)
      .i32(28)
      .i32(29)
      .u32(0x33333333U)
      .i32(30)
      .f32(31.25F)
      .f32(32.5F)
      .i32(33)
      .i32(34)
      .u32(0x44444444U);
}

Buffer& point(Buffer& buffer,
    const std::int32_t id,
    const std::int32_t definition_id,
    const float x_coordinate) {
  return buffer.i32(id)
      .i32(definition_id)
      .f32(x_coordinate)
      .f32(x_coordinate + 1.0F)
      .f32(x_coordinate + 2.0F)
      .f32(x_coordinate + 3.0F)
      .i32(id + 40)
      .i32(id + 50)
      .u32(0x55555555U + static_cast<std::uint32_t>(id));
}

std::vector<std::byte> complete_fixture(const std::uint32_t count_high_bits = 0U) {
  Buffer buffer;
  buffer.u32(App::Omikron::k_sfx_magic)
      .u32(count_high_bits | 1U)
      .zeros(0x28U)
      .u32(count_high_bits | 1U)
      .zeros(0x2CU)
      .u32(count_high_bits | 1U);
  definition(buffer);
  buffer.u32(count_high_bits | 1U).zeros(0x10U).u32(count_high_bits | 1U);
  node(buffer);
  buffer.u32(count_high_bits | 2U).i32(70).chars("one", 4U).u32(1U).f32(71.5F);
  point(buffer, 72, 73, 74.0F);
  buffer.i32(80).chars("two", 4U).u32(2U).f32(81.5F);
  point(buffer, 82, 83, 84.0F);
  point(buffer, 92, 93, 94.0F);
  return buffer.data();
}

std::vector<std::byte> bytes(std::initializer_list<std::uint32_t> dwords) {
  Buffer buffer;
  for (const std::uint32_t value : dwords) {
    buffer.u32(value);
  }
  return buffer.data();
}

}  // namespace

TEST_SUITE("Core::Omikron::SFX") {
  TEST_CASE("parses every recovered field at its exact serialized offset") {
    const auto parsed{App::Omikron::SFX::load(complete_fixture())};
    if (!parsed) {
      FAIL(parsed.error());
    }
    REQUIRE(parsed.has_value());
    CHECK(parsed->magic == App::Omikron::k_sfx_magic);
    REQUIRE(parsed->records_a.size() == 1U);
    REQUIRE(parsed->records_b.size() == 1U);
    REQUIRE(parsed->definitions.size() == 1U);
    const App::Omikron::SfxDefinition& value{parsed->definitions.front()};
    CHECK(value.definition_id == 17);
    CHECK(value.sound_id == -2);
    CHECK(value.sprite_id_raw == 0xABCD1234U);
    CHECK(value.sprite_id() == 0x1234U);
    CHECK(value.flags == 0x76543210U);
    CHECK(value.direction.x == doctest::Approx(1.25F));
    CHECK(value.direction.y == doctest::Approx(2.5F));
    CHECK(value.direction.z == doctest::Approx(3.75F));
    CHECK(value.vertical_acceleration == doctest::Approx(4.25F));
    CHECK(value.lifetime == doctest::Approx(5.5F));
    CHECK(value.sound_delay == doctest::Approx(6.75F));
    CHECK(value.emission_delay == doctest::Approx(7.25F));
    CHECK(value.raw_2c == doctest::Approx(8.5F));
    CHECK(value.start_color_rgb == 0x00123456U);
    CHECK(value.end_color_rgb == 0x00654321U);
    CHECK(value.initial_scale == doctest::Approx(10.25F));
    CHECK(value.cone_angle_degrees == doctest::Approx(11.5F));
    CHECK(value.angular_velocity_degrees == doctest::Approx(12.75F));
    CHECK(value.spawn_count == -2);
    CHECK(value.name == "particle");
    CHECK(value.sprite_render_mode == 6U);
    CHECK(value.raw_4f == 0xA5U);

    REQUIRE(parsed->section_d.size() == 1U);
    REQUIRE(parsed->nodes.size() == 1U);
    const App::Omikron::SfxNode& parsed_node{parsed->nodes.front()};
    CHECK(parsed_node.node_id == 21);
    CHECK(parsed_node.label == "node");
    CHECK(parsed_node.trigger_type == 22);
    CHECK(parsed_node.trigger_id == 23);
    CHECK(parsed_node.track_id == 24);
    CHECK(parsed_node.serialized_track_ptr == 0x11111111U);
    CHECK(parsed_node.serialized_point_ptr == 0x22222222U);
    CHECK(parsed_node.serialized_runtime_position.x == doctest::Approx(25.25F));
    CHECK(parsed_node.serialized_runtime_position.y == doctest::Approx(26.5F));
    CHECK(parsed_node.serialized_runtime_position.z == doctest::Approx(27.75F));
    CHECK(parsed_node.anchor_reference_type == 28);
    CHECK(parsed_node.anchor_reference_id == 29);
    CHECK(parsed_node.serialized_anchor_ptr == 0x33333333U);
    CHECK(parsed_node.fixed_definition_id == 30);
    CHECK(parsed_node.startup_delay == doctest::Approx(31.25F));
    CHECK(parsed_node.serialized_elapsed == doctest::Approx(32.5F));
    CHECK(parsed_node.repeat_limit == 33);
    CHECK(parsed_node.serialized_repeat_index == 34);
    CHECK(parsed_node.flags == 0x44444444U);

    REQUIRE(parsed->tracks.size() == 2U);
    CHECK(parsed->tracks.at(0).track_id == 70);
    CHECK(parsed->tracks.at(0).label == "one");
    CHECK(parsed->tracks.at(0).point_count == 1U);
    CHECK(parsed->tracks.at(0).mutable_duration_seed == doctest::Approx(71.5F));
    REQUIRE(parsed->tracks.at(0).points.size() == 1U);
    const App::Omikron::SfxTrackPoint& parsed_point{parsed->tracks.at(0).points.front()};
    CHECK(parsed_point.point_id == 72);
    CHECK(parsed_point.definition_id == 73);
    CHECK(parsed_point.position.x == doctest::Approx(74.0F));
    CHECK(parsed_point.position.y == doctest::Approx(75.0F));
    CHECK(parsed_point.position.z == doctest::Approx(76.0F));
    CHECK(parsed_point.segment_duration == doctest::Approx(77.0F));
    CHECK(parsed_point.reference_type == 112);
    CHECK(parsed_point.reference_id == 122);
    CHECK(parsed_point.serialized_reference_ptr == 0x5555559DU);
    CHECK(parsed->tracks.at(1).track_id == 80);
    CHECK(parsed->tracks.at(1).points.size() == 2U);
    CHECK(parsed->tracks.at(1).points.at(1).point_id == 92);
  }

  TEST_CASE("reads only the low byte of every section count") {
    const auto parsed{App::Omikron::SFX::load(complete_fixture(0xABCD0000U))};
    if (!parsed) {
      FAIL(parsed.error());
    }
    REQUIRE(parsed.has_value());
    CHECK(parsed->raw_count_a == 0xABCD0001U);
    CHECK(parsed->raw_count_b == 0xABCD0001U);
    CHECK(parsed->raw_definition_count == 0xABCD0001U);
    CHECK(parsed->raw_section_d_count == 0xABCD0001U);
    CHECK(parsed->raw_node_count == 0xABCD0001U);
    CHECK(parsed->raw_track_count == 0xABCD0002U);
    CHECK(parsed->tracks.size() == 2U);
  }

  TEST_CASE("accepts the recovered tail being absent after definitions") {
    const auto parsed{App::Omikron::SFX::load(bytes({App::Omikron::k_sfx_magic, 0U, 0U, 0U}))};
    REQUIRE(parsed.has_value());
    CHECK(parsed->nodes.empty());
    CHECK(parsed->tracks.empty());
  }

  TEST_CASE("rejects wrong magic and every truncated fixed section") {
    CHECK_FALSE(App::Omikron::SFX::load(bytes({0x12345678U})).has_value());
    CHECK_FALSE(App::Omikron::SFX::load(bytes({App::Omikron::k_sfx_magic})).has_value());

    Buffer section_a;
    section_a.u32(App::Omikron::k_sfx_magic).u32(1U).zeros(0x27U);
    CHECK_FALSE(App::Omikron::SFX::load(section_a.data()).has_value());

    Buffer section_b;
    section_b.u32(App::Omikron::k_sfx_magic).u32(0U).u32(1U).zeros(0x2BU);
    CHECK_FALSE(App::Omikron::SFX::load(section_b.data()).has_value());

    Buffer definition_bytes;
    definition_bytes.u32(App::Omikron::k_sfx_magic).u32(0U).u32(0U).u32(1U).zeros(0x4FU);
    CHECK_FALSE(App::Omikron::SFX::load(definition_bytes.data()).has_value());

    Buffer section_d;
    section_d.u32(App::Omikron::k_sfx_magic).u32(0U).u32(0U).u32(0U).u32(1U).zeros(0x0FU);
    CHECK_FALSE(App::Omikron::SFX::load(section_d.data()).has_value());

    Buffer node_bytes;
    node_bytes.u32(App::Omikron::k_sfx_magic).u32(0U).u32(0U).u32(0U).u32(0U).u32(1U).zeros(0x4BU);
    CHECK_FALSE(App::Omikron::SFX::load(node_bytes.data()).has_value());
  }

  TEST_CASE("rejects truncated track headers, point overflow, and trailing bytes") {
    Buffer header;
    header.u32(App::Omikron::k_sfx_magic)
        .u32(0U)
        .u32(0U)
        .u32(0U)
        .u32(0U)
        .u32(0U)
        .u32(1U)
        .zeros(0x0FU);
    CHECK_FALSE(App::Omikron::SFX::load(header.data()).has_value());

    Buffer points;
    points.u32(App::Omikron::k_sfx_magic)
        .u32(0U)
        .u32(0U)
        .u32(0U)
        .u32(0U)
        .u32(0U)
        .u32(1U)
        .i32(1)
        .chars("trk", 4U)
        .u32(0xFFFFFFFFU)
        .f32(0.0F);
    CHECK_FALSE(App::Omikron::SFX::load(points.data()).has_value());

    auto trailing{complete_fixture()};
    trailing.push_back(std::byte{0x7F});
    CHECK_FALSE(App::Omikron::SFX::load(trailing).has_value());
  }
}

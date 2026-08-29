#include "Core/Omikron/ThreeDM.hpp"

#include <doctest/doctest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Core/Dialog/DialogPerformanceRuntime.hpp"
#include "Core/Omikron/Model3DO.hpp"

namespace {

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value) {
  for (std::size_t shift{0}; shift < 32U; shift += 8U) {
    bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
  }
}

void append_f32(std::vector<std::byte>& bytes, const float value) {
  append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

void append_quaternion(std::vector<std::byte>& bytes, const float component_w) {
  append_f32(bytes, component_w);
  append_f32(bytes, 0.0F);
  append_f32(bytes, 0.0F);
  append_f32(bytes, 0.0F);
}

std::vector<std::byte> header(const std::uint32_t audio_bytes,
    const std::uint32_t morph_count,
    const std::uint32_t field_08,
    const std::span<const std::uint32_t> object_ids,
    const std::uint8_t mode = 0U) {
  std::vector<std::byte> bytes;
  append_u32(bytes, (static_cast<std::uint32_t>(mode) << 24U) | audio_bytes);
  append_u32(bytes, morph_count);
  append_u32(bytes, field_08);
  append_u32(bytes, static_cast<std::uint32_t>(object_ids.size()));
  for (const std::uint32_t id : object_ids) {
    append_u32(bytes, id);
  }
  return bytes;
}

void append_frame_root_slot_2(std::vector<std::byte>& bytes, const float root_x) {
  append_quaternion(bytes, 2.0F);
  append_quaternion(bytes, 3.0F);
  append_f32(bytes, root_x);
  append_f32(bytes, 20.0F);
  append_f32(bytes, 30.0F);
  append_quaternion(bytes, 4.0F);
  append_quaternion(bytes, 5.0F);
}

}  // namespace

TEST_CASE("3DM parses packed header and derives physical frames from EOF") {
  const std::vector<std::uint32_t> ids{10U, 20U, 30U, 40U};
  auto bytes{header(2U, 0U, 9999U, ids)};
  append_frame_root_slot_2(bytes, 10.0F);
  bytes.push_back(std::byte{0x12});
  bytes.push_back(std::byte{0x34});
  append_frame_root_slot_2(bytes, 11.0F);  // Valid final motion-only frame.

  const auto clip{App::Omikron::ThreeDM::load(bytes)};
  REQUIRE(clip.has_value());
  CHECK_EQ(clip->header().stream_mode, 0U);
  CHECK_EQ(clip->header().audio_bytes_per_frame, 2U);
  CHECK_EQ(clip->header().field_08, 9999U);
  CHECK_EQ(clip->frames().size(), 2U);
  CHECK_EQ(clip->audio_chunk_count(), 1U);
  CHECK_EQ(clip->audio_chunk(0U).size(), 2U);
  CHECK_EQ(std::to_integer<std::uint8_t>(clip->audio_chunk(0U).front()), 0x12U);
  CHECK_EQ(std::to_integer<std::uint8_t>(clip->audio_chunk(0U).back()), 0x34U);
  CHECK(clip->audio_chunk(1U).empty());

  const auto frame{clip->decode_frame(0U, 2U)};
  REQUIRE(frame.has_value());
  CHECK_EQ(frame->root_translation.x, 10.0F);
  CHECK_EQ(frame->object_rotations.at(0).w, doctest::Approx(1.0F));
  CHECK_EQ(frame->object_rotations.at(2).w, doctest::Approx(1.0F));
}

TEST_CASE("3DM root slot zero reads translation before its WXYZ quaternion") {
  const std::vector<std::uint32_t> ids{30U, 10U};
  auto bytes{header(0U, 0U, 1U, ids)};
  append_f32(bytes, 4.0F);
  append_f32(bytes, 5.0F);
  append_f32(bytes, 6.0F);
  append_quaternion(bytes, 2.0F);
  append_quaternion(bytes, 3.0F);
  const auto clip{App::Omikron::ThreeDM::load(bytes)};
  REQUIRE(clip.has_value());
  const auto frame{clip->decode_frame(0U, 0U)};
  REQUIRE(frame.has_value());
  CHECK_EQ(frame->root_translation.y, 5.0F);
  CHECK_EQ(frame->object_rotations.at(0).w, doctest::Approx(1.0F));
}

TEST_CASE("3DM preserves one-to-one morph position and normal order") {
  const std::vector<std::uint32_t> ids{30U};
  auto bytes{header(0U, 2U, 0U, ids)};
  append_f32(bytes, 1.0F);
  append_f32(bytes, 2.0F);
  append_f32(bytes, 3.0F);
  append_quaternion(bytes, 1.0F);
  for (std::uint32_t value{10U}; value < 22U; ++value) {
    append_f32(bytes, static_cast<float>(value));
  }
  const auto clip{App::Omikron::ThreeDM::load(bytes)};
  REQUIRE(clip.has_value());
  const auto frame{clip->decode_frame(0U, 0U)};
  REQUIRE(frame.has_value());
  REQUIRE_EQ(frame->morph_vertices.size(), 2U);
  CHECK_EQ(frame->morph_vertices.at(0).position.x, 10.0F);
  CHECK_EQ(frame->morph_vertices.at(0).normal.z, 15.0F);
  CHECK_EQ(frame->morph_vertices.at(1).position.x, 16.0F);
  CHECK_EQ(frame->morph_vertices.at(1).normal.z, 21.0F);
}

TEST_CASE("3DM rejects a zero quaternion during bounded frame decoding") {
  const std::vector<std::uint32_t> ids{30U};
  auto bytes{header(0U, 0U, 0U, ids)};
  append_f32(bytes, 0.0F);
  append_f32(bytes, 0.0F);
  append_f32(bytes, 0.0F);
  append_quaternion(bytes, 0.0F);
  const auto clip{App::Omikron::ThreeDM::load(bytes)};
  REQUIRE(clip.has_value());
  CHECK_FALSE(clip->decode_frame(0U, 0U).has_value());
}

TEST_CASE("3DM rejects unsupported, excessive, truncated and partial streams") {
  const std::vector<std::uint32_t> no_ids;
  CHECK_FALSE(App::Omikron::ThreeDM::load(header(0U, 0U, 0U, no_ids, 1U)).has_value());
  CHECK_FALSE(App::Omikron::ThreeDM::load(header(0U, 201U, 0U, no_ids)).has_value());

  auto too_many{header(0U, 0U, 0U, no_ids)};
  too_many.resize(12U);
  append_u32(too_many, 31U);
  CHECK_FALSE(App::Omikron::ThreeDM::load(too_many).has_value());

  auto truncated{header(0U, 0U, 0U, no_ids)};
  truncated.resize(12U);
  append_u32(truncated, 1U);
  CHECK_FALSE(App::Omikron::ThreeDM::load(truncated).has_value());

  auto partial{header(1U, 0U, 0U, no_ids)};
  partial.push_back(std::byte{0});
  CHECK_FALSE(App::Omikron::ThreeDM::load(partial).has_value());
}

TEST_CASE("3DM binds authored IDs to model script IDs without ordinal fallback") {
  const std::vector<std::uint32_t> ids{10U, 20U, 30U, 40U};
  auto bytes{header(0U, 0U, 0U, ids)};
  append_frame_root_slot_2(bytes, 0.0F);
  const auto clip{App::Omikron::ThreeDM::load(bytes)};
  REQUIRE(clip.has_value());

  App::Omikron::Model3DOData model;
  model.meshes.resize(4U);
  model.meshes.at(0).script_id = 40U;
  model.meshes.at(1).script_id = 10U;
  model.meshes.at(2).script_id = 30U;
  model.meshes.at(3).script_id = 20U;
  model.root_mesh_index = 2;
  const auto binding{App::Dialog::bind_three_dm(clip.value(), model)};
  REQUIRE(binding.has_value());
  CHECK_EQ(binding->object_mesh_indices, std::vector<std::size_t>({1U, 3U, 2U, 0U}));
  CHECK_EQ(binding->root_object_slot, 2U);
  CHECK_FALSE(binding->face_mesh_index.has_value());
}

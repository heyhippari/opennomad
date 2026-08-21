#include "Core/Omikron/Animation3DA.hpp"
#include "Core/Omikron/Path3DP.hpp"
#include "Core/RuntimeMath.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

#include "OmikronTestBuffer.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

TEST_SUITE("Core::Omikron::BodyAnimationResources") {
  TEST_CASE("3DA parses channels and relative sample streams") {
    constexpr std::uint32_t descriptors_end{8U + (2U * 0x28U)};
    constexpr std::uint32_t root_rotation_offset{descriptors_end + 24U};
    constexpr std::uint32_t child_rotation_offset{root_rotation_offset + 32U};
    Buffer payload;
    payload.u32(3).u32(2);
    payload.u32(7)
        .chars("Root", 20)
        .u32(2)
        .u32(descriptors_end)
        .u32(2)
        .u32(root_rotation_offset);
    payload.u32(9)
        .chars("Child", 20)
        .u32(4)
        .u32(0)
        .u32(1)
        .u32(child_rotation_offset);
    payload.f32(0.0F).f32(2.0F).f32(4.0F);
    payload.f32(10.0F).f32(12.0F).f32(14.0F);
    payload.f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
    payload.f32(0.0F).f32(0.0F).f32(0.0F).f32(1.0F);
    payload.f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);

    const auto animation{App::Omikron::Animation3DA::load(payload.data())};
    REQUIRE(animation.has_value());
    CHECK_EQ(animation->max_frame_index, 3U);
    REQUIRE_EQ(animation->channels.size(), std::size_t{2});
    const App::Omikron::Animation3DAChannel& root{animation->channels.at(0)};
    CHECK_EQ(root.channel_id, 7U);
    CHECK_EQ(root.name, "Root");
    const auto translation{root.sample_translation(0.5F)};
    REQUIRE(translation.has_value());
    const App::Runtime::Vec3 translation_value{translation.value_or(App::Runtime::Vec3{})};
    CHECK_EQ(translation_value.x, doctest::Approx(5.0F));
    CHECK_EQ(translation_value.y, doctest::Approx(7.0F));
    CHECK_EQ(translation_value.z, doctest::Approx(9.0F));
    const auto rotation{root.sample_rotation(0.0F)};
    REQUIRE(rotation.has_value());
    const App::Runtime::Quaternion rotation_value{
        rotation.value_or(App::Runtime::Quaternion{})};
    CHECK_EQ(rotation_value.w, doctest::Approx(0.0F));
    CHECK_EQ(rotation_value.z, doctest::Approx(1.0F));

    const App::Omikron::Animation3DAChannel& child{animation->channels.at(1)};
    CHECK_EQ(child.translation_sample_count, 4U);
    CHECK(child.translations.empty());
    REQUIRE_EQ(child.rotations.size(), std::size_t{1});
    CHECK_EQ(child.rotations.at(0).w, doctest::Approx(1.0F));
    CHECK_EQ(child.rotations.at(0).x, doctest::Approx(0.0F));
    CHECK_EQ(child.rotations.at(0).y, doctest::Approx(0.0F));
    CHECK_EQ(child.rotations.at(0).z, doctest::Approx(0.0F));
    CHECK(animation->channel_by_id(9) == &child);
  }

  TEST_CASE("3DA rejects out-of-bounds non-null streams") {
    Buffer payload;
    payload.u32(1).u32(1).u32(2).chars("Bad", 20).u32(2).u32(999).u32(0).u32(0);
    const auto animation{App::Omikron::Animation3DA::load(payload.data())};
    CHECK_FALSE(animation.has_value());
  }

  TEST_CASE("3DP parses named subpaths and mode-1 interpolation") {
    Buffer payload;
    payload.u32(2);
    payload.chars("First", 20).u32(2).u32(2);
    payload.u32(0).f32(0.0F).f32(2.0F).f32(4.0F).f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
    payload.u32(2).f32(10.0F).f32(12.0F).f32(14.0F).f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
    payload.chars("Second", 20).u32(77).u32(1);
    payload.u32(5).f32(-1.0F).f32(-2.0F).f32(-3.0F).f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);

    const auto path{App::Omikron::Path3DP::load(payload.data())};
    REQUIRE(path.has_value());
    REQUIRE_EQ(path->subpaths.size(), std::size_t{2});
    CHECK_EQ(path->subpaths.at(0).name, "First");
    CHECK_EQ(path->subpaths.at(1).field_14, 77U);
    CHECK_EQ(path->subpaths.at(0).points.at(1).key, 2U);
    const auto sample{path->subpaths.at(0).sample_mode_1(1.0F)};
    REQUIRE(sample.has_value());
    CHECK_EQ(sample->position.x, doctest::Approx(5.0F));
    CHECK_EQ(sample->position.y, doctest::Approx(7.0F));
    CHECK_EQ(sample->position.z, doctest::Approx(9.0F));
    CHECK_EQ(sample->quaternion.w, doctest::Approx(1.0F));
  }

  TEST_CASE("3DP rejects malformed counts and truncated points") {
    Buffer huge;
    huge.u32(1).chars("Huge", 20).u32(2).u32(0xFFFF'FFFFU);
    CHECK_FALSE(App::Omikron::Path3DP::load(huge.data()).has_value());

    Buffer truncated;
    truncated.u32(1).chars("Short", 20).u32(2).u32(1).u32(0).f32(1.0F);
    CHECK_FALSE(App::Omikron::Path3DP::load(truncated.data()).has_value());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

#include "Core/Character/SecondarySupportQuery.hpp"

#include <doctest/doctest.h>

// NOLINTBEGIN(bugprone-unchecked-optional-access)

namespace {

App::Omikron::Model3DOData floor_model(const std::uint32_t flags = 0U) {
  App::Omikron::Model3DOData model;
  model.meshes.push_back(App::Omikron::MeshDescriptor{.flags = flags, .vertex_count = 3});
  model.polygons.push_back(App::Omikron::MeshPolygons{
      .triangles = {App::Omikron::Triangle{.vertices = {{{.index = 0}, {.index = 1}, {.index = 2}}},
          .face_normal = {.x = 0.0F, .y = -1.0F, .z = 0.0F}}}});
  model.vertices = {{.position = {.x = -20.0F, .z = -20.0F}},
      {.position = {.x = 20.0F, .z = -20.0F}},
      {.position = {.x = 0.0F, .z = 20.0F}}};
  model.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{
      .world_translation = {.x = 0.0F, .y = 20.0F, .z = 0.0F}});
  return model;
}

}  // namespace

TEST_SUITE("Core::Character::SecondarySupportQuery") {
  TEST_CASE("returns nearest static support distance normal and object") {
    const auto model{floor_model()};
    const auto hit{App::Character::SecondarySupportQuery::find(
        model, model.runtime_objects, {.x = 0.0F, .y = 5.0F, .z = 0.0F})};

    REQUIRE(hit.has_value());
    CHECK_EQ(hit->object_index, 0U);
    CHECK(hit->distance == doctest::Approx(15.0F));
    CHECK(hit->world_normal.y == doctest::Approx(-1.0F));
  }

  TEST_CASE("includes transformed and primary-special objects") {
    for (const std::uint32_t flags : {0x00080000U, 0x20000000U}) {
      const auto model{floor_model(flags)};
      CHECK(App::Character::SecondarySupportQuery::find(
          model, model.runtime_objects, {.x = 0.0F, .y = 5.0F, .z = 0.0F})
              .has_value());
    }
  }

  TEST_CASE("skips both native mask bits") {
    for (const std::uint32_t flags : {0x1U, 0x40U, 0x41U}) {
      const auto model{floor_model(flags)};
      CHECK_FALSE(App::Character::SecondarySupportQuery::find(
          model, model.runtime_objects, {.x = 0.0F, .y = 5.0F, .z = 0.0F})
              .has_value());
    }
  }

  TEST_CASE("uses current transformed object translation") {
    auto model{floor_model(0x00080000U)};
    model.runtime_objects.front().world_translation.y = 35.0F;

    const auto hit{App::Character::SecondarySupportQuery::find(
        model, model.runtime_objects, {.x = 0.0F, .y = 5.0F, .z = 0.0F})};

    REQUIRE(hit.has_value());
    CHECK(hit->distance == doctest::Approx(30.0F));
  }
}

// NOLINTEND(bugprone-unchecked-optional-access)

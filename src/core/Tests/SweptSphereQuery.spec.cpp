#include "Core/Character/SweptSphereQuery.hpp"

#include <doctest/doctest.h>

// NOLINTBEGIN(bugprone-unchecked-optional-access)

#include <cmath>
#include <numbers>

namespace {

App::Omikron::Model3DOData floor_model(const bool quad = false) {
  App::Omikron::Model3DOData model;
  model.meshes.push_back(App::Omikron::MeshDescriptor{.vertex_count = quad ? 4U : 3U});
  if (quad) {
    model.polygons.push_back(
        App::Omikron::MeshPolygons{.rectangles = {App::Omikron::Rectangle{.vertices = {0, 1, 2, 3},
                                       .face_normal = {.x = 0.0F, .y = -1.0F, .z = 0.0F}}}});
    model.vertices = {{.position = {.x = -10.0F, .z = -10.0F}},
        {.position = {.x = 10.0F, .z = -10.0F}},
        {.position = {.x = 10.0F, .z = 10.0F}},
        {.position = {.x = -10.0F, .z = 10.0F}}};
  } else {
    model.polygons.push_back(
        App::Omikron::MeshPolygons{.triangles = {App::Omikron::Triangle{
                                       .vertices = {{{.index = 0}, {.index = 1}, {.index = 2}}},
                                       .face_normal = {.x = 0.0F, .y = -1.0F, .z = 0.0F}}}});
    model.vertices = {{.position = {.x = -10.0F, .z = -10.0F}},
        {.position = {.x = 10.0F, .z = -10.0F}},
        {.position = {.x = 0.0F, .z = 10.0F}}};
  }
  model.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{});
  return model;
}

App::Character::SweptSphereQueryInput downward(
    const App::Runtime::Vec3 start, const float radius = 2.0F) {
  return {.start = start,
      .direction = {.x = 0.0F, .y = 1.0F, .z = 0.0F},
      .max_distance = 100.0F,
      .radius = radius};
}

}  // namespace

TEST_SUITE("Core::Character::SweptSphereQuery") {
  TEST_CASE("finds the earliest transformed face contact") {
    auto model{floor_model()};
    model.runtime_objects.front().world_translation.y = 20.0F;

    const auto hit{App::Character::SweptSphereQuery::find_in_object(
        model, model.runtime_objects, 0U, downward({.x = 0.0F, .y = 5.0F, .z = 0.0F}))};

    REQUIRE(hit.has_value());
    CHECK(hit->travel_distance == doctest::Approx(13.0F));
    CHECK(hit->world_point.y == doctest::Approx(20.0F));
    CHECK(hit->world_normal.y == doctest::Approx(-1.0F));
  }

  TEST_CASE("retains exact starting contact and slight face overlap") {
    const auto model{floor_model()};
    for (const float start_y : {-2.0F, -1.5F}) {
      const auto hit{App::Character::SweptSphereQuery::find_in_object(
          model, model.runtime_objects, 0U, downward({.x = 0.0F, .y = start_y, .z = 0.0F}))};
      REQUIRE(hit.has_value());
      CHECK_EQ(hit->travel_distance, 0.0F);
      CHECK(hit->world_normal.y == doctest::Approx(-1.0F));
    }
  }

  TEST_CASE("finite sphere catches an outer edge beyond the face interior") {
    const auto model{floor_model(true)};

    const auto hit{App::Character::SweptSphereQuery::find_in_object(
        model, model.runtime_objects, 0U, downward({.x = 11.0F, .y = -5.0F, .z = 0.0F}))};

    REQUIRE(hit.has_value());
    CHECK(hit->world_point.x == doctest::Approx(10.0F));
    CHECK(hit->world_normal.x > 0.0F);
    CHECK(hit->travel_distance > 3.0F);
    CHECK(hit->travel_distance < 5.0F);
  }

  TEST_CASE("finite sphere catches a polygon vertex") {
    const auto model{floor_model(true)};

    const auto hit{App::Character::SweptSphereQuery::find_in_object(
        model, model.runtime_objects, 0U, downward({.x = 11.0F, .y = -5.0F, .z = 11.0F}))};

    REQUIRE(hit.has_value());
    CHECK(hit->world_point.x == doctest::Approx(10.0F));
    CHECK(hit->world_point.z == doctest::Approx(10.0F));
    CHECK(hit->world_normal.x > 0.0F);
    CHECK(hit->world_normal.z > 0.0F);
  }

  TEST_CASE("uses runtime rotation and inverse-scale normal transformation") {
    auto model{floor_model()};
    constexpr float angle{30.0F * std::numbers::pi_v<float> / 180.0F};
    model.polygons.front().triangles.front().face_normal = {.x = 1.0F, .y = -1.0F};
    model.runtime_objects.front().scale = {.x = 2.0F, .y = 1.0F, .z = 1.0F};
    model.runtime_objects.front().world_matrix.values = {std::cos(angle),
        -std::sin(angle),
        0.0F,
        std::sin(angle),
        std::cos(angle),
        0.0F,
        0.0F,
        0.0F,
        1.0F};

    const auto hit{App::Character::SweptSphereQuery::find_in_object(
        model, model.runtime_objects, 0U, downward({.x = 0.0F, .y = -20.0F, .z = 0.0F}))};

    REQUIRE(hit.has_value());
    CHECK(hit->world_normal.x == doctest::Approx(-0.0599153F));
    CHECK(hit->world_normal.y == doctest::Approx(-0.9982035F));
  }

  TEST_CASE("quad uses only authored outer features") {
    const auto model{floor_model(true)};
    const auto hit{App::Character::SweptSphereQuery::find_in_object(
        model, model.runtime_objects, 0U, downward({.x = 0.0F, .y = -5.0F, .z = 0.0F}, 0.25F))};
    REQUIRE(hit.has_value());
    CHECK(hit->world_normal.y == doctest::Approx(-1.0F));
  }
}

// NOLINTEND(bugprone-unchecked-optional-access)

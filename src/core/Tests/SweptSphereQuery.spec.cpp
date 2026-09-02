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

void add_ceiling(
    App::Omikron::Model3DOData& model, const float authored_y, const std::uint32_t flags = 0U) {
  const std::uint32_t vertex_base{static_cast<std::uint32_t>(model.vertices.size())};
  model.meshes.push_back(
      App::Omikron::MeshDescriptor{.flags = flags, .vertex_count = 4, .vertex_base = vertex_base});
  model.polygons.push_back(
      App::Omikron::MeshPolygons{.rectangles = {App::Omikron::Rectangle{.vertices = {0, 1, 2, 3},
                                     .face_normal = {.x = 0.0F, .y = 1.0F, .z = 0.0F}}}});
  model.vertices.insert(model.vertices.end(),
      {{.position = {.x = -20.0F, .y = authored_y, .z = -20.0F}},
          {.position = {.x = -20.0F, .y = authored_y, .z = 20.0F}},
          {.position = {.x = 20.0F, .y = authored_y, .z = 20.0F}},
          {.position = {.x = 20.0F, .y = authored_y, .z = -20.0F}}});
  model.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{});
}

App::Character::SweptSphereQueryInput upward(const App::Runtime::Vec3 start = {}) {
  return {.start = start,
      .direction = {.x = 0.0F, .y = -1.0F, .z = 0.0F},
      .max_distance = 1000.0F,
      .radius = 2.0F};
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

  TEST_CASE("world scan selects nearest eligible object and preserves stable ties") {
    App::Omikron::Model3DOData model;
    add_ceiling(model, -40.0F);
    add_ceiling(model, -20.0F);

    const auto nearest{
        App::Character::SweptSphereQuery::find(model, model.runtime_objects, upward(), 0x41U)};
    REQUIRE(nearest.has_value());
    CHECK_EQ(nearest->object_index, 1U);
    CHECK(nearest->travel_distance == doctest::Approx(18.0F));

    model.runtime_objects.front().world_translation.y = 20.0F;
    const auto tied{
        App::Character::SweptSphereQuery::find(model, model.runtime_objects, upward(), 0x41U)};
    REQUIRE(tied.has_value());
    CHECK_EQ(tied->object_index, 0U);
  }

  TEST_CASE("world scan applies only its caller-provided ceiling mask") {
    for (const std::uint32_t included_flags : {0U, 0x00080000U, 0x20000000U}) {
      App::Omikron::Model3DOData model;
      add_ceiling(model, -20.0F, included_flags);
      CHECK(App::Character::SweptSphereQuery::find(model, model.runtime_objects, upward(), 0x41U)
              .has_value());
    }
    for (const std::uint32_t excluded_flags : {0x1U, 0x40U, 0x41U}) {
      App::Omikron::Model3DOData model;
      add_ceiling(model, -20.0F, excluded_flags);
      CHECK_FALSE(
          App::Character::SweptSphereQuery::find(model, model.runtime_objects, upward(), 0x41U)
              .has_value());
    }
  }

  TEST_CASE("world scan skips a closer excluded object and follows runtime translation") {
    App::Omikron::Model3DOData model;
    add_ceiling(model, -10.0F, 0x1U);
    add_ceiling(model, 0.0F, 0x00080000U);
    model.runtime_objects.at(1).world_translation.y = -30.0F;

    const auto hit{
        App::Character::SweptSphereQuery::find(model, model.runtime_objects, upward(), 0x41U)};

    REQUIRE(hit.has_value());
    CHECK_EQ(hit->object_index, 1U);
    CHECK(hit->travel_distance == doctest::Approx(28.0F));
    CHECK(hit->world_point.y == doctest::Approx(-30.0F));
    CHECK(hit->world_normal.y == doctest::Approx(1.0F));
  }

  TEST_CASE("upward world scan retains starting ceiling touch and slight overlap") {
    for (const float ceiling_y : {-2.0F, -1.5F}) {
      App::Omikron::Model3DOData model;
      add_ceiling(model, ceiling_y);
      const auto hit{
          App::Character::SweptSphereQuery::find(model, model.runtime_objects, upward(), 0x41U)};
      REQUIRE(hit.has_value());
      CHECK_EQ(hit->travel_distance, 0.0F);
      CHECK(hit->world_normal.y == doctest::Approx(1.0F));
    }
  }

  TEST_CASE("upward world scan retains authored ceiling edge and vertex features") {
    App::Omikron::Model3DOData model;
    add_ceiling(model, -20.0F);

    const auto edge{App::Character::SweptSphereQuery::find(
        model, model.runtime_objects, upward({.x = 21.0F}), 0x41U)};
    REQUIRE(edge.has_value());
    CHECK(edge->world_point.x == doctest::Approx(20.0F));
    CHECK(edge->world_normal.x > 0.0F);

    const auto vertex{App::Character::SweptSphereQuery::find(
        model, model.runtime_objects, upward({.x = 21.0F, .z = 21.0F}), 0x41U)};
    REQUIRE(vertex.has_value());
    CHECK(vertex->world_point.x == doctest::Approx(20.0F));
    CHECK(vertex->world_point.z == doctest::Approx(20.0F));
    CHECK(vertex->world_normal.x > 0.0F);
    CHECK(vertex->world_normal.z > 0.0F);
  }

  TEST_CASE("upward world scan uses rotated non-uniformly scaled class-two geometry") {
    App::Omikron::Model3DOData model;
    model.meshes.push_back(App::Omikron::MeshDescriptor{.flags = 0x00080000U, .vertex_count = 4});
    model.polygons.push_back(
        App::Omikron::MeshPolygons{.rectangles = {App::Omikron::Rectangle{
                                       .vertices = {0, 1, 2, 3}, .face_normal = {.x = 1.0F}}}});
    model.vertices = {{.position = {.y = -20.0F, .z = -20.0F}},
        {.position = {.y = 20.0F, .z = -20.0F}},
        {.position = {.y = 20.0F, .z = 20.0F}},
        {.position = {.y = -20.0F, .z = 20.0F}}};
    model.runtime_objects.push_back(
        App::Omikron::Model3DOData::RuntimeObjectState{.scale = {.x = 2.0F, .y = 3.0F, .z = 1.0F},
            .world_matrix = {.values = {0.0F, 1.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F}},
            .world_translation = {.y = -30.0F}});

    const auto hit{
        App::Character::SweptSphereQuery::find(model, model.runtime_objects, upward(), 0x41U)};

    REQUIRE(hit.has_value());
    CHECK(hit->travel_distance == doctest::Approx(28.0F));
    CHECK(hit->world_point.y == doctest::Approx(-30.0F));
    CHECK(hit->world_normal.x == doctest::Approx(0.0F));
    CHECK(hit->world_normal.y == doctest::Approx(1.0F));
  }
}

// NOLINTEND(bugprone-unchecked-optional-access)

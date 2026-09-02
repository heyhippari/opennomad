#include "Core/Character/StaticSupportQuery.hpp"

#include <doctest/doctest.h>

namespace {

App::Omikron::Model3DOData flat_triangle_model() {
  App::Omikron::Model3DOData model;
  model.meshes.push_back(App::Omikron::MeshDescriptor{.vertex_count = 3});
  model.polygons.push_back(App::Omikron::MeshPolygons{
      .triangles = {App::Omikron::Triangle{.vertices = {{{.index = 0}, {.index = 1}, {.index = 2}}},
          .face_normal = {.x = 0.0F, .y = -1.0F, .z = 0.0F}}}});
  model.vertices = {{.position = {.x = -10.0F, .y = 0.0F, .z = -10.0F}},
      {.position = {.x = 10.0F, .y = 0.0F, .z = -10.0F}},
      {.position = {.x = 0.0F, .y = 0.0F, .z = 10.0F}}};
  model.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{
      .world_translation = {.x = 0.0F, .y = 20.0F, .z = 0.0F}});
  return model;
}

App::Character::StaticSupportQueryInput query_at(
    const float x = 0.0F, const float y = 5.0F, const float z = 0.0F, const float radius = 4.0F) {
  return {.world_probe = {.x = x, .y = y, .z = z}, .radius = radius};
}

}  // namespace

TEST_SUITE("Core::Character::StaticSupportQuery") {
  TEST_CASE("finds a translated flat triangle beneath the probe") {
    const App::Omikron::Model3DOData model{flat_triangle_model()};

    const auto hit{App::Character::StaticSupportQuery::find(model,
        model.runtime_objects,
        {.world_probe = {.x = 0.0F, .y = 5.0F, .z = 0.0F}, .radius = 4.0F})};

    REQUIRE(hit.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const App::Character::StaticSupportHit& value{hit.value()};
    CHECK_EQ(value.object_index, 0U);
    CHECK(value.world_point.x == doctest::Approx(0.0F));
    CHECK(value.world_point.y == doctest::Approx(20.0F));
    CHECK(value.world_point.z == doctest::Approx(0.0F));
    CHECK(value.world_normal.y == doctest::Approx(-1.0F));
    CHECK(value.clearance == doctest::Approx(11.0F));
  }

  TEST_CASE("finds a quad and includes edge and corner boundaries") {
    App::Omikron::Model3DOData model;
    model.meshes.push_back(App::Omikron::MeshDescriptor{.vertex_count = 4});
    model.polygons.push_back(
        App::Omikron::MeshPolygons{.rectangles = {App::Omikron::Rectangle{.vertices = {0, 1, 2, 3},
                                       .face_normal = {.x = 0.0F, .y = -1.0F, .z = 0.0F}}}});
    model.vertices = {{.position = {.x = -10.0F, .y = 20.0F, .z = -10.0F}},
        {.position = {.x = 10.0F, .y = 20.0F, .z = -10.0F}},
        {.position = {.x = 10.0F, .y = 20.0F, .z = 10.0F}},
        {.position = {.x = -10.0F, .y = 20.0F, .z = 10.0F}}};
    model.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{});

    CHECK(App::Character::StaticSupportQuery::find(model, model.runtime_objects, query_at())
            .has_value());
    CHECK(App::Character::StaticSupportQuery::find(
        model, model.runtime_objects, query_at(10.0F, 5.0F, 0.0F))
            .has_value());
    CHECK(App::Character::StaticSupportQuery::find(
        model, model.runtime_objects, query_at(10.0F, 5.0F, 10.0F))
            .has_value());
    CHECK_FALSE(App::Character::StaticSupportQuery::find(
        model, model.runtime_objects, query_at(10.1F, 5.0F, 0.0F))
            .has_value());
  }

  TEST_CASE("rejects walls ceilings near-vertical faces and outside intersections") {
    App::Omikron::Model3DOData model{flat_triangle_model()};
    auto& triangle{model.polygons.front().triangles.front()};

    triangle.face_normal = {.x = 1.0F, .y = 0.0F, .z = 0.0F};
    CHECK_FALSE(App::Character::StaticSupportQuery::find(model, model.runtime_objects, query_at())
            .has_value());
    triangle.face_normal = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    CHECK_FALSE(App::Character::StaticSupportQuery::find(model, model.runtime_objects, query_at())
            .has_value());
    triangle.face_normal = {.x = 1.0F, .y = -0.0001F, .z = 0.0F};
    CHECK_FALSE(App::Character::StaticSupportQuery::find(model, model.runtime_objects, query_at())
            .has_value());
    triangle.face_normal = {.x = 0.0F, .y = -1.0F, .z = 0.0F};
    CHECK_FALSE(App::Character::StaticSupportQuery::find(
        model, model.runtime_objects, query_at(50.0F, 5.0F, 0.0F))
            .has_value());
  }

  TEST_CASE("selects the nearest stacked floor and retains negative clearance") {
    App::Omikron::Model3DOData model{flat_triangle_model()};
    model.meshes.push_back(model.meshes.front());
    model.polygons.push_back(model.polygons.front());
    model.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{
        .world_translation = {.x = 0.0F, .y = 30.0F, .z = 0.0F}});

    const auto nearest{App::Character::StaticSupportQuery::find(
        model, model.runtime_objects, query_at(0.0F, 5.0F, 0.0F, 20.0F))};

    REQUIRE(nearest.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const App::Character::StaticSupportHit& value{nearest.value()};
    CHECK_EQ(value.object_index, 0U);
    CHECK(value.world_point.y == doctest::Approx(20.0F));
    CHECK(value.clearance == doctest::Approx(-5.0F));
  }

  TEST_CASE("honors static filtering masks") {
    App::Omikron::Model3DOData model{flat_triangle_model()};
    model.meshes.front().flags = 0x00000001U;
    CHECK_FALSE(App::Character::StaticSupportQuery::find(model, model.runtime_objects, query_at())
            .has_value());
    model.meshes.front().flags = 0x00000040U;
    CHECK_FALSE(App::Character::StaticSupportQuery::find(model, model.runtime_objects, query_at())
            .has_value());
    model.meshes.front().flags = 0x00080000U;
    CHECK_FALSE(App::Character::StaticSupportQuery::find(model, model.runtime_objects, query_at())
            .has_value());
  }

  TEST_CASE("uses authored face normals independently of material and vertex normals") {
    App::Omikron::Model3DOData model{flat_triangle_model()};
    auto& triangle{model.polygons.front().triangles.front()};
    triangle.material_id = 999;
    for (auto& vertex : model.vertices) {
      vertex.normal = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    }
    CHECK(App::Character::StaticSupportQuery::find(model, model.runtime_objects, query_at())
            .has_value());
  }

  TEST_CASE("skips empty and malformed geometry safely") {
    const App::Omikron::Model3DOData empty;
    CHECK_FALSE(App::Character::StaticSupportQuery::find(empty, {}, query_at()).has_value());

    App::Omikron::Model3DOData malformed{flat_triangle_model()};
    malformed.polygons.front().triangles.front().vertices.front().index = 999;
    CHECK_FALSE(
        App::Character::StaticSupportQuery::find(malformed, malformed.runtime_objects, query_at())
            .has_value());
  }
}
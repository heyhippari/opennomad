#include "Core/Character/HorizontalCollisionQuery.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <numbers>

namespace {

using App::Character::HorizontalCollisionBody;
using App::Character::HorizontalCollisionQuery;
using App::Omikron::MeshDescriptor;
using App::Omikron::MeshPolygons;
using App::Omikron::Model3DOData;
using App::Omikron::Rectangle;
using App::Runtime::Vec3;

constexpr HorizontalCollisionBody K_BODY{.radius = 2.0F, .top_y = -5.0F, .bottom_y = 5.0F};

Model3DOData wall_model(const std::uint32_t flags = 0U) {
  Model3DOData model;
  model.meshes.push_back(MeshDescriptor{.flags = flags, .vertex_count = 4});
  model.polygons.push_back(MeshPolygons{
      .rectangles = {Rectangle{.vertices = {0, 1, 2, 3}, .face_normal = {.x = -1.0F}}}});
  model.vertices = {{.position = {.x = 10.0F, .y = -20.0F, .z = -20.0F}},
      {.position = {.x = 10.0F, .y = -20.0F, .z = 20.0F}},
      {.position = {.x = 10.0F, .y = 20.0F, .z = 20.0F}},
      {.position = {.x = 10.0F, .y = 20.0F, .z = -20.0F}}};
  model.runtime_objects.push_back(Model3DOData::RuntimeObjectState{});
  return model;
}

}  // namespace

TEST_SUITE("Core::Character::HorizontalCollision") {
  TEST_CASE("no geometry and near-zero movement preserve requested horizontal motion") {
    const Model3DOData empty;
    const auto unobstructed{HorizontalCollisionQuery::resolve(
        empty, {}, {}, {.x = 8.0F, .y = 12.0F, .z = 3.0F}, K_BODY)};
    CHECK_EQ(unobstructed.resolved_displacement.x, 8.0F);
    CHECK_EQ(unobstructed.resolved_displacement.y, 0.0F);
    CHECK_EQ(unobstructed.resolved_displacement.z, 3.0F);
    CHECK_FALSE(unobstructed.forward_collision);

    const auto tiny{
        HorizontalCollisionQuery::resolve(empty, {}, {}, {.x = 0.00001F, .z = 0.00001F}, K_BODY)};
    CHECK_EQ(tiny.resolved_displacement.x, 0.0F);
    CHECK_EQ(tiny.resolved_displacement.z, 0.0F);
  }

  TEST_CASE("continuous wall sweep stops at the native one-inch skin") {
    const Model3DOData model{wall_model()};
    const auto result{
        HorizontalCollisionQuery::resolve(model, model.runtime_objects, {}, {.x = 100.0F}, K_BODY)};

    CHECK(result.forward_collision);
    CHECK_EQ(result.collision_passes, 1U);
    CHECK(result.resolved_displacement.x == doctest::Approx(7.0F));
    REQUIRE(result.last_hit.has_value());
    const auto hit{result.last_hit.value_or(App::Character::HorizontalCollisionHit{})};
    CHECK(hit.travel_distance == doctest::Approx(8.0F));
    CHECK(hit.world_normal.x == doctest::Approx(-1.0F));
  }

  TEST_CASE("lookahead observes but does not curtail a contact beyond the endpoint") {
    Model3DOData model{wall_model()};
    for (auto& vertex : model.vertices) {
      vertex.position.x = 11.5F;
    }
    const auto result{
        HorizontalCollisionQuery::resolve(model, model.runtime_objects, {}, {.x = 8.0F}, K_BODY)};

    CHECK_FALSE(result.forward_collision);
    CHECK_EQ(result.resolved_displacement.x, 8.0F);
  }

  TEST_CASE("contact exactly at the endpoint is a forward collision") {
    const Model3DOData model{wall_model()};
    const auto result{
        HorizontalCollisionQuery::resolve(model, model.runtime_objects, {}, {.x = 8.0F}, K_BODY)};
    CHECK(result.forward_collision);
    CHECK(result.resolved_displacement.x == doctest::Approx(7.0F));
  }

  TEST_CASE("pure starting-skin correction is diagnosed separately from forward collision") {
    const Model3DOData model{wall_model()};
    const auto result{HorizontalCollisionQuery::resolve(
        model, model.runtime_objects, {.x = 7.5F}, {.x = -2.0F}, K_BODY)};

    CHECK(result.depenetrated);
    CHECK_FALSE(result.forward_collision);
    CHECK_EQ(result.depenetration_iterations, 1U);
    CHECK(result.resolved_displacement.x == doctest::Approx(-2.5F));
  }

  TEST_CASE("diagonal collision preserves tangential wall motion") {
    const Model3DOData model{wall_model()};
    const auto result{HorizontalCollisionQuery::resolve(
        model, model.runtime_objects, {}, {.x = 20.0F, .z = 20.0F}, K_BODY)};

    CHECK(result.forward_collision);
    CHECK(result.resolved_displacement.x == doctest::Approx(7.0F));
    CHECK(result.resolved_displacement.z == doctest::Approx(20.0F));
  }

  TEST_CASE("parallel and away movement do not stick to a wall outside the skin") {
    const Model3DOData model{wall_model()};
    const auto parallel{HorizontalCollisionQuery::resolve(
        model, model.runtime_objects, {.x = 7.0F}, {.z = 8.0F}, K_BODY)};
    CHECK_FALSE(parallel.forward_collision);
    CHECK_EQ(parallel.resolved_displacement.z, 8.0F);

    const auto away{HorizontalCollisionQuery::resolve(
        model, model.runtime_objects, {.x = 7.0F}, {.x = -8.0F}, K_BODY)};
    CHECK_FALSE(away.forward_collision);
    CHECK_EQ(away.resolved_displacement.x, -8.0F);
  }

  TEST_CASE("flat floors and ceilings never become horizontal blockers") {
    Model3DOData model{wall_model()};
    model.polygons.front().rectangles.front().face_normal = {.y = -1.0F};
    model.vertices = {{.position = {.x = -20.0F, .y = 5.0F, .z = -20.0F}},
        {.position = {.x = 20.0F, .y = 5.0F, .z = -20.0F}},
        {.position = {.x = 20.0F, .y = 5.0F, .z = 20.0F}},
        {.position = {.x = -20.0F, .y = 5.0F, .z = 20.0F}}};
    CHECK_FALSE(
        HorizontalCollisionQuery::find(model, model.runtime_objects, {}, {.x = 1.0F}, 20.0F, K_BODY)
            .has_value());
    model.polygons.front().rectangles.front().face_normal.y = 1.0F;
    CHECK_FALSE(
        HorizontalCollisionQuery::find(model, model.runtime_objects, {}, {.x = 1.0F}, 20.0F, K_BODY)
            .has_value());
  }

  TEST_CASE("ordinary horizontal object masks differ from static support filtering") {
    for (const std::uint32_t flags : {0x00000001U, 0x00000040U, 0x20000000U}) {
      const Model3DOData model{wall_model(flags)};
      CHECK_FALSE(HorizontalCollisionQuery::find(
          model, model.runtime_objects, {}, {.x = 1.0F}, 20.0F, K_BODY)
              .has_value());
    }
    const Model3DOData transformed{wall_model(0x00080000U)};
    CHECK(HorizontalCollisionQuery::find(
        transformed, transformed.runtime_objects, {}, {.x = 1.0F}, 20.0F, K_BODY)
            .has_value());
  }

  TEST_CASE("runtime translation and nonuniform scale affect vertices and normals") {
    Model3DOData translated{wall_model(0x00080000U)};
    translated.runtime_objects.front().world_translation.x = 10.0F;
    auto result{HorizontalCollisionQuery::resolve(
        translated, translated.runtime_objects, {}, {.x = 30.0F}, K_BODY)};
    CHECK(result.resolved_displacement.x == doctest::Approx(17.0F));

    Model3DOData scaled{wall_model(0x00080000U)};
    scaled.runtime_objects.front().scale = {.x = 2.0F, .y = 0.5F, .z = 3.0F};
    result =
        HorizontalCollisionQuery::resolve(scaled, scaled.runtime_objects, {}, {.x = 30.0F}, K_BODY);
    CHECK(result.resolved_displacement.x == doctest::Approx(17.0F));
    REQUIRE(result.last_hit.has_value());
    const auto hit{result.last_hit.value_or(App::Character::HorizontalCollisionHit{})};
    CHECK(hit.world_normal.x == doctest::Approx(-1.0F));
  }

  TEST_CASE("runtime rotation moves the wall and its response normal") {
    Model3DOData model{wall_model(0x00080000U)};
    model.runtime_objects.front().world_matrix =
        App::Runtime::rotation_y(std::numbers::pi_v<float> / 2.0F);
    const auto result{
        HorizontalCollisionQuery::resolve(model, model.runtime_objects, {}, {.z = 30.0F}, K_BODY)};

    CHECK(result.forward_collision);
    CHECK(result.resolved_displacement.z == doctest::Approx(7.0F));
    REQUIRE(result.last_hit.has_value());
    const auto hit{result.last_hit.value_or(App::Character::HorizontalCollisionHit{})};
    CHECK(hit.world_normal.z == doctest::Approx(-1.0F));
  }

  TEST_CASE("triangle edge and vertex features are swept continuously") {
    Model3DOData model;
    model.meshes.push_back(MeshDescriptor{.vertex_count = 3});
    model.polygons.push_back(
        App::Omikron::MeshPolygons{.triangles = {App::Omikron::Triangle{
                                       .vertices = {{{.index = 0}, {.index = 1}, {.index = 2}}},
                                       .face_normal = {.x = -1.0F}}}});
    model.vertices = {{.position = {.x = 10.0F, .y = -10.0F, .z = -10.0F}},
        {.position = {.x = 10.0F, .y = 0.0F, .z = 10.0F}},
        {.position = {.x = 10.0F, .y = 10.0F, .z = -10.0F}}};
    model.runtime_objects.push_back(Model3DOData::RuntimeObjectState{});

    const auto edge{HorizontalCollisionQuery::find(
        model, model.runtime_objects, {.y = 8.0F}, {.x = 1.0F}, 20.0F, K_BODY)};
    REQUIRE(edge.has_value());
    const auto edge_hit{edge.value_or(App::Character::HorizontalCollisionHit{})};
    CHECK(edge_hit.travel_distance == doctest::Approx(8.0F));

    const auto vertex{HorizontalCollisionQuery::find(
        model, model.runtime_objects, {.z = 11.5F}, {.x = 1.0F}, 20.0F, K_BODY)};
    REQUIRE(vertex.has_value());
    const auto vertex_hit{vertex.value_or(App::Character::HorizontalCollisionHit{})};
    CHECK(vertex_hit.travel_distance == doctest::Approx(10.0F - std::sqrt(1.75F)));
    CHECK(vertex_hit.world_normal.z == doctest::Approx(0.75F));
  }

  TEST_CASE("trimmed cylinder steps over low geometry but blocks geometry above the trim") {
    Model3DOData model{wall_model()};
    for (auto& vertex : model.vertices) {
      vertex.position.y = vertex.position.y < 0.0F ? 1.0F : 5.0F;
    }
    const HorizontalCollisionBody trimmed{.radius = 2.0F, .top_y = -12.0F, .bottom_y = 0.2F};
    CHECK_FALSE(HorizontalCollisionQuery::find(
        model, model.runtime_objects, {}, {.x = 1.0F}, 20.0F, trimmed)
            .has_value());
    model.vertices.front().position.y = 0.0F;
    model.vertices.at(1).position.y = 0.0F;
    CHECK(HorizontalCollisionQuery::find(
        model, model.runtime_objects, {}, {.x = 1.0F}, 20.0F, trimmed)
            .has_value());
  }

  TEST_CASE("effective radius changes horizontal contact without changing vertical extents") {
    const Model3DOData model{wall_model()};
    const auto normal{
        HorizontalCollisionQuery::resolve(model, model.runtime_objects, {}, {.x = 20.0F}, K_BODY)};
    const auto enlarged{HorizontalCollisionQuery::resolve(model,
        model.runtime_objects,
        {},
        {.x = 20.0F},
        {.radius = 4.0F, .top_y = K_BODY.top_y, .bottom_y = K_BODY.bottom_y})};
    CHECK(normal.resolved_displacement.x == doctest::Approx(7.0F));
    CHECK(enlarged.resolved_displacement.x == doctest::Approx(5.0F));
  }

  TEST_CASE("a slide can contact and constrain against a second wall") {
    Model3DOData model{wall_model()};
    model.meshes.push_back(MeshDescriptor{.vertex_count = 4, .vertex_base = 4});
    model.polygons.push_back(MeshPolygons{
        .rectangles = {Rectangle{.vertices = {0, 1, 2, 3}, .face_normal = {.x = -1.0F}}}});
    model.vertices.insert(model.vertices.end(),
        {{.position = {.x = 12.0F, .y = -20.0F, .z = -20.0F}},
            {.position = {.x = 12.0F, .y = -20.0F, .z = 20.0F}},
            {.position = {.x = 12.0F, .y = 20.0F, .z = 20.0F}},
            {.position = {.x = 12.0F, .y = 20.0F, .z = -20.0F}}});
    model.runtime_objects.push_back(Model3DOData::RuntimeObjectState{
        .world_matrix = App::Runtime::rotation_y(std::numbers::pi_v<float> / 2.0F)});

    const auto result{HorizontalCollisionQuery::resolve(
        model, model.runtime_objects, {}, {.x = 20.0F, .z = 20.0F}, K_BODY)};
    CHECK(result.forward_collision);
    CHECK_EQ(result.collision_passes, 2U);
    CHECK(result.resolved_displacement.x == doctest::Approx(7.0F));
    CHECK(result.resolved_displacement.z == doctest::Approx(9.0F));
  }

  TEST_CASE("pathological shallow plane reaches the depenetration safety cap") {
    constexpr Vec3 normal{.x = -0.01F, .y = -0.99995F};
    constexpr Vec3 center{.x = 2.005F, .y = 5.499975F};
    constexpr Vec3 tangent{.x = normal.y * 100.0F, .y = -normal.x * 100.0F};
    Model3DOData model;
    model.meshes.push_back(MeshDescriptor{.vertex_count = 4});
    model.polygons.push_back(
        MeshPolygons{.rectangles = {Rectangle{.vertices = {0, 1, 2, 3}, .face_normal = normal}}});
    model.vertices = {
        {.position = {.x = center.x - tangent.x, .y = center.y - tangent.y, .z = -100.0F}},
        {.position = {.x = center.x - tangent.x, .y = center.y - tangent.y, .z = 100.0F}},
        {.position = {.x = center.x + tangent.x, .y = center.y + tangent.y, .z = 100.0F}},
        {.position = {.x = center.x + tangent.x, .y = center.y + tangent.y, .z = -100.0F}}};
    model.runtime_objects.push_back(Model3DOData::RuntimeObjectState{});

    const auto result{
        HorizontalCollisionQuery::resolve(model, model.runtime_objects, {}, {.x = 2.0F}, K_BODY)};
    CHECK(result.depenetrated);
    CHECK_FALSE(result.forward_collision);
    CHECK(result.depenetration_limit_reached);
    CHECK_EQ(
        result.depenetration_iterations, HorizontalCollisionQuery::K_MAX_DEPENETRATION_ITERATIONS);
  }

  TEST_CASE("quad is tested as four authored edges without a diagonal feature") {
    const Model3DOData model{wall_model()};
    const auto face{HorizontalCollisionQuery::find(
        model, model.runtime_objects, {}, {.x = 1.0F}, 20.0F, K_BODY)};
    REQUIRE(face.has_value());
    const auto hit{face.value_or(App::Character::HorizontalCollisionHit{})};
    CHECK(hit.travel_distance == doctest::Approx(8.0F));
    CHECK(hit.world_point.z == doctest::Approx(0.0F));
  }
}
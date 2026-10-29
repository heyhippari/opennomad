#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
// misc-include-cleaner, modernize-use-designated-initializers)

#include <array>
#include <cmath>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <random>
#include <span>
#include <vector>

#include "Core/DecorCulling.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/ViewFrustum.hpp"

namespace {

using App::BoundingSphere;
using App::DecorCulling;
using App::ViewFrustum;

[[nodiscard]] ViewFrustum make_frustum(const glm::vec3& eye, const glm::vec3& target) {
  const glm::mat4 view{glm::lookAt(eye, target, glm::vec3{0.0F, 1.0F, 0.0F})};
  const glm::mat4 projection{glm::perspective(glm::radians(60.0F), 4.0F / 3.0F, 2.0F, 500.0F)};
  const glm::mat4 clip{projection * view};
  return ViewFrustum::from_clip_matrix(std::span<const float, 16>{glm::value_ptr(clip), 16U});
}

App::Omikron::MeshDescriptor make_mesh(const float radius) {
  App::Omikron::MeshDescriptor mesh;
  mesh.bounds_min = {-1.0F, -1.0F, -1.0F};
  mesh.bounds_max = {1.0F, 1.0F, 1.0F};
  mesh.bounding_radius = radius;
  return mesh;
}

}  // namespace

TEST_SUITE("Core::DecorCulling") {
  TEST_CASE("Frustum keeps spheres in view and rejects spheres fully outside each plane") {
    const ViewFrustum frustum{make_frustum({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F})};
    CHECK(frustum.intersects({{0.0F, 0.0F, -50.0F}, 1.0F}));
    CHECK_FALSE(frustum.intersects({{0.0F, 0.0F, 50.0F}, 1.0F}));      // behind
    CHECK_FALSE(frustum.intersects({{0.0F, 0.0F, -2000.0F}, 1.0F}));   // beyond far
    CHECK_FALSE(frustum.intersects({{500.0F, 0.0F, -50.0F}, 1.0F}));   // right
    CHECK_FALSE(frustum.intersects({{-500.0F, 0.0F, -50.0F}, 1.0F}));  // left
    CHECK_FALSE(frustum.intersects({{0.0F, 500.0F, -50.0F}, 1.0F}));   // above
    CHECK_FALSE(frustum.intersects({{0.0F, -500.0F, -50.0F}, 1.0F}));  // below
    // A large sphere around the eye straddles every plane and must be kept.
    CHECK(frustum.intersects({{0.0F, 0.0F, 0.0F}, 300.0F}));
    // A sphere that merely touches a side plane is kept.
    CHECK(frustum.intersects({{40.0F, 0.0F, -50.0F}, 40.0F}));
  }

  TEST_CASE("Frustum never rejects a sphere around a point that is inside the view volume") {
    const glm::vec3 eye{120.0F, -40.0F, 3300.0F};
    const glm::vec3 target{150.0F, -40.0F, 3000.0F};
    const glm::mat4 view{glm::lookAt(eye, target, glm::vec3{0.0F, 1.0F, 0.0F})};
    const glm::mat4 projection{glm::perspective(glm::radians(50.0F), 16.0F / 9.0F, 2.0F, 800.0F)};
    const glm::mat4 clip{projection * view};
    const ViewFrustum frustum{
        ViewFrustum::from_clip_matrix(std::span<const float, 16>{glm::value_ptr(clip), 16U})};
    std::mt19937 random{1234U};
    std::uniform_real_distribution<float> offset{-900.0F, 900.0F};
    std::size_t inside{0};
    for (std::size_t sample{0}; sample < 20000U; ++sample) {
      const glm::vec3 point{eye.x + offset(random), eye.y + offset(random), eye.z + offset(random)};
      const glm::vec4 projected{clip * glm::vec4{point, 1.0F}};
      const bool in_view{projected.w > 0.0F && std::abs(projected.x) <= projected.w &&
                         std::abs(projected.y) <= projected.w &&
                         std::abs(projected.z) <= projected.w};
      if (in_view) {
        ++inside;
        CHECK(frustum.intersects({{point.x, point.y, point.z}, 0.01F}));
      }
    }
    CHECK(inside > 100U);
  }

  TEST_CASE("Frustum treats non-finite input as visible") {
    const ViewFrustum frustum{make_frustum({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F})};
    const float nan{std::numeric_limits<float>::quiet_NaN()};
    CHECK(frustum.intersects({{nan, 0.0F, -10.0F}, 1.0F}));
    CHECK(frustum.intersects({{0.0F, 0.0F, 50.0F}, nan}));
  }

  TEST_CASE("Object sphere follows the live runtime transform, scale and GL basis") {
    App::Omikron::Model3DOData model;
    model.meshes.push_back(make_mesh(2.0F));
    model.skin_parent_index = {-1};
    std::vector<App::Omikron::Model3DOData::RuntimeObjectState> objects(1U);
    objects.front().world_translation = {10.0F, 20.0F, 30.0F};
    objects.front().scale = {1.0F, 3.0F, 2.0F};

    const auto sphere{DecorCulling::object_sphere(model, objects, 0U)};
    REQUIRE(sphere.has_value());
    // Runtime (+Y down, +Z forward) maps to GL by negating Y and Z.
    CHECK(sphere->center.at(0) == doctest::Approx(10.0F));
    CHECK(sphere->center.at(1) == doctest::Approx(-20.0F));
    CHECK(sphere->center.at(2) == doctest::Approx(-30.0F));
    CHECK(sphere->radius == doctest::Approx(6.0F));

    objects.front().world_translation = {-100.0F, 0.0F, 0.0F};
    const auto moved{DecorCulling::object_sphere(model, objects, 0U)};
    REQUIRE(moved.has_value());
    CHECK(moved->center.at(0) == doctest::Approx(-100.0F));
  }

  TEST_CASE("Objects without a safe bound are never culled") {
    App::Omikron::Model3DOData model;
    model.meshes.push_back(make_mesh(0.0F));  // no radius
    model.meshes.push_back(make_mesh(5.0F));  // skinned
    model.meshes.push_back(make_mesh(5.0F));  // bounded
    model.skin_parent_index = {-1, 2, -1};
    std::vector<App::Omikron::Model3DOData::RuntimeObjectState> objects(3U);
    for (auto& object : objects) {
      object.world_translation = {0.0F, 0.0F, -5000.0F};  // far outside the test frustum
    }
    CHECK_FALSE(DecorCulling::object_sphere(model, objects, 0U).has_value());
    CHECK_FALSE(DecorCulling::object_sphere(model, objects, 1U).has_value());
    CHECK(DecorCulling::object_sphere(model, objects, 2U).has_value());
    CHECK_FALSE(DecorCulling::object_sphere(model, objects, 3U).has_value());

    DecorCulling culling;
    culling.rebuild(model, objects);
    const ViewFrustum frustum{make_frustum({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F})};
    CHECK(culling.object_visible(0U, frustum));
    CHECK(culling.object_visible(1U, frustum));
    CHECK_FALSE(culling.object_visible(2U, frustum));
    CHECK(culling.object_visible(99U, frustum));
  }

  TEST_CASE("Moving a culled object into view makes it visible after rebuild") {
    App::Omikron::Model3DOData model;
    model.meshes.push_back(make_mesh(5.0F));
    model.skin_parent_index = {-1};
    std::vector<App::Omikron::Model3DOData::RuntimeObjectState> objects(1U);
    objects.front().world_translation = {0.0F, 0.0F, 5000.0F};
    DecorCulling culling;
    culling.rebuild(model, objects);
    const ViewFrustum frustum{make_frustum({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F})};
    CHECK_FALSE(culling.object_visible(0U, frustum));
    objects.front().world_translation = {0.0F, 0.0F, 50.0F};  // Runtime +Z forward => GL -Z
    culling.rebuild(model, objects);
    CHECK(culling.object_visible(0U, frustum));
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
// misc-include-cleaner, modernize-use-designated-initializers)

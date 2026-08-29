#include "Core/Sprite/SpriteRenderer.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <numbers>
#include <span>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/Sprite/SpritePool.hpp"
#include "Core/Sprite/SpriteResource.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <glm/glm.hpp>

namespace {

using App::Sprite::SpriteHandle;
using App::Sprite::SpritePool;
using App::Sprite::SpriteRenderer;
using App::Sprite::SpriteRenderMode;
using App::Sprite::SpriteResource;
using App::Sprite::SpriteSkipReason;

/// One object with a single frame descriptor spanning points 0 (0,0,0) and
/// 1 (2,3,0): the resolved frame is 2 units wide and 3 units high.
App::Omikron::Model3DOData make_sprite_model() {
  App::Omikron::Model3DOData model;
  model.materials.emplace_back();

  App::Omikron::MeshDescriptor mesh;
  mesh.name = "SPRITE";
  mesh.vertex_count = 4;
  mesh.rectangle_count = 1;
  model.meshes.push_back(mesh);

  App::Omikron::MeshPolygons polygons;
  App::Omikron::Rectangle frame;
  frame.vertices = {0, 2, 1, 3};
  frame.uv = {0, 0, 12, 34, 255, 255, 56, 78};
  frame.material_id = 0;
  polygons.rectangles.push_back(frame);
  model.polygons.push_back(polygons);

  model.vertices.resize(4);
  model.vertices.at(0).position = App::Omikron::Vec3{0.0F, 0.0F, 0.0F};  // Point 0.
  model.vertices.at(1).position = App::Omikron::Vec3{2.0F, 3.0F, 0.0F};  // Diagonal target.
  model.vertices.at(2).position = App::Omikron::Vec3{0.0F, 3.0F, 0.0F};
  model.vertices.at(3).position = App::Omikron::Vec3{2.0F, 0.0F, 0.0F};
  return model;
}

SpriteResource make_resource() {
  SpriteResource resource;
  resource.name = "TEST";
  resource.model = make_sprite_model();
  return resource;
}

/// GL camera basis produced from a Runtime camera looking down native +Z.
constexpr glm::vec3 k_eye{0.0F, 0.0F, 0.0F};
constexpr glm::vec3 k_forward{0.0F, 0.0F, -1.0F};
constexpr glm::vec3 k_right{1.0F, 0.0F, 0.0F};
constexpr glm::vec3 k_up{0.0F, 1.0F, 0.0F};
constexpr float k_near{0.1F};
constexpr float k_far{100.0F};

/// Builds the queue for one fixture resource with the fixed camera basis.
void build_queue(SpriteRenderer& renderer,
    const SpritePool& pool,
    const std::span<const SpriteResource* const> resources) {
  renderer.build_queue(pool, resources, k_eye, k_forward, k_right, k_up, k_near, k_far);
}

/// Creates and attaches one sprite at position with the given mode/frame.
SpriteHandle make_attached(SpritePool& pool,
    const std::array<float, 3> position,
    const SpriteRenderMode mode = SpriteRenderMode::k_default) {
  const SpriteHandle handle{pool.create(0, 0, 1, position).value()};
  pool.set_render_mode(handle, mode);
  pool.set_frame(handle, 0).value();
  pool.attach(handle).value();
  return handle;
}

void check_vec3(const std::array<float, 3>& actual, const float x, const float y, const float z) {
  CHECK_EQ(actual.at(0), doctest::Approx(x));
  CHECK_EQ(actual.at(1), doctest::Approx(y));
  CHECK_EQ(actual.at(2), doctest::Approx(z));
}

}  // namespace

TEST_SUITE("Core::Sprite::SpriteRenderer") {
  TEST_CASE("Emits a camera-facing quad at the instance position") {
    SpritePool pool;
    make_attached(pool, {0.0F, 0.0F, 5.0F});
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});

    REQUIRE_EQ(renderer.vertices().size(), std::size_t{6});
    check_vec3(renderer.vertices().at(0).position, -1.0F, -1.5F, -5.0F);
    check_vec3(renderer.vertices().at(1).position, 1.0F, -1.5F, -5.0F);
    check_vec3(renderer.vertices().at(2).position, 1.0F, 1.5F, -5.0F);
    check_vec3(renderer.vertices().at(4).position, 1.0F, 1.5F, -5.0F);
    check_vec3(renderer.vertices().at(5).position, -1.0F, 1.5F, -5.0F);
    CHECK_EQ(renderer.queue_stats().visible, std::size_t{1});
    CHECK_EQ(renderer.queue_stats().drawn, std::size_t{1});
  }

  TEST_CASE("Winds triangles counter-clockwise as seen from the camera") {
    SpritePool pool;
    make_attached(pool, {0.0F, 0.0F, 5.0F});
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});

    const auto make_vec3 = [](const std::array<float, 3>& value) {
      return glm::vec3{value.at(0), value.at(1), value.at(2)};
    };
    const glm::vec3 edge1{make_vec3(renderer.vertices().at(1).position) -
                          make_vec3(renderer.vertices().at(0).position)};
    const glm::vec3 edge2{make_vec3(renderer.vertices().at(2).position) -
                          make_vec3(renderer.vertices().at(0).position)};
    const glm::vec3 normal{glm::cross(edge1, edge2)};
    CHECK_LT(glm::dot(normal, k_forward), 0.0F);  // Faces back toward the camera.
  }

  TEST_CASE("Converts frame UVs with /256 and adds the instance offsets") {
    SpritePool pool;
    const SpriteHandle handle{make_attached(pool, {0.0F, 0.0F, 5.0F})};
    pool.set_texture_offset(handle, 0.1F, -0.2F);
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});

    // Corner order: 0 = (-,-), 1 = (+,-), 2 = (+,+), 5 = (-,+). u spans
    // left-to-right, v spans bottom-to-top.
    CHECK_EQ(renderer.vertices().at(0).uv.at(0), doctest::Approx(0.1F));
    CHECK_EQ(renderer.vertices().at(0).uv.at(1), doctest::Approx(-0.2F));
    CHECK_EQ(renderer.vertices().at(1).uv.at(0), doctest::Approx((255.0F / 256.0F) + 0.1F));
    CHECK_EQ(renderer.vertices().at(1).uv.at(1), doctest::Approx(-0.2F));
    CHECK_EQ(renderer.vertices().at(2).uv.at(0), doctest::Approx((255.0F / 256.0F) + 0.1F));
    CHECK_EQ(renderer.vertices().at(2).uv.at(1), doctest::Approx((255.0F / 256.0F) - 0.2F));
    CHECK_EQ(renderer.vertices().at(5).uv.at(0), doctest::Approx(0.1F));
    CHECK_EQ(renderer.vertices().at(5).uv.at(1), doctest::Approx((255.0F / 256.0F) - 0.2F));
  }

  TEST_CASE("Propagates diffuse alpha to every billboard vertex") {
    SpritePool pool;
    const SpriteHandle handle{make_attached(pool, {0.0F, 0.0F, 5.0F})};
    pool.set_tint(handle, {0.5F, 0.25F, 0.125F});
    pool.set_diffuse_alpha(handle, 0.5F);
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});

    REQUIRE_EQ(renderer.vertices().size(), std::size_t{6});
    for (const App::Sprite::SpriteVertex& vertex : renderer.vertices()) {
      CHECK_EQ(vertex.tint.at(0), 0.5F);
      CHECK_EQ(vertex.tint.at(1), 0.25F);
      CHECK_EQ(vertex.tint.at(2), 0.125F);
      CHECK_EQ(vertex.tint.at(3), 0.5F);
    }
  }

  TEST_CASE("Scales the quad by scale_x and scale_y") {
    SpritePool pool;
    const SpriteHandle handle{make_attached(pool, {0.0F, 0.0F, 5.0F})};
    pool.set_scale(handle, 2.0F, 1.0F);
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});

    // Half-extents become 2 x 1.5; corner 0 = pos + right*(-2) + up*(-1.5).
    check_vec3(renderer.vertices().at(0).position, -2.0F, -1.5F, -5.0F);
  }

  TEST_CASE("Rotates the quad around the billboard centre") {
    SpritePool pool;
    const SpriteHandle handle{make_attached(pool, {0.0F, 0.0F, 5.0F})};
    pool.set_rotation(handle, std::numbers::pi_v<float> / 2.0F);
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});

    // corner(-1, -1.5) rotated 90° becomes (1.5, -1) in camera space.
    check_vec3(renderer.vertices().at(0).position, 1.5F, -1.0F, -5.0F);
  }

  TEST_CASE("Batches equal pipeline keys and counts draw calls") {
    SpritePool pool;
    make_attached(pool, {0.0F, 0.0F, 5.0F});
    make_attached(pool, {1.0F, 0.0F, 5.0F});
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});
    CHECK_EQ(renderer.queue_stats().draw_calls, std::size_t{2});
    CHECK_EQ(renderer.queue_stats().batches, std::size_t{1});
  }

  TEST_CASE("Counts a separate batch per render mode") {
    SpritePool pool;
    const SpriteHandle alpha{make_attached(pool, {0.0F, 0.0F, 5.0F})};
    pool.set_render_mode(alpha, SpriteRenderMode::k_alpha);
    const SpriteHandle additive{make_attached(pool, {1.0F, 0.0F, 5.0F})};
    pool.set_render_mode(additive, SpriteRenderMode::k_additive);
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});
    CHECK_EQ(renderer.queue_stats().batches, std::size_t{2});
  }

  TEST_CASE("Orders opaque commands before translucent ones") {
    SpritePool pool;
    const SpriteHandle alpha{make_attached(pool, {0.0F, 0.0F, 5.0F})};
    pool.set_render_mode(alpha, SpriteRenderMode::k_alpha);
    make_attached(pool, {1.0F, 0.0F, 5.0F});
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});
    REQUIRE_EQ(renderer.commands().size(), std::size_t{2});
    CHECK_EQ(renderer.commands().at(0).pipeline_key.render_mode, SpriteRenderMode::k_default);
    CHECK_EQ(renderer.commands().at(1).pipeline_key.render_mode, SpriteRenderMode::k_alpha);
  }

  TEST_CASE("Orders additive before darken regardless of resource identity") {
    SpritePool pool;
    const SpriteHandle additive{pool.create(1, 0, 1, {0.0F, 0.0F, 5.0F}).value()};
    pool.set_render_mode(additive, SpriteRenderMode::k_additive);
    pool.set_frame(additive, 0).value();
    pool.attach(additive).value();
    const SpriteHandle darken{pool.create(0, 0, 1, {1.0F, 0.0F, 5.0F}).value()};
    pool.set_render_mode(darken, SpriteRenderMode::k_darken);
    pool.set_frame(darken, 0).value();
    pool.attach(darken).value();

    const SpriteResource low_resource{make_resource()};
    const SpriteResource high_resource{make_resource()};
    const std::array<const SpriteResource*, 2> resources{&low_resource, &high_resource};
    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});

    REQUIRE_EQ(renderer.commands().size(), std::size_t{2});
    CHECK_EQ(renderer.commands().at(0).sprite, additive);
    CHECK_EQ(renderer.commands().at(0).pipeline_key.render_mode, SpriteRenderMode::k_additive);
    CHECK_EQ(renderer.commands().at(1).sprite, darken);
    CHECK_EQ(renderer.commands().at(1).pipeline_key.render_mode, SpriteRenderMode::k_darken);
  }

  TEST_CASE("Preserves insertion order within a batch") {
    SpritePool pool;
    const SpriteHandle first{make_attached(pool, {0.0F, 0.0F, 5.0F})};
    const SpriteHandle second{make_attached(pool, {1.0F, 0.0F, 5.0F})};
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});
    REQUIRE_EQ(renderer.commands().size(), std::size_t{2});
    // The render list is head-inserted: second was attached last.
    CHECK_EQ(renderer.commands().at(0).sprite, second);
    CHECK_EQ(renderer.commands().at(1).sprite, first);
  }

  TEST_CASE("Skips an invalid frame with a diagnostic reason") {
    SpritePool pool;
    const SpriteHandle handle{make_attached(pool, {0.0F, 0.0F, 5.0F})};
    // Out of range: the instance stores the 0xFFFF sentinel and the call errors.
    const auto out_of_range_frame{pool.set_frame(handle, 5)};
    CHECK_FALSE(out_of_range_frame.has_value());
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});
    CHECK_EQ(renderer.queue_stats().invalid, std::size_t{1});
    CHECK_EQ(renderer.queue_stats().visible, std::size_t{0});
    REQUIRE_EQ(renderer.queue_stats().skipped.size(), std::size_t{1});
    CHECK_EQ(renderer.queue_stats().skipped.at(0).second, SpriteSkipReason::k_frame_out_of_range);
  }

  TEST_CASE("Reports a missing resource") {
    SpritePool pool;
    const SpriteHandle handle{pool.create(5, 0, 1, {0.0F, 0.0F, 5.0F}).value()};
    pool.set_frame(handle, 0).value();
    pool.attach(handle).value();
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});
    CHECK_EQ(renderer.queue_stats().invalid, std::size_t{1});
    REQUIRE_EQ(renderer.queue_stats().skipped.size(), std::size_t{1});
    CHECK_EQ(renderer.queue_stats().skipped.at(0).second, SpriteSkipReason::k_missing_resource);
  }

  TEST_CASE("Culls sprites behind the camera") {
    SpritePool pool;
    make_attached(pool, {0.0F, 0.0F, -5.0F});
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});
    CHECK_EQ(renderer.queue_stats().culled, std::size_t{1});
    CHECK_EQ(renderer.queue_stats().visible, std::size_t{0});
    REQUIRE_EQ(renderer.queue_stats().skipped.size(), std::size_t{1});
    CHECK_EQ(renderer.queue_stats().skipped.at(0).second, SpriteSkipReason::k_behind_camera);
  }

  TEST_CASE("Culls sprites outside the depth range") {
    SpritePool pool;
    make_attached(pool, {0.0F, 0.0F, 2000.0F});
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});
    CHECK_EQ(renderer.queue_stats().culled, std::size_t{1});
    REQUIRE_EQ(renderer.queue_stats().skipped.size(), std::size_t{1});
    CHECK_EQ(renderer.queue_stats().skipped.at(0).second, SpriteSkipReason::k_outside_depth_range);
  }

  TEST_CASE("Detached instances are not queued") {
    SpritePool pool;
    pool.create(0, 0, 1, {0.0F, 0.0F, 5.0F}).value();  // Never attached.
    const SpriteResource resource{make_resource()};
    const std::array<const SpriteResource*, 1> resources{&resource};

    SpriteRenderer renderer;
    build_queue(renderer, pool, std::span<const SpriteResource* const>{resources});
    CHECK_EQ(renderer.queue_stats().attached, std::size_t{0});
    CHECK_EQ(renderer.queue_stats().visible, std::size_t{0});
    CHECK(renderer.vertices().empty());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

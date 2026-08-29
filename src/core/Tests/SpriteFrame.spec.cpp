#include "Core/Sprite/SpriteFrame.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <expected>
#include <string>

#include "Core/Omikron/Model3DO.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

namespace {

using App::Omikron::Material;
using App::Omikron::MeshDescriptor;
using App::Omikron::MeshPolygons;
using App::Omikron::Model3DOData;
using App::Omikron::RawVertex;
using App::Omikron::Rectangle;
using App::Omikron::Vec3;
using App::Sprite::SpriteFrameError;

/// A one-object model whose object has two frame descriptors (rectangles)
/// over four point records. Frame 0 spans points 0 and 1: width 2 (x: 0→2),
/// height 3 (y: 0→3).
Model3DOData make_sprite_model() {
  Model3DOData model;
  model.materials.emplace_back();

  MeshDescriptor mesh;
  mesh.name = "SPRITE";
  mesh.vertex_count = 4;
  mesh.rectangle_count = 2;
  model.meshes.push_back(mesh);

  MeshPolygons polygons;
  Rectangle frame0;
  frame0.vertices = {0, 2, 1, 3};
  frame0.uv = {0, 0, 12, 34, 255, 255, 56, 78};
  frame0.material_id = 0;
  polygons.rectangles.push_back(frame0);
  Rectangle frame1;
  frame1.vertices = {0, 2, 1, 3};
  frame1.uv = {32, 64, 96, 128, 0, 0, 0, 0};
  frame1.material_id = 0;
  polygons.rectangles.push_back(frame1);
  model.polygons.push_back(polygons);

  model.vertices.resize(4);
  model.vertices.at(0).position = Vec3{0.0F, 0.0F, 0.0F};  // Point 0: origin.
  model.vertices.at(1).position = Vec3{2.0F, 3.0F, 0.0F};  // Point 1: diagonal target.
  model.vertices.at(2).position = Vec3{0.0F, 3.0F, 0.0F};  // Point 2: adjacent corner.
  model.vertices.at(3).position = Vec3{2.0F, 0.0F, 0.0F};  // Point 3: adjacent corner.
  return model;
}

}  // namespace

TEST_SUITE("Core::Sprite::SpriteFrame") {
  TEST_CASE("Frame count falls back to the rectangle table") {
    const Model3DOData model{make_sprite_model()};
    CHECK_EQ(App::Sprite::frame_count(model, 0), std::size_t{2});
  }

  TEST_CASE("Frame count prefers the serialized count when non-zero") {
    Model3DOData model{make_sprite_model()};
    model.header.frame_count = 7;
    CHECK_EQ(App::Sprite::frame_count(model, 0), std::size_t{7});
  }

  TEST_CASE("Frame count is zero for an out-of-range object") {
    const Model3DOData model{make_sprite_model()};
    CHECK_EQ(App::Sprite::frame_count(model, 1), std::size_t{0});
  }

  TEST_CASE("Resolves a valid frame with /256 UVs") {
    const Model3DOData model{make_sprite_model()};
    const auto frame{App::Sprite::resolve_frame(model, 0, 0, 0.0F, 0.0F)};
    REQUIRE(frame.has_value());
    CHECK_EQ(frame->width, doctest::Approx(2.0F));
    CHECK_EQ(frame->height, doctest::Approx(3.0F));
    CHECK_EQ(frame->texture_index, 0);
    CHECK_EQ(frame->uv0.at(0), doctest::Approx(0.0F));
    CHECK_EQ(frame->uv0.at(1), doctest::Approx(0.0F));
    CHECK_EQ(frame->uv1.at(0), doctest::Approx(255.0F / 256.0F));
    CHECK_EQ(frame->uv1.at(1), doctest::Approx(255.0F / 256.0F));
  }

  TEST_CASE("Adds the per-instance texture offsets to the frame UVs") {
    const Model3DOData model{make_sprite_model()};
    const auto frame{App::Sprite::resolve_frame(model, 0, 0, 0.1F, -0.2F)};
    REQUIRE(frame.has_value());
    CHECK_EQ(frame->uv0.at(0), doctest::Approx(0.1F));
    CHECK_EQ(frame->uv0.at(1), doctest::Approx(-0.2F));
    CHECK_EQ(frame->uv1.at(0), doctest::Approx((255.0F / 256.0F) + 0.1F));
    CHECK_EQ(frame->uv1.at(1), doctest::Approx((255.0F / 256.0F) - 0.2F));
  }

  TEST_CASE("Accepts the last valid frame and rejects frame == frame count") {
    const Model3DOData model{make_sprite_model()};
    CHECK(App::Sprite::resolve_frame(model, 0, 1, 0.0F, 0.0F).has_value());
    const auto out{App::Sprite::resolve_frame(model, 0, 2, 0.0F, 0.0F)};
    REQUIRE_FALSE(out.has_value());
    CHECK_EQ(out.error().kind, SpriteFrameError::Kind::k_frame_out_of_range);
  }

  TEST_CASE("Rejects an out-of-range object") {
    const Model3DOData model{make_sprite_model()};
    const auto result{App::Sprite::resolve_frame(model, 1, 0, 0.0F, 0.0F)};
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().kind, SpriteFrameError::Kind::k_object_out_of_range);
  }

  TEST_CASE("Rejects a point index outside the object's vertex block") {
    Model3DOData model{make_sprite_model()};
    model.polygons.at(0).rectangles.at(0).vertices.at(0) = 7;
    const auto result{App::Sprite::resolve_frame(model, 0, 0, 0.0F, 0.0F)};
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().kind, SpriteFrameError::Kind::k_point_out_of_range);
  }

  TEST_CASE("Rejects a texture index outside the material table") {
    Model3DOData model{make_sprite_model()};
    model.polygons.at(0).rectangles.at(0).material_id = 3;
    const auto result{App::Sprite::resolve_frame(model, 0, 0, 0.0F, 0.0F)};
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().kind, SpriteFrameError::Kind::k_texture_out_of_range);
  }

  TEST_CASE("Rejects degenerate dimensions (zero width)") {
    Model3DOData model{make_sprite_model()};
    model.vertices.at(1).position = Vec3{0.0F, 3.0F, 0.0F};  // x equal to point 0.
    const auto result{App::Sprite::resolve_frame(model, 0, 0, 0.0F, 0.0F)};
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().kind, SpriteFrameError::Kind::k_degenerate_dimensions);
  }

  TEST_CASE("Rejects degenerate dimensions (zero height)") {
    Model3DOData model{make_sprite_model()};
    model.vertices.at(1).position = Vec3{2.0F, 0.0F, 0.0F};  // y equal to point 0.
    const auto result{App::Sprite::resolve_frame(model, 0, 0, 0.0F, 0.0F)};
    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(result.error().kind, SpriteFrameError::Kind::k_degenerate_dimensions);
  }

  TEST_CASE("Negative corner deltas resolve to absolute dimensions") {
    Model3DOData model{make_sprite_model()};
    // Diagonal point at (-2, -3): both deltas negative, same absolute size.
    model.vertices.at(1).position = Vec3{-2.0F, -3.0F, 0.0F};
    const auto result{App::Sprite::resolve_frame(model, 0, 0, 0.0F, 0.0F)};
    REQUIRE(result.has_value());
    CHECK_EQ(result->width, doctest::Approx(2.0F));
    CHECK_EQ(result->height, doctest::Approx(3.0F));
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

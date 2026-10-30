#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/SkyRenderer.hpp"
#include "Core/WorldPresentation.hpp"
#include "OmikronTestBuffer.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

namespace {

constexpr std::uint32_t K_ROOT_OFFSET{0x2C};
constexpr std::uint32_t K_HEADER_SIZE{K_ROOT_OFFSET + 0x148};
constexpr std::uint32_t K_MATERIALS_OFFSET{K_HEADER_SIZE};
constexpr std::uint32_t K_VERTICES_OFFSET{K_MATERIALS_OFFSET + 80U};
constexpr std::uint32_t K_TRIANGLES_OFFSET{K_VERTICES_OFFSET + 32U};
constexpr std::uint32_t K_RECTANGLES_OFFSET{K_TRIANGLES_OFFSET + 28U};
constexpr std::uint32_t K_MESHES_OFFSET{K_RECTANGLES_OFFSET};
constexpr std::uint32_t K_LIGHTS_OFFSET{K_MESHES_OFFSET + 140U};

/// One-object sky fixture: a single vertex/triangle, root mesh ID 10 at the
/// given serialized position, carrying `flags`.
Buffer make_sky_model(const std::uint32_t flags,
    const App::Runtime::Vec3 root_position,
    const App::Runtime::Vec3 vertex_position) {
  Buffer buffer;
  buffer.chars("OD3X", 4)
      .u32(4)
      .u32(K_ROOT_OFFSET)
      .u32(K_MATERIALS_OFFSET)
      .u32(K_VERTICES_OFFSET)
      .u32(K_TRIANGLES_OFFSET)
      .u32(K_RECTANGLES_OFFSET)
      .u32(K_MESHES_OFFSET)
      .u32(0)
      .u32(0)
      .u32(K_LIGHTS_OFFSET)
      .zeros(72)
      .u32(0)
      .zeros(104)
      .u32(10)    // root mesh ID
      .f32(1.0F)  // base-light scalar
      .u32(1)     // triangles
      .u32(0)     // rectangles
      .u32(1)     // vertices
      .u64(0)
      .u32(1)  // materials
      .u32(0)
      .u32(0)
      .u32(0)  // cameras
      .u32(1)  // objects
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .zeros(80);
  // Material.
  buffer.chars("CIEL", 20).chars("", 20).chars("", 20).u32(0).u64(0).u32(0).u16(32).u16(32);
  // Vertex: position, normal, unknown, BGRA colour.
  buffer.f32(vertex_position.x)
      .f32(vertex_position.y)
      .f32(vertex_position.z)
      .f32(0.0F)
      .f32(1.0F)
      .f32(0.0F)
      .u32(0)
      .u8(255)
      .u8(255)
      .u8(255)
      .u8(255);
  // Degenerate triangle on vertex 0, material 0.
  buffer.u16(0).u16(0).u16(0).zeros(6).i32(0).f32(0.0F).f32(0.0F).f32(0.0F);
  // Root mesh descriptor.
  buffer.u32(flags)
      .u32(0)
      .u32(10)
      .u32(0)
      .chars("face", 20)
      .f32(root_position.x)
      .f32(root_position.y)
      .f32(root_position.z)
      .i32(-1)
      .i32(-1)
      .i32(-1)
      .u32(0)
      .u32(1)
      .u32(1)
      .u32(0)
      .zeros(64);
  return buffer;
}

}  // namespace

TEST_SUITE("Core::Sky") {
  TEST_CASE("Sky resources resolve from MESHES/MISC") {
    CHECK_EQ(App::Sky::resource_path("ASKY", ".3DO"), std::string{"MESHES/MISC/ASKY.3DO"});
    CHECK_EQ(App::Sky::resource_path("ASKY", ".3DT"), std::string{"MESHES/MISC/ASKY.3DT"});
  }

  TEST_CASE("Sky geometry applies Runtime's load-time scale, lowering and blend clearing") {
    constexpr std::uint32_t k_flags{0x01003000U};
    const Buffer file{make_sky_model(k_flags,
        App::Runtime::Vec3{.x = 28.5F, .y = 401.0F, .z = 909.0F},
        App::Runtime::Vec3{.x = 2.0F, .y = 0.0F, .z = -4.0F})};
    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());

    const auto groups{App::Sky::build_geometry(model.value())};
    REQUIRE(groups.has_value());
    REQUIRE_EQ(groups->size(), std::size_t{1});
    const App::Omikron::MaterialGroup& group{groups->front()};
    // Bits 12/13 are cleared, bits 16/17 set; the U-scroll bit survives.
    CHECK_EQ(group.flags, 0x01030000U);
    CHECK_EQ(App::Omikron::blend_mode(group.flags), App::Omikron::BlendMode::k_opaque);
    REQUIRE_FALSE(group.vertices.empty());
    // X/Z are camera-relative (the authored root X/Z are replaced every frame),
    // Y is the serialized root Y lowered by 2250, and the object is scaled by 12.5.
    CHECK(group.vertices.front().position.at(0) == doctest::Approx(25.0F));
    CHECK(group.vertices.front().position.at(1) == doctest::Approx(401.0F - 2250.0F));
    CHECK(group.vertices.front().position.at(2) == doctest::Approx(-50.0F));
  }

  TEST_CASE("Sky follows the camera on X/Z only") {
    const App::Runtime::Transform transform{
        App::Sky::follow_transform(App::Runtime::Vec3{.x = 120.0F, .y = -75.0F, .z = 3300.0F})};
    CHECK(transform.translation.x == doctest::Approx(120.0F));
    CHECK(transform.translation.y == doctest::Approx(0.0F));
    CHECK(transform.translation.z == doctest::Approx(3300.0F));
  }

  TEST_CASE("Area loads select, preserve and release the global sky like Runtime") {
    App::WorldPresentationState presentation;
    CHECK(presentation.sky_name().empty());

    presentation.apply_area_sky("", "ASKY");
    CHECK_EQ(presentation.sky_name(), std::string{"ASKY"});

    // A destination without a sky preserves the current AREA's sky.
    presentation.apply_area_sky("ASKY", "");
    CHECK_EQ(presentation.sky_name(), std::string{"ASKY"});

    // A destination sky replaces the selection.
    presentation.apply_area_sky("ASKY", "SSKY");
    CHECK_EQ(presentation.sky_name(), std::string{"SSKY"});

    // Neither the current nor the destination AREA declares a sky.
    presentation.apply_area_sky("", "");
    CHECK(presentation.sky_name().empty());

    presentation.apply_area_sky("", "LSKY");
    presentation.clear();
    CHECK(presentation.sky_name().empty());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

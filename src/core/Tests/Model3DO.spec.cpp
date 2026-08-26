#include "Core/Omikron/Model3DO.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "Core/RuntimeMath.hpp"
#include "OmikronTestBuffer.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

namespace {

constexpr std::size_t K_DIRECTORY_SIZE{0x2C};
constexpr std::size_t K_ROOT_SIZE{0x148};
constexpr std::size_t K_MATERIAL_SIZE{80};
constexpr std::size_t K_VERTEX_SIZE{32};
constexpr std::size_t K_TRIANGLE_SIZE{28};
constexpr std::size_t K_MESH_SIZE{140};
constexpr std::size_t K_CAMERA_SIZE{52};
constexpr std::size_t K_LIGHT_SIZE{304};

/// Builds a header whose sections immediately follow it in the order
/// materials, vertices, triangles, rectangles, meshes, cameras, lights.
Buffer make_header(const std::uint32_t material_count,
    const std::uint32_t mesh_count,
    const std::uint32_t vertex_count,
    const std::uint32_t triangle_count,
    const std::uint32_t rectangle_count,
    const std::uint32_t lights_unknown1 = 0,
    const std::uint32_t lights_unknown2 = 0,
    const std::uint32_t frame_count = 0,
    const std::uint32_t texture_count = 0,
    const std::uint32_t object_count = 0,
    const std::uint32_t root_mesh_id = 1,
    const std::uint32_t root_offset = K_DIRECTORY_SIZE,
    const std::uint32_t camera_count = 0) {
  // These two legacy fixture arguments used to populate fields from the
  // incorrect flat-header interpretation. Keep their positions temporarily
  // so existing tests do not need unrelated call-site churn.
  static_cast<void>(texture_count);
  static_cast<void>(object_count);

  const std::size_t header_size{static_cast<std::size_t>(root_offset) + K_ROOT_SIZE};
  const std::size_t materials_offset{header_size};
  const std::size_t vertices_offset{
      materials_offset + (static_cast<std::size_t>(material_count) * K_MATERIAL_SIZE)};
  const std::size_t triangles_offset{
      vertices_offset + (static_cast<std::size_t>(vertex_count) * K_VERTEX_SIZE)};
  const std::size_t rectangles_offset{
      triangles_offset + (static_cast<std::size_t>(triangle_count) * K_TRIANGLE_SIZE)};
  const std::size_t meshes_offset{
      rectangles_offset + (static_cast<std::size_t>(rectangle_count) * 32U)};
  const std::size_t cameras_offset{
      meshes_offset + (static_cast<std::size_t>(mesh_count) * K_MESH_SIZE)};
  const std::size_t lights_offset{
      cameras_offset + (static_cast<std::size_t>(camera_count) * K_CAMERA_SIZE)};

  Buffer buffer;
  // Real files start with the OD3X signature and version 4.
  buffer.chars("OD3X", 4)
      .u32(4)
      .u32(root_offset)
      .u32(static_cast<std::uint32_t>(materials_offset))
      .u32(static_cast<std::uint32_t>(vertices_offset))
      .u32(static_cast<std::uint32_t>(triangles_offset))
      .u32(static_cast<std::uint32_t>(rectangles_offset))
      .u32(static_cast<std::uint32_t>(meshes_offset))
      .u32(0)
      .u32(camera_count == 0U ? 0U : static_cast<std::uint32_t>(cameras_offset))
      .u32(static_cast<std::uint32_t>(lights_offset))  // relationships, cameras, lights.
      .zeros(root_offset - K_DIRECTORY_SIZE)
      .zeros(72)
      .u32(frame_count)
      .zeros(104)
      .u32(root_mesh_id)
      .f32(1.0F)  // +0xB8 Runtime base-light scalar.
      .u32(triangle_count)
      .u32(rectangle_count)
      .u32(vertex_count)
      .u64(0)
      .u32(material_count)
      .u32(0)
      .u32(0)
      .u32(camera_count)                       // +0xDC camera count.
      .u32(mesh_count)                         // +0xE0 object count.
      .u32(0)                                  // +0xE4 relationship count.
      .u32(lights_unknown1 + lights_unknown2)  // +0xE8 serialized light count.
      .u32(lights_unknown1)
      .u32(lights_unknown2)
      .zeros(84);
  return buffer;
}

/// Appends a minimal mesh descriptor (one float position on X).
void append_mesh(Buffer& buffer,
    const std::uint32_t flags,
    const std::uint32_t mesh_id,
    const std::int32_t parent_id,
    const std::uint32_t vertex_count,
    const std::uint32_t triangle_count,
    const std::uint32_t rectangle_count,
    const float position_x,
    const std::int32_t first_child_id = -1,
    const std::int32_t next_sibling_id = -1,
    const float bone_position_x = 0.0F) {
  buffer.u32(flags)
      .u32(0)
      .u32(mesh_id)
      .u32(0)
      .chars("MESH", 20)
      .f32(position_x)
      .f32(0.0F)
      .f32(0.0F)
      .i32(parent_id)
      .i32(first_child_id)
      .i32(next_sibling_id)
      .u32(0)
      .u32(vertex_count)
      .u32(triangle_count)
      .u32(rectangle_count)
      .f32(0.0F)
      .f32(0.0F)
      .f32(0.0F)
      .f32(0.0F)
      .f32(1.0F)
      .f32(1.0F)
      .f32(1.0F)
      .f32(2.0F)
      .f32(2.0F)
      .f32(2.0F)
      .f32(0.0F)
      .f32(0.0F)
      .f32(0.0F)
      .f32(bone_position_x)
      .f32(0.0F)
      .f32(0.0F);
}

void append_material(Buffer& buffer, const std::string_view name = "MATERIAL") {
  buffer.chars(name, 20).chars("", 20).chars("", 20).u32(0).u64(0).u32(0).u16(32).u16(32);
}

void append_vertex(Buffer& buffer, const float position_x = 0.0F) {
  buffer.f32(position_x)
      .f32(0.0F)
      .f32(0.0F)
      .f32(0.0F)
      .f32(0.0F)
      .f32(1.0F)
      .u32(0)
      .u8(0)
      .u8(0)
      .u8(0)
      .u8(255);
}

void append_degenerate_triangle(Buffer& buffer, const std::int32_t material_id = 0) {
  buffer.u16(0)
      .u16(0)
      .u16(0)
      .u8(0)
      .u8(0)
      .u8(0)
      .u8(0)
      .u8(0)
      .u8(0)
      .i32(material_id)
      .i32(0)
      .i32(0)
      .i32(0);
}

/// Appends one 304-byte light record in serialized native XYZ order.
void append_light(Buffer& buffer,
    const std::uint32_t flags,
    const std::string_view name,
    const float attenuation_end,
    const float attenuation_start,
    const float intensity,
    const std::array<std::uint8_t, 4> color_bgra,
    const std::array<float, 3> position,
    const std::array<float, 3> target) {
  buffer.u32(flags)
      .chars(name, 20)
      .f32(attenuation_end)
      .f32(attenuation_start)
      .f32(intensity)
      .f32(0.0F)
      .f32(0.0F)
      .u8(color_bgra.at(0))
      .u8(color_bgra.at(1))
      .u8(color_bgra.at(2))
      .u8(color_bgra.at(3));
  buffer.f32(position.at(0)).f32(position.at(1)).f32(position.at(2)).zeros(20);
  buffer.f32(target.at(0)).f32(target.at(1)).f32(target.at(2)).zeros(20);
  for (std::size_t slot{0}; slot < 4U; ++slot) {
    buffer.f32(0.0F).f32(0.0F).f32(0.0F).zeros(20);
  }
  buffer.zeros(64);
}

void append_camera(Buffer& buffer,
    const std::string_view name,
    const std::array<float, 3> eye,
    const std::array<float, 3> target,
    const float roll_degrees,
    const float horizontal_fov_degrees) {
  buffer.chars(name, 20)
      .f32(eye.at(0))
      .f32(eye.at(1))
      .f32(eye.at(2))
      .f32(target.at(0))
      .f32(target.at(1))
      .f32(target.at(2))
      .f32(roll_degrees)
      .f32(horizontal_fov_degrees);
}

}  // namespace

TEST_SUITE("Core::Omikron::Model3DO") {
  TEST_CASE("Parses a header-only file") {
    const Buffer file{make_header(0, 0, 0, 0, 0)};

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());

    CHECK_EQ(
        std::string_view{model->header.signature.data(), model->header.signature.size()}, "OD3X");
    CHECK_EQ(model->header.version_major, 4U);
    CHECK_EQ(model->header.root_offset, K_DIRECTORY_SIZE);
    CHECK_EQ(model->header.material_count, 0U);
    CHECK_EQ(model->header.object_count, 0U);
    CHECK_EQ(model->header.mesh_count, 0U);
    CHECK(model->materials.empty());
    CHECK(model->meshes.empty());
    CHECK(model->vertices.empty());
    CHECK(model->cameras.empty());
    CHECK(model->lights.empty());
  }

  TEST_CASE("Parses named 0x34-byte scene cameras in Runtime-native units") {
    Buffer file{make_header(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, K_DIRECTORY_SIZE, 1)};
    append_camera(file,
        "CAMERA",
        {10.0F, -20.0F, 30.0F},
        {40.0F, 50.0F, -60.0F},
        15.0F,
        80.0F);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());
    REQUIRE_EQ(model->cameras.size(), std::size_t{1});
    const App::Omikron::CameraRecord& camera{model->cameras.front()};
    CHECK_EQ(camera.name, "CAMERA");
    CHECK(camera.eye.x == doctest::Approx(10.0F));
    CHECK(camera.eye.y == doctest::Approx(-20.0F));
    CHECK(camera.eye.z == doctest::Approx(30.0F));
    CHECK(camera.target.x == doctest::Approx(40.0F));
    CHECK(camera.target.y == doctest::Approx(50.0F));
    CHECK(camera.target.z == doctest::Approx(-60.0F));
    CHECK(camera.roll_degrees == doctest::Approx(15.0F));
    CHECK(camera.horizontal_fov_degrees == doctest::Approx(80.0F));
  }

  TEST_CASE("Preserves recovered root fields") {
    const Buffer file{make_header(0, 0, 0, 0, 0, 0, 0, 5)};

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());

    CHECK_EQ(model->header.frame_count, 5U);
    CHECK_EQ(model->header.base_light_level, doctest::Approx(1.0F));
    CHECK_EQ(model->header.material_count, 0U);
    CHECK_EQ(model->header.object_count, 0U);
    CHECK_EQ(model->header.texture_count, 0U);
  }

  TEST_CASE("Seeks a non-default serialized root offset") {
    constexpr std::uint32_t root_offset{0x60};
    Buffer file{make_header(0, 1, 0, 0, 0, 0, 0, 5, 7, 3, 42, root_offset)};
    append_mesh(file, 0, 42, -1, 0, 0, 0, 0.0F);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());
    CHECK_EQ(model->header.root_offset, root_offset);
    CHECK_EQ(model->header.root_mesh_id, 42U);
    CHECK_EQ(model->root_mesh_index, 0);
    CHECK_EQ(model->header.frame_count, 5U);
    CHECK_EQ(model->header.texture_count, 0U);
    CHECK_EQ(model->header.object_count, 1U);
    CHECK_EQ(model->header.mesh_count, 1U);
  }

  TEST_CASE("Does not read past the 0x148-byte serialized root") {
    Buffer file{make_header(0, 0, 0, 0, 0)};
    // Exact value observed as HO1_FNM's bogus mesh count when parsing ran
    // beyond the real root into following data.
    file.u32(0x416FB251U);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());
    CHECK_EQ(model->header.object_count, 0U);
    CHECK_EQ(model->header.mesh_count, 0U);
    CHECK(model->meshes.empty());
  }

  TEST_CASE("Rejects an unresolved root object instead of using a parentless fallback") {
    Buffer file{make_header(0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 99)};
    append_mesh(file, 0, 1, -1, 0, 0, 0, 0.0F);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().find("root mesh ID 99 does not resolve") != std::string::npos);
  }

  TEST_CASE("Parses materials, vertices and mesh descriptors") {
    Buffer file{make_header(1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 7)};

    // Material.
    file.chars("SKIN", 20)
        .chars("skin.bmp", 20)
        .chars("skin.tga", 20)
        .u32(99)
        .u16(0xFFFF)
        .u16(0xFFFF)
        .u16(0xFFFF)
        .u16(0xFFFF)
        .u16(8)
        .u8(3)
        .u8(5)
        .u16(64)
        .u16(64);
    // Vertex: native position (1, 2, 3), normal (0, 1, 0), colour BGRA.
    file.f32(1.0F)
        .f32(2.0F)
        .f32(3.0F)
        .f32(0.0F)
        .f32(1.0F)
        .f32(0.0F)
        .u32(42)
        .u8(0x11)
        .u8(0x22)
        .u8(0x33)
        .u8(0x44);
    // Triangle.
    file.u16(0).u16(0).u16(0).u8(1).u8(2).u8(3).u8(4).u8(5).u8(6).i32(0).i32(0).i32(0).i32(0);
    // Mesh descriptor.
    append_mesh(file, 0, 7, -1, 1, 1, 0, 0.0F);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());

    REQUIRE_EQ(model->materials.size(), std::size_t{1});
    CHECK_EQ(model->materials.at(0).name, "SKIN");
    CHECK_EQ(model->materials.at(0).texture_name, "skin.bmp");
    CHECK_EQ(model->materials.at(0).palette_name, "skin.tga");
    CHECK_EQ(model->materials.at(0).data_size, 99U);
    CHECK_EQ(model->materials.at(0).texture_page_index, 0xFFFFU);
    CHECK_EQ(model->materials.at(0).texture_slot_index, 0xFFFFU);
    CHECK_EQ(model->materials.at(0).palette_page_index, 0xFFFFU);
    CHECK_EQ(model->materials.at(0).palette_slot_index, 0xFFFFU);
    CHECK_EQ(model->materials.at(0).bits_per_pixel, 8U);
    CHECK_EQ(model->materials.at(0).atlas_u_offset, 3U);
    CHECK_EQ(model->materials.at(0).atlas_v_offset, 5U);
    CHECK_EQ(model->materials.at(0).width, 64U);
    CHECK_EQ(model->materials.at(0).height, 64U);

    REQUIRE_EQ(model->meshes.size(), std::size_t{1});
    CHECK_EQ(model->meshes.at(0).mesh_id, 7U);
    CHECK_EQ(model->meshes.at(0).vertex_base, std::size_t{0});
    CHECK_EQ(model->meshes.at(0).triangle_byte_offset, std::size_t{0});

    REQUIRE_EQ(model->vertices.size(), std::size_t{1});
    // Authoritative parsing preserves ordinary serialized native XYZ.
    CHECK_EQ(model->vertices.at(0).position.x, doctest::Approx(1.0F));
    CHECK_EQ(model->vertices.at(0).position.y, doctest::Approx(2.0F));
    CHECK_EQ(model->vertices.at(0).position.z, doctest::Approx(3.0F));
    CHECK_EQ(model->vertices.at(0).normal.x, doctest::Approx(0.0F));
    CHECK_EQ(model->vertices.at(0).normal.y, doctest::Approx(1.0F));
    CHECK_EQ(model->vertices.at(0).normal.z, doctest::Approx(0.0F));
    CHECK_EQ(model->vertices.at(0).unknown_t1, 42U);
    CHECK_EQ(model->vertices.at(0).color_bgra, std::array<std::uint8_t, 4>{0x11, 0x22, 0x33, 0x44});

    REQUIRE_EQ(model->polygons.at(0).triangles.size(), std::size_t{1});
    const auto& triangle{model->polygons.at(0).triangles.at(0)};
    CHECK_EQ(triangle.material_id, 0);
    CHECK_EQ(triangle.vertices.at(0).index, 0U);
    CHECK_FALSE(triangle.vertices.at(0).parented);
  }

  TEST_CASE("Triangle references decode the parented flag and index mask") {
    Buffer file{make_header(0, 1, 0, 1, 0)};

    file.u16(0x8005)
        .u16(7)
        .u16(0x8FFF)
        .u8(0)
        .u8(0)
        .u8(0)
        .u8(0)
        .u8(0)
        .u8(0)
        .i32(-1)
        .i32(0)
        .i32(0)
        .i32(0);
    append_mesh(file, 0, 1, -1, 0, 1, 0, 0.0F);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());

    const auto& triangle{model->polygons.at(0).triangles.at(0)};
    CHECK_EQ(triangle.vertices.at(0).index, 5U);
    CHECK(triangle.vertices.at(0).parented);
    CHECK_EQ(triangle.vertices.at(1).index, 7U);
    CHECK_FALSE(triangle.vertices.at(1).parented);
    CHECK_EQ(triangle.vertices.at(2).index, 0x03FFU);
    CHECK(triangle.vertices.at(2).parented);
    CHECK_EQ(triangle.material_id, -1);
  }

  TEST_CASE("Skin parents skip joint-only meshes") {
    Buffer file{make_header(0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 10)};

    append_mesh(file, 0, 10, -1, 0, 0, 0, 0.0F);  // Root.
    append_mesh(file, 1, 11, 10, 0, 0, 0, 0.0F);  // Joint-only (flag bit 0).
    append_mesh(file, 0, 12, 11, 0, 0, 0, 0.0F);  // Child.

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());

    CHECK_EQ(model->hierarchy_parent_index, std::vector<std::int32_t>{-1, 0, 1});
    CHECK_EQ(model->skin_parent_index, std::vector<std::int32_t>{-1, 0, 0});
  }

  TEST_CASE("Static geometry applies mesh positions and resolves parented corners") {
    Buffer file{make_header(1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 10)};

    // Material 64x64.
    file.chars("SKIN", 20).chars("", 20).chars("", 20).u32(0).u64(0).u32(0).u16(64).u16(64);
    // Vertex 0 (own block of mesh 0) and vertex 1 (own block of mesh 1).
    file.f32(0.0F).f32(0.0F).f32(0.0F).f32(0.0F).f32(0.0F).f32(1.0F).u32(0).u8(0).u8(0).u8(0).u8(
        255);
    file.f32(40.0F).f32(0.0F).f32(0.0F).f32(0.0F).f32(0.0F).f32(1.0F).u32(0).u8(0).u8(0).u8(0).u8(
        255);
    // Triangle: corner 0 parented, corners 1-2 own.
    file.u16(0x8000).u16(0).u16(0).u8(32).u8(64).u8(0).u8(0).u8(16).u8(32).i32(0).i32(0).i32(0).i32(
        0);
    append_mesh(file, 0, 10, -1, 1, 0, 0, 0.0F, 11);  // Root origin (0, 0, 0).
    append_mesh(file, 0, 11, 10, 1, 1, 0, 40.0F, -1, -1, 40.0F);
    // Child Runtime origin is its parent's origin plus native offset (40, 0, 0).

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());

    const auto groups{App::Omikron::Model3DO::build_static_geometry(model.value())};
    REQUIRE(groups.has_value());
    REQUIRE_EQ(groups->size(), std::size_t{1});

    const auto& group{groups->at(0)};
    CHECK_EQ(group.material_id, 0);
    REQUIRE_EQ(group.vertices.size(), std::size_t{3});
    CHECK_EQ(group.indices, std::vector<std::uint32_t>{0U, 1U, 2U});

    // Block-local positions plus the owning mesh's bind-pose world origin.
    CHECK_EQ(group.vertices.at(0).position.at(0), doctest::Approx(0.0F));
    CHECK_EQ(group.vertices.at(0).position.at(1), doctest::Approx(0.0F));
    CHECK_EQ(group.vertices.at(1).position.at(0), doctest::Approx(80.0F));
    CHECK_EQ(group.vertices.at(2).position.at(0), doctest::Approx(80.0F));

    // UVs are pixel coordinates divided by the texture size.
    CHECK_EQ(group.vertices.at(0).uv.at(0), doctest::Approx(0.5F));
    CHECK_EQ(group.vertices.at(0).uv.at(1), doctest::Approx(1.0F));
    CHECK_EQ(group.vertices.at(2).uv.at(0), doctest::Approx(0.25F));
    CHECK_EQ(group.vertices.at(2).uv.at(1), doctest::Approx(0.5F));
  }

  TEST_CASE("Runtime hierarchy composes child offsets through parent rotation") {
    Buffer file{make_header(0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 10)};
    append_mesh(file, 0, 10, -1, 0, 0, 0, 5.0F, 11);
    append_mesh(file, 0, 11, 10, 0, 0, 0, 0.0F, -1, -1, 10.0F);

    auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());
    model->runtime_objects.at(0).local_matrix =
        App::Runtime::rotation_z(std::numbers::pi_v<float> * 0.5F);
    const auto resolved{App::Omikron::Model3DO::resolve_runtime_transforms(model.value())};
    REQUIRE(resolved.has_value());

    const auto& root{model->runtime_objects.at(0)};
    CHECK(root.world_translation.x == doctest::Approx(5.0F));
    CHECK(root.world_translation.y == doctest::Approx(0.0F));
    const auto& child{model->runtime_objects.at(1)};
    CHECK(child.world_translation.x == doctest::Approx(5.0F));
    CHECK(child.world_translation.y == doctest::Approx(-10.0F));
    CHECK(child.world_translation.z == doctest::Approx(0.0F));
  }

  TEST_CASE("Root-level siblings use serialized positions and remain renderable") {
    Buffer file{make_header(1, 3, 2, 2, 0, 0, 0, 0, 0, 0, 10)};
    append_material(file, "PORTAL");
    append_vertex(file);
    append_vertex(file);
    append_degenerate_triangle(file);
    append_degenerate_triangle(file);
    append_mesh(file, 0, 10, -1, 1, 1, 0, 100.0F, -1, 11);
    append_mesh(file, 0, 11, -1, 1, 1, 0, 200.0F, -1, -1, 0.0F);
    append_mesh(file, 0, 12, -1, 0, 0, 0, 300.0F);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());
    CHECK_EQ(model->hierarchy_reachable, std::vector<std::uint8_t>{1U, 1U, 0U});
    CHECK(model->runtime_objects.at(0).world_translation.x == doctest::Approx(100.0F));
    CHECK(model->runtime_objects.at(1).local_offset.x == doctest::Approx(200.0F));
    CHECK(model->runtime_objects.at(1).world_translation.x == doctest::Approx(200.0F));

    const auto groups{App::Omikron::Model3DO::build_static_geometry(model.value())};
    REQUIRE(groups.has_value());
    REQUIRE_EQ(groups->size(), std::size_t{2});
    CHECK_EQ(groups->at(0).mesh_index, std::size_t{0});
    CHECK_EQ(groups->at(1).mesh_index, std::size_t{1});
    REQUIRE_EQ(groups->at(0).vertices.size(), std::size_t{3});
    REQUIRE_EQ(groups->at(1).vertices.size(), std::size_t{3});
    CHECK(groups->at(0).vertices.at(0).position.at(0) == doctest::Approx(100.0F));
    CHECK(groups->at(1).vertices.at(0).position.at(0) == doctest::Approx(200.0F));
  }

  TEST_CASE("Nested child siblings retain parent-relative bone offsets") {
    Buffer file{make_header(0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 10)};
    append_mesh(file, 0, 10, -1, 0, 0, 0, 100.0F, 11);
    append_mesh(file, 0, 11, 10, 0, 0, 0, 1000.0F, -1, 12, 10.0F);
    append_mesh(file, 0, 12, 10, 0, 0, 0, 2000.0F, -1, -1, 20.0F);
    append_mesh(file, 0, 13, -1, 0, 0, 0, 300.0F);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());
    CHECK_EQ(model->hierarchy_reachable, std::vector<std::uint8_t>{1U, 1U, 1U, 0U});
    CHECK(model->runtime_objects.at(1).local_offset.x == doctest::Approx(10.0F));
    CHECK(model->runtime_objects.at(1).world_translation.x == doctest::Approx(110.0F));
    CHECK(model->runtime_objects.at(2).local_offset.x == doctest::Approx(20.0F));
    CHECK(model->runtime_objects.at(2).world_translation.x == doctest::Approx(120.0F));
  }

  TEST_CASE("Rejects a cycle in the top-level sibling chain") {
    Buffer file{make_header(0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 10)};
    append_mesh(file, 0, 10, -1, 0, 0, 0, 0.0F, -1, 11);
    append_mesh(file, 0, 11, -1, 0, 0, 0, 0.0F, -1, 10);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().find("cycle in 3DO sibling chain") != std::string::npos);
  }

  TEST_CASE("Reachable invisible siblings do not emit static geometry") {
    Buffer file{make_header(1, 2, 2, 2, 0, 0, 0, 0, 0, 0, 10)};
    append_material(file);
    append_vertex(file);
    append_vertex(file);
    append_degenerate_triangle(file);
    append_degenerate_triangle(file);
    append_mesh(file, 0, 10, -1, 1, 1, 0, 100.0F, -1, 11);
    append_mesh(file,
        static_cast<std::uint32_t>(App::Omikron::MeshFlags::k_invisible),
        11,
        -1,
        1,
        1,
        0,
        200.0F);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());
    CHECK_EQ(model->hierarchy_reachable, std::vector<std::uint8_t>{1U, 1U});
    const auto groups{App::Omikron::Model3DO::build_static_geometry(model.value())};
    REQUIRE(groups.has_value());
    REQUIRE_EQ(groups->size(), std::size_t{1});
    CHECK_EQ(groups->at(0).vertices.size(), std::size_t{3});
    CHECK(groups->at(0).vertices.at(0).position.at(0) == doctest::Approx(100.0F));
  }

  TEST_CASE("Rectangles are split into two triangles") {
    Buffer file{make_header(1, 1, 4, 0, 1)};

    file.chars("SKIN", 20).chars("", 20).chars("", 20).u32(0).u64(0).u32(0).u16(64).u16(32);
    // Four vertices in a native 40-by-40 quad.
    file.f32(0.0F).f32(0.0F).f32(0.0F).f32(0.0F).f32(0.0F).f32(1.0F).u32(0).u8(0).u8(0).u8(0).u8(
        255);
    file.f32(40.0F).f32(0.0F).f32(0.0F).f32(0.0F).f32(0.0F).f32(1.0F).u32(0).u8(0).u8(0).u8(0).u8(
        255);
    file.f32(40.0F).f32(0.0F).f32(40.0F).f32(0.0F).f32(0.0F).f32(1.0F).u32(0).u8(0).u8(0).u8(0).u8(
        255);
    file.f32(0.0F).f32(0.0F).f32(40.0F).f32(0.0F).f32(0.0F).f32(1.0F).u32(0).u8(0).u8(0).u8(0).u8(
        255);
    // Rectangle (0, 1, 2, 3) with UV bytes 0..7.
    file.u16(0)
        .u16(1)
        .u16(2)
        .u16(3)
        .u8(0)
        .u8(1)
        .u8(2)
        .u8(3)
        .u8(4)
        .u8(5)
        .u8(6)
        .u8(7)
        .i32(0)
        .i32(0)
        .i32(0)
        .i32(0);
    append_mesh(file, 0, 1, -1, 4, 0, 1, 0.0F);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());

    const auto groups{App::Omikron::Model3DO::build_static_geometry(model.value())};
    REQUIRE(groups.has_value());
    REQUIRE_EQ(groups->size(), std::size_t{1});

    const auto& group{groups->at(0)};
    REQUIRE_EQ(group.vertices.size(), std::size_t{6});
    CHECK_EQ(group.indices, std::vector<std::uint32_t>{0U, 1U, 2U, 3U, 4U, 5U});
    // First triangle reuses corners 0, 1, 2; the second uses 0, 2, 3.
    CHECK_EQ(group.vertices.at(0).position.at(0), doctest::Approx(0.0F));
    CHECK_EQ(group.vertices.at(2).position.at(0), doctest::Approx(40.0F));
    CHECK_EQ(group.vertices.at(3).position.at(0), doctest::Approx(0.0F));
    CHECK_EQ(group.vertices.at(4).position.at(2), doctest::Approx(40.0F));
    CHECK_EQ(group.vertices.at(5).position.at(2), doctest::Approx(40.0F));
    // The last corner carries UV bytes 6, 7 of the rectangle.
    CHECK_EQ(group.vertices.at(5).uv.at(0), doctest::Approx(6.0F / 64.0F));
    CHECK_EQ(group.vertices.at(5).uv.at(1), doctest::Approx(7.0F / 32.0F));
  }

  TEST_CASE("Invalid material ids are clamped to zero") {
    Buffer file{make_header(1, 1, 1, 1, 0)};

    file.chars("SKIN", 20).chars("", 20).chars("", 20).u32(0).u64(0).u32(0).u16(32).u16(32);
    file.f32(0.0F).f32(0.0F).f32(0.0F).f32(0.0F).f32(0.0F).f32(1.0F).u32(0).u8(0).u8(0).u8(0).u8(
        255);
    file.u16(0).u16(0).u16(0).u8(0).u8(0).u8(0).u8(0).u8(0).u8(0).i32(-1).i32(0).i32(0).i32(0);
    append_mesh(file, 0, 1, -1, 1, 1, 0, 0.0F);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());

    const auto groups{App::Omikron::Model3DO::build_static_geometry(model.value())};
    REQUIRE(groups.has_value());
    REQUIRE_EQ(groups->size(), std::size_t{1});
    CHECK_EQ(groups->at(0).material_id, 0);
  }

  TEST_CASE("Rejects truncated files") {
    Buffer file{make_header(1, 1, 1, 1, 0)};
    // Material and vertex are missing: parsing must fail cleanly.
    file.chars("SKIN", 20);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    CHECK_FALSE(model.has_value());
  }

  TEST_CASE("Static geometry requires visible geometry") {
    Buffer file{make_header(0, 1, 0, 0, 0)};
    append_mesh(file, 0, 1, -1, 0, 0, 0, 0.0F);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());

    const auto groups{App::Omikron::Model3DO::build_static_geometry(model.value())};
    CHECK_FALSE(groups.has_value());
  }

  TEST_CASE("UV-scroll-U flag survives static geometry") {
    Buffer file{make_header(1, 1, 1, 1, 0)};

    file.chars("SCROLL", 20).chars("", 20).chars("", 20).u32(0).u64(0).u32(0).u16(32).u16(32);
    file.f32(0.0F).f32(0.0F).f32(0.0F).f32(0.0F).f32(0.0F).f32(1.0F).u32(0).u8(0).u8(0).u8(0).u8(
        255);
    file.u16(0).u16(0).u16(0).u8(0).u8(0).u8(0).u8(0).u8(0).u8(0).i32(0).i32(0).i32(0).i32(0);
    append_mesh(file, 1U << 24, 1, -1, 1, 1, 0, 0.0F);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());

    const auto groups{App::Omikron::Model3DO::build_static_geometry(model.value())};
    REQUIRE(groups.has_value());
    REQUIRE_EQ(groups->size(), std::size_t{1});

    const auto& group{groups->at(0)};
    CHECK(App::Omikron::has_flag(group.flags, App::Omikron::MeshFlags::k_uv_scroll_u));
    // UV scrolling is orthogonal to framebuffer blend selection.
    CHECK_EQ(App::Omikron::blend_mode(group.flags), App::Omikron::BlendMode::k_opaque);
  }

  TEST_CASE("Parses explicit light records") {
    Buffer file{make_header(0, 0, 0, 0, 0, 0, 1)};

    // Position (40, 80, 0) and target (40, 80, 40) in file space convert to
    // (1, -2, 0) and (1, -2, -1) in world space.
    append_light(file,
        0x00040002U,
        "CEILING",
        120.0F,
        30.0F,
        2.5F,
        std::array<std::uint8_t, 4>{0x11, 0x22, 0x33, 0x44},
        std::array<float, 3>{40.0F, 80.0F, 0.0F},
        std::array<float, 3>{40.0F, 80.0F, 40.0F});

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());
    REQUIRE_EQ(model->lights.size(), std::size_t{1});

    const App::Omikron::Light& light{model->lights.at(0)};
    CHECK_EQ(light.flags, 0x00040002U);
    CHECK_EQ(light.name, "CEILING");
    CHECK_EQ(light.attenuation_end, doctest::Approx(120.0F));
    CHECK_EQ(light.attenuation_start, doctest::Approx(30.0F));
    CHECK_EQ(light.intensity, doctest::Approx(2.5F));
    CHECK_EQ(light.unknown4, doctest::Approx(0.0F));
    CHECK_EQ(light.unknown5, doctest::Approx(0.0F));
    CHECK_EQ(light.color_bgra, (std::array<std::uint8_t, 4>{0x11, 0x22, 0x33, 0x44}));

    CHECK_EQ(light.points.at(0).x, doctest::Approx(40.0F));
    CHECK_EQ(light.points.at(0).y, doctest::Approx(80.0F));
    CHECK_EQ(light.points.at(0).z, doctest::Approx(0.0F));
    CHECK_EQ(light.points.at(1).x, doctest::Approx(40.0F));
    CHECK_EQ(light.points.at(1).y, doctest::Approx(80.0F));
    CHECK_EQ(light.points.at(1).z, doctest::Approx(40.0F));

    const App::Omikron::Vec3 direction{light.direction()};
    CHECK_EQ(direction.x, doctest::Approx(0.0F));
    CHECK_EQ(direction.y, doctest::Approx(0.0F));
    CHECK_EQ(direction.z, doctest::Approx(1.0F));

    const std::array<float, 4> color{light.color_rgba()};
    CHECK_EQ(color.at(0), doctest::Approx(0x33 / 255.0F));
    CHECK_EQ(color.at(1), doctest::Approx(0x22 / 255.0F));
    CHECK_EQ(color.at(2), doctest::Approx(0x11 / 255.0F));
    CHECK_EQ(color.at(3), doctest::Approx(0x44 / 255.0F));
  }

  TEST_CASE("Degenerate light targets become point lights") {
    App::Omikron::Light light;
    light.points.at(0) = App::Omikron::Vec3{.x = 1.0F, .y = 2.0F, .z = 3.0F};
    light.points.at(1) = light.points.at(0);

    const App::Omikron::Vec3 direction{light.direction()};
    CHECK_EQ(direction.x, doctest::Approx(0.0F));
    CHECK_EQ(direction.y, doctest::Approx(0.0F));
    CHECK_EQ(direction.z, doctest::Approx(0.0F));
  }

  TEST_CASE("Mesh-light counts have no record section") {
    const Buffer file{make_header(0, 0, 0, 0, 0, 3, 0)};

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE(model.has_value());
    CHECK(model->lights.empty());
    CHECK_EQ(model->header.lights_unknown1, 3U);
    CHECK_EQ(model->header.lights_unknown2, 0U);
  }

  TEST_CASE("Rejects truncated light records") {
    Buffer file{make_header(0, 0, 0, 0, 0, 0, 1)};
    // Only part of a light record is present.
    file.u32(0).chars("L", 20);

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().find("lights") != std::string::npos);
  }

  TEST_CASE("Rejects implausible light counts") {
    const Buffer file{make_header(0, 0, 0, 0, 0, 0, 0xFFFF'FFFFU)};

    const auto model{App::Omikron::Model3DO::load(file.data())};
    REQUIRE_FALSE(model.has_value());
    CHECK(model.error().find("implausible light count") != std::string::npos);
  }

  TEST_CASE("Mesh flags map to blend modes") {
    using App::Omikron::BlendMode;

    CHECK_EQ(App::Omikron::blend_mode(0U), BlendMode::k_opaque);
    CHECK_EQ(App::Omikron::blend_mode(1U << 2), BlendMode::k_opaque);  // Vertex-lit alone.
    CHECK_EQ(App::Omikron::blend_mode(1U << 11), BlendMode::k_alpha_test);
    CHECK_EQ(App::Omikron::blend_mode(1U << 12), BlendMode::k_alpha_blend);
    CHECK_EQ(App::Omikron::blend_mode((1U << 12) | (1U << 13)), BlendMode::k_additive);
    CHECK_EQ(App::Omikron::blend_mode((1U << 12) | (1U << 14)), BlendMode::k_subtractive);
    // Subtractive wins when both modifiers are set.
    CHECK_EQ(
        App::Omikron::blend_mode((1U << 12) | (1U << 13) | (1U << 14)), BlendMode::k_subtractive);
    // The modifiers are inert without alpha blending, as in the reference importer.
    CHECK_EQ(App::Omikron::blend_mode(1U << 13), BlendMode::k_opaque);
    CHECK_EQ(App::Omikron::blend_mode(1U << 14), BlendMode::k_opaque);
    // Blending takes priority over alpha testing when both are set.
    CHECK_EQ(App::Omikron::blend_mode((1U << 11) | (1U << 12)), BlendMode::k_alpha_blend);
    // Unrelated flags do not change the outcome.
    CHECK_EQ(App::Omikron::blend_mode((1U << 12) | (1U << 20) | (1U << 26) | (1U << 27)),
        BlendMode::k_alpha_blend);
    // Runtime's U-scroll flag is orthogonal to blend selection.
    CHECK_EQ(App::Omikron::blend_mode(1U << 24), BlendMode::k_opaque);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

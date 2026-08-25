#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"

namespace {

void write_i16(std::vector<std::byte>& data, const std::size_t offset, const std::int16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u16(std::vector<std::byte>& data, const std::size_t offset, const std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_i32(std::vector<std::byte>& data, const std::size_t offset, const std::int32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u32(std::vector<std::byte>& data, const std::size_t offset, const std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

App::Omikron::IamAreaRecord make_area() {
  constexpr std::size_t k_placement_offset{App::Omikron::IamAreaRecord::k_header_size};
  constexpr std::size_t k_definition_offset{k_placement_offset + 0x14U};
  constexpr std::uint16_t k_state_bit_index{468};
  std::vector<std::byte> data(k_definition_offset + 0x114U, std::byte{});
  write_u32(
      data, App::Omikron::IamAreaRecord::k_offset_script, static_cast<std::uint32_t>(data.size()));
  write_u32(data, App::Omikron::IamAreaRecord::k_offset_table_offsets, k_placement_offset);
  write_u16(data, App::Omikron::IamAreaRecord::k_offset_table_counts, 1);
  write_i16(data, k_placement_offset + 0x00U, -1);
  write_i16(data, k_placement_offset + 0x02U, 310);
  write_i32(data, k_placement_offset + 0x04U, -2588);
  write_i32(data, k_placement_offset + 0x08U, -271);
  write_i32(data, k_placement_offset + 0x0CU, -816);
  write_i16(data, k_placement_offset + 0x10U, 4084);
  write_u16(data, k_placement_offset + 0x12U, k_state_bit_index);

  write_u32(
      data, App::Omikron::IamAreaRecord::k_offset_table_offsets + (4U * 4U), k_definition_offset);
  write_u16(data, App::Omikron::IamAreaRecord::k_offset_table_counts + (4U * 2U), 1);
  constexpr std::string_view k_name{"KAY'L 669"};
  std::memcpy(data.data() + k_definition_offset + 0x08U, k_name.data(), k_name.size());
  constexpr std::string_view k_model{"HO1_FNM"};
  std::memcpy(data.data() + k_definition_offset + 0x90U, k_model.data(), k_model.size());
  write_u16(data, k_definition_offset + 0x110U, 310);

  auto area{App::Omikron::IamAreaRecord::load(data)};
  REQUIRE(area.has_value());
  return std::move(area).value();
}

App::Omikron::IamSceneRecord make_scene() {
  constexpr std::size_t k_placement_offset{App::Omikron::IamSceneRecord::k_header_size};
  constexpr std::size_t k_definition_offset{k_placement_offset + 0x14U};
  constexpr std::size_t k_record_size{k_definition_offset + 0x114U};
  std::vector<std::byte> data(k_record_size, std::byte{});
  write_u32(data,
      App::Omikron::IamSceneRecord::k_offset_table_offsets,
      static_cast<std::uint32_t>(k_placement_offset));
  write_u16(data, App::Omikron::IamSceneRecord::k_offset_table_counts, 1);
  for (const std::size_t table_index : {1U, 2U, 3U, 4U, 6U, 7U}) {
    write_u32(data,
        App::Omikron::IamSceneRecord::k_offset_table_offsets + (table_index * 4U),
        static_cast<std::uint32_t>(table_index <= 4U ? k_definition_offset : k_record_size));
  }
  write_u16(data, App::Omikron::IamSceneRecord::k_offset_table_counts + (4U * 2U), 1);

  write_i16(data, k_placement_offset + 0x00U, -1);
  write_i16(data, k_placement_offset + 0x02U, 57);
  write_i32(data, k_placement_offset + 0x04U, 49457);
  write_i32(data, k_placement_offset + 0x08U, -511);
  write_i32(data, k_placement_offset + 0x0CU, 19386);
  write_i16(data, k_placement_offset + 0x10U, 4073);

  constexpr std::string_view k_name{"LOCAL CHARACTER"};
  constexpr std::string_view k_model{"DE1_FN"};
  std::memcpy(data.data() + k_definition_offset + 0x08U, k_name.data(), k_name.size());
  std::memcpy(data.data() + k_definition_offset + 0x90U, k_model.data(), k_model.size());
  write_i16(data, k_definition_offset + 0x110U, 57);

  auto scene{App::Omikron::IamSceneRecord::load(data)};
  REQUIRE(scene.has_value());
  return std::move(scene).value();
}

std::shared_ptr<const App::Character::ModelResource> fake_resource(const std::string_view name) {
  auto resource{std::make_shared<App::Character::ModelResource>()};
  resource->name = name;
  resource->groups.push_back(App::Omikron::MaterialGroup{});
  return resource;
}

std::shared_ptr<const App::Character::ModelResource> fake_morph_resource(
    const std::string_view name) {
  auto resource{std::make_shared<App::Character::ModelResource>()};
  resource->name = name;
  auto& model{resource->model};
  model.materials.emplace_back();
  App::Omikron::MeshDescriptor mesh;
  mesh.name = "face";
  mesh.script_id = 30U;
  mesh.flags = static_cast<std::uint32_t>(App::Omikron::MeshFlags::k_face_morph);
  mesh.vertex_count = 3U;
  model.meshes.push_back(mesh);
  App::Omikron::Triangle triangle;
  triangle.material_id = 0;
  triangle.vertices.at(0).index = 0U;
  triangle.vertices.at(1).index = 1U;
  triangle.vertices.at(2).index = 2U;
  App::Omikron::MeshPolygons polygons;
  polygons.triangles.push_back(triangle);
  model.polygons.push_back(std::move(polygons));
  model.vertices = {
      App::Omikron::RawVertex{.position = {0.0F, 0.0F, 0.0F}, .normal = {0.0F, 1.0F, 0.0F}},
      App::Omikron::RawVertex{.position = {1.0F, 0.0F, 0.0F}, .normal = {0.0F, 1.0F, 0.0F}},
      App::Omikron::RawVertex{.position = {0.0F, 1.0F, 0.0F}, .normal = {0.0F, 1.0F, 0.0F}}};
  model.root_mesh_index = 0;
  model.hierarchy_parent_index = {-1};
  model.hierarchy_first_child_index = {-1};
  model.hierarchy_next_sibling_index = {-1};
  model.hierarchy_reachable = {1U};
  model.skin_parent_index = {-1};
  model.runtime_objects.emplace_back();
  REQUIRE(App::Omikron::Model3DO::resolve_runtime_transforms(model).has_value());
  auto groups{App::Omikron::Model3DO::build_static_geometry(model)};
  REQUIRE(groups.has_value());
  resource->groups = std::move(groups).value();
  return resource;
}

}  // namespace

TEST_SUITE("Core::Character::Runtime") {
  TEST_CASE("AREA 118 character materialization uses shared transform helpers and model data") {
    const App::Omikron::IamAreaRecord area{make_area()};
    std::string requested_model;
    App::Character::Runtime runtime{
        [&requested_model](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          requested_model = name;
          return fake_resource(name);
        }};

    REQUIRE(runtime
            .activate(118,
                area,
                App::Script::AreaCharacterActivationRequest{
                    .character_id = 310, .apply_area_transform = true})
            .has_value());
    const App::Character::RuntimeCharacter* character{runtime.find(310)};
    REQUIRE(character != nullptr);
    CHECK_EQ(requested_model, "HO1_FNM");
    CHECK_EQ(character->definition_name, "KAY'L 669");
    CHECK_EQ(character->model_resource_name, "HO1_FNM");
    CHECK(character->active);
    CHECK(character->area_present);
    CHECK(character->loaded());
    CHECK(character->renderable());
    CHECK_EQ(character->serialized_area_position.at(0), -2588);
    CHECK_EQ(character->serialized_orientation_units, 4084);
    CHECK_EQ(character->transform.translation.x,
        static_cast<float>(App::Runtime::area_position_to_inches(-2588)));
    CHECK_EQ(character->transform.translation.y,
        static_cast<float>(App::Runtime::area_position_to_inches(-271)));
    CHECK_EQ(character->transform.translation.z,
        static_cast<float>(App::Runtime::area_position_to_inches(-816)));
    CHECK_EQ(character->runtime_orientation_degrees, App::Runtime::area_angle_to_degrees(4084));
  }

  TEST_CASE("Repeated activation reuses character and model while false preserves transform") {
    const App::Omikron::IamAreaRecord area{make_area()};
    std::size_t loads{0};
    App::Character::Runtime runtime{
        [&loads](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          ++loads;
          return fake_resource(name);
        }};
    const App::Script::AreaCharacterActivationRequest placed{
        .character_id = 310, .apply_area_transform = true};
    REQUIRE(runtime.activate(118, area, placed).has_value());
    App::Character::RuntimeCharacter* character{runtime.find(310)};
    REQUIRE(character != nullptr);
    character->transform.translation = App::Runtime::Vec3{11.0F, 22.0F, 33.0F};
    character->transform.matrix = App::Runtime::rotation_x(0.5F);
    const App::Runtime::Matrix3 preserved_matrix{character->transform.matrix};

    REQUIRE(runtime
            .activate(118,
                area,
                App::Script::AreaCharacterActivationRequest{
                    .character_id = 310, .apply_area_transform = false})
            .has_value());
    CHECK_EQ(runtime.characters().size(), 1U);
    CHECK_EQ(runtime.model_resource_count(), 1U);
    CHECK_EQ(loads, 1U);
    character = runtime.find(310);
    REQUIRE(character != nullptr);
    CHECK_EQ(character->transform.translation.x, 11.0F);
    CHECK_EQ(character->transform.translation.y, 22.0F);
    CHECK_EQ(character->transform.translation.z, 33.0F);
    CHECK_EQ(character->transform.matrix.values, preserved_matrix.values);
  }

  TEST_CASE("Current-body ensure, presentation, and transfer retain live state without reloading") {
    const App::Omikron::IamAreaRecord area{make_area()};
    std::size_t source_loads{0};
    std::size_t target_loads{0};
    App::Character::Runtime source{
        [&source_loads](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          ++source_loads;
          return fake_resource(name);
        }};
    App::Character::Runtime target{
        [&target_loads](const std::string_view)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          ++target_loads;
          return std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string>{
              std::unexpect, "target must adopt the shared source resource"};
        }};

    REQUIRE(source.ensure_area_character(118, area, 310).has_value());
    App::Character::RuntimeCharacter* character{source.find(310)};
    REQUIRE(character != nullptr);
    const std::shared_ptr<const App::Character::ModelResource> source_resource{character->model_resource};
    character->transform.translation = App::Runtime::Vec3{11.0F, 22.0F, 33.0F};
    character->pose_revision = 17U;
    REQUIRE(source.set_presentation_enabled(310, false).has_value());
    CHECK_FALSE(character->renderable());

    // Current-character reselection only reactivates the existing body; it
    // does not restore its authored transform or reset its mutable pose.
    REQUIRE(source.ensure_area_character(118, area, 310).has_value());
    character = source.find(310);
    REQUIRE(character != nullptr);
    CHECK_EQ(character->transform.translation.x, 11.0F);
    CHECK_EQ(character->pose_revision, 17U);
    CHECK_FALSE(character->presentation_enabled);

    REQUIRE(source.transfer_character_to(target, 310).has_value());
    CHECK(source.find(310) == nullptr);
    character = target.find(310);
    REQUIRE(character != nullptr);
    CHECK_EQ(character->instance_id, 0U);
    CHECK(character->model_resource == source_resource);
    CHECK_EQ(character->transform.translation.x, 11.0F);
    CHECK_EQ(character->transform.translation.y, 22.0F);
    CHECK_EQ(character->transform.translation.z, 33.0F);
    CHECK_EQ(character->pose_revision, 17U);
    CHECK_FALSE(character->presentation_enabled);
    CHECK_FALSE(character->renderable());
    CHECK_EQ(source_loads, 1U);
    CHECK_EQ(target_loads, 0U);
    CHECK_EQ(target.model_resource_count(), 1U);

    REQUIRE(target.set_presentation_enabled(310, true).has_value());
    character = target.find(310);
    REQUIRE(character != nullptr);
    CHECK(character->renderable());
  }

  TEST_CASE("SCENE-only characters use SCENE definitions and cleanly dematerialize") {
    const App::Omikron::IamSceneRecord scene{make_scene()};
    std::size_t loads{0};
    App::Character::Runtime runtime{
        [&loads](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          ++loads;
          return fake_resource(name);
        }};

    REQUIRE(runtime.preload_scene_characters(222, 55, scene).has_value());
    const App::Character::RuntimeCharacter* character{runtime.find(57)};
    REQUIRE(character != nullptr);
    REQUIRE(character->scene_id.has_value());
    CHECK_EQ(character->scene_id.value(), 55);
    CHECK_EQ(character->area_id, 222);
    CHECK_EQ(character->model_resource_name, "DE1_FN");
    CHECK_EQ(loads, 1U);
    CHECK(character->active);
    CHECK(character->area_present);
    CHECK_FALSE(character->presentation_enabled);
    CHECK_FALSE(character->renderable());
    REQUIRE(runtime.set_presentation_enabled(57, true).has_value());
    CHECK_FALSE(character->presentation_enabled);
    CHECK_FALSE(character->renderable());
    CHECK_EQ(character->transform.translation.x,
        static_cast<float>(App::Runtime::area_position_to_inches(49457)));

    // Preload establishes a logical resident body so a bound SCX script may
    // start before visibility. Compact 0x4E changes presentation separately.
    REQUIRE(runtime.ensure_scene_character(222, 55, scene, 57).has_value());
    character = runtime.find(57);
    REQUIRE(character != nullptr);
    CHECK(character->active);
    CHECK(character->area_present);
    CHECK(character->renderable());

    const App::Omikron::IamAreaAddressRecord address{.serialized_position = {43922, 2592, 19656},
        .orientation_units = 0,
        .address_id = 654};
    REQUIRE(runtime.place_character_at_address(57, address).has_value());
    character = runtime.find(57);
    REQUIRE(character != nullptr);
    CHECK_EQ(character->serialized_area_position.at(0), 43922);
    CHECK_EQ(character->serialized_orientation_units, 0);
    CHECK_EQ(character->transform.translation.z,
        static_cast<float>(App::Runtime::area_position_to_inches(19656)));

    runtime.dematerialize_scene_characters(222, 55);
    character = runtime.find(57);
    REQUIRE(character != nullptr);
    CHECK_FALSE(character->active);
    CHECK_FALSE(character->area_present);
    CHECK_FALSE(character->scene_id.has_value());
  }

  TEST_CASE("Special current-character ID remains distinct from table-0 lookup") {
    const App::Omikron::IamAreaRecord area{make_area()};
    App::Character::Runtime runtime{
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          return fake_resource(name);
        }};
    const auto result{runtime.activate(118,
        area,
        App::Script::AreaCharacterActivationRequest{
            .character_id = -1, .apply_area_transform = true})};
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("current-character") != std::string::npos);
    CHECK(runtime.characters().empty());
  }

  TEST_CASE("dialogue overlay keeps shared vertices immutable and reveals latest base pose") {
    const App::Omikron::IamAreaRecord area{make_area()};
    App::Character::Runtime runtime{
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          return fake_morph_resource(name);
        }};
    REQUIRE(runtime
            .activate(118,
                area,
                App::Script::AreaCharacterActivationRequest{
                    .character_id = 310, .apply_area_transform = true})
            .has_value());
    App::Character::RuntimeCharacter* character{runtime.find(310)};
    REQUIRE(character != nullptr);
    REQUIRE(character->model_resource != nullptr);
    const float immutable_position{character->model_resource->model.vertices.at(0).position.x};

    character->runtime_objects.at(0).local_offset.x = 7.0F;
    REQUIRE(App::Omikron::Model3DO::resolve_runtime_transforms(
                character->model_resource->model, std::span{character->runtime_objects})
                .has_value());
    App::Character::DialogPerformanceOverlay overlay;
    overlay.object_rotations.resize(1U);
    overlay.object_rotations.at(0) = App::Runtime::Quaternion{};
    overlay.root_object_index = 0U;
    overlay.root_translation_delta.x = 2.0F;
    overlay.face_mesh_index = 0U;
    overlay.face_vertices = {
        {.position = {10.0F, 0.0F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}},
        {.position = {11.0F, 0.0F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}},
        {.position = {12.0F, 0.0F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}}};
    REQUIRE(runtime.apply_dialog_performance(310, std::move(overlay)).has_value());
    CHECK_EQ(character->model_resource->model.vertices.at(0).position.x, immutable_position);
    REQUIRE_FALSE(character->posed_groups.empty());
    CHECK_EQ(character->posed_groups.at(0).vertices.at(0).position.at(0), 19.0F);

    character->runtime_objects.at(0).local_offset.x = 20.0F;
    REQUIRE(App::Omikron::Model3DO::resolve_runtime_transforms(
                character->model_resource->model, std::span{character->runtime_objects})
                .has_value());
    runtime.clear_dialog_performance(310);
    CHECK_FALSE(character->dialog_performance.has_value());
    CHECK_EQ(character->posed_groups.at(0).vertices.at(0).position.at(0), 20.0F);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)

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
#include "OmikronTestBuffer.hpp"

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
  write_u32(data, App::Omikron::IamAreaRecord::k_offset_primary_event, 0U);
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

std::shared_ptr<const App::Omikron::CtlControlSet> fake_ctl_resource() {
  Buffer bytes;
  bytes.u32(0x30374543U).u32(0x101U).u32(0).u32(1).zeros(0x48U);
  bytes.u32(7).u32(1).u32(1).u32(0).u32(0).chars("Default", 12);
  bytes.u32(71).u32(0).u32(0x8022U).zeros(0x58U - 12U);
  auto parsed{App::Omikron::CtlControlSet::load(bytes.data())};
  REQUIRE(parsed.has_value());
  return std::make_shared<const App::Omikron::CtlControlSet>(std::move(parsed).value());
}

}  // namespace

TEST_SUITE("Core::Character::Runtime") {
  TEST_CASE("visual object world transform composes instance state with the logical actor") {
    App::Character::RuntimeCharacter character;
    character.transform.translation = {.x = 100.0F, .y = 200.0F, .z = 300.0F};
    character.set_principal_orientation({.y = 90.0F});
    character.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{
        .world_translation = {.x = 10.0F, .y = 20.0F, .z = 30.0F}});

    const auto model_transform{character.object_model_transform(0U)};
    REQUIRE(model_transform.has_value());
    CHECK_EQ(model_transform->translation.x, 10.0F);
    CHECK_EQ(model_transform->translation.y, 20.0F);
    CHECK_EQ(model_transform->translation.z, 30.0F);

    const auto world_transform{character.object_world_transform(0U)};
    REQUIRE(world_transform.has_value());
    const App::Runtime::Vec3 expected{App::Runtime::transform_point(
        model_transform->translation, character.presentation_transform())};
    CHECK_EQ(world_transform->translation.x, doctest::Approx(expected.x));
    CHECK_EQ(world_transform->translation.y, doctest::Approx(expected.y));
    CHECK_EQ(world_transform->translation.z, doctest::Approx(expected.z));
    CHECK_FALSE(character.object_world_transform(1U).has_value());
  }

  TEST_CASE("actor object uses first mesh with the greatest polygon total") {
    App::Omikron::Model3DOData model;
    CHECK_FALSE(App::Character::actor_object_index(model).has_value());

    model.meshes = {App::Omikron::MeshDescriptor{.triangle_count = 6, .rectangle_count = 4},
        App::Omikron::MeshDescriptor{.triangle_count = 8, .rectangle_count = 2},
        App::Omikron::MeshDescriptor{.triangle_count = 20, .rectangle_count = 1}};
    CHECK_EQ(App::Character::actor_object_index(model), std::optional<std::size_t>{2U});

    model.meshes.at(2).triangle_count = 10;
    model.meshes.at(2).rectangle_count = 0;
    CHECK_EQ(App::Character::actor_object_index(model), std::optional<std::size_t>{0U});

    model.root_mesh_index = 0;
    model.meshes.at(1).parent_id = 1;
    model.meshes.at(1).triangle_count = 11;
    CHECK_EQ(App::Character::actor_object_index(model), std::optional<std::size_t>{1U});
    CHECK_NE(model.root_mesh_index, 1);

    model.meshes.at(2).triangle_count = 11;
    CHECK_EQ(App::Character::actor_object_index(model), std::optional<std::size_t>{1U});
  }
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
    CHECK(character->physical_motion.initialized);
    CHECK_EQ(
        character->physical_motion.candidate_translation.x, character->transform.translation.x);
    CHECK_EQ(character->physical_motion.accepted_translation.x, character->transform.translation.x);
    CHECK_EQ(character->runtime_orientation_degrees, App::Runtime::area_angle_to_degrees(4084));
    CHECK_EQ(character->principal_orientation_degrees.x, doctest::Approx(0.0F));
    CHECK_EQ(character->principal_orientation_degrees.y,
        doctest::Approx(static_cast<float>(character->runtime_orientation_degrees)));
    CHECK_EQ(character->principal_orientation_degrees.z, doctest::Approx(0.0F));
    CHECK_EQ(character->transform.matrix.values, character->principal_orientation().values);
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
    const App::Character::BodyIdentity body_identity{character->body_identity};
    const std::shared_ptr<const App::Character::ModelResource> source_resource{
        character->model_resource};
    character->transform.translation = App::Runtime::Vec3{11.0F, 22.0F, 33.0F};
    character->physical_motion = App::Character::PhysicalMotionState{
        .candidate_translation = {.x = 44.0F, .y = 55.0F, .z = 66.0F},
        .accepted_translation = {.x = 11.0F, .y = 22.0F, .z = 33.0F},
        .accumulator_seconds = 0.01F,
        .initialized = true};
    character->pose_revision = 17U;
    REQUIRE(source.set_body_presentation_enabled(body_identity, false).has_value());
    CHECK_FALSE(character->renderable());

    // Current-character reselection only reactivates the existing body; it
    // does not restore its authored transform or reset its mutable pose.
    REQUIRE(source.ensure_area_character(118, area, 310).has_value());
    character = source.find(310);
    REQUIRE(character != nullptr);
    CHECK_EQ(character->transform.translation.x, 11.0F);
    CHECK_EQ(character->pose_revision, 17U);
    CHECK_FALSE(character->presentation_enabled);

    REQUIRE(source.transfer_body_to(target, body_identity).has_value());
    CHECK(source.find(310) == nullptr);
    character = target.find(310);
    REQUIRE(character != nullptr);
    CHECK_EQ(character->instance_id, 0U);
    CHECK(character->model_resource == source_resource);
    CHECK_EQ(character->transform.translation.x, 11.0F);
    CHECK_EQ(character->transform.translation.y, 22.0F);
    CHECK_EQ(character->transform.translation.z, 33.0F);
    CHECK(character->physical_motion.initialized);
    CHECK_EQ(character->physical_motion.candidate_translation.x, 44.0F);
    CHECK_EQ(character->physical_motion.accepted_translation.y, 22.0F);
    CHECK_EQ(character->physical_motion.accumulator_seconds, doctest::Approx(0.01F));
    CHECK_EQ(character->pose_revision, 17U);
    CHECK_FALSE(character->presentation_enabled);
    CHECK_FALSE(character->renderable());
    CHECK_EQ(source_loads, 1U);
    CHECK_EQ(target_loads, 0U);
    CHECK_EQ(target.model_resource_count(), 1U);

    REQUIRE(target.set_body_presentation_enabled(body_identity, true).has_value());
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
    CHECK(character->presentation_enabled);
    CHECK(character->renderable());
    CHECK_EQ(character->transform.translation.x,
        static_cast<float>(App::Runtime::area_position_to_inches(49457)));

    // SCENE materialization creates the resident body normally. Presentation
    // is changed only by an explicit authored visibility operation; attaching
    // a SCENE must not manufacture a hidden state for every new character.
    REQUIRE(runtime.ensure_scene_character(222, 55, scene, 57).has_value());
    character = runtime.find(57);
    REQUIRE(character != nullptr);
    CHECK(character->active);
    CHECK(character->area_present);
    CHECK(character->renderable());

    const App::Omikron::IamAreaAddressRecord address{
        .serialized_position = {43922, 2592, 19656}, .orientation_units = 0, .address_id = 654};
    REQUIRE(runtime.place_body_at_address(character->body_identity, address).has_value());
    character = runtime.find(57);
    REQUIRE(character != nullptr);
    CHECK_EQ(character->serialized_area_position.at(0), 43922);
    CHECK_EQ(character->serialized_orientation_units, 0);
    CHECK_EQ(character->transform.translation.z,
        static_cast<float>(App::Runtime::area_position_to_inches(19656)));
    CHECK(character->physical_motion.initialized);
    CHECK_EQ(
        character->physical_motion.candidate_translation.z, character->transform.translation.z);
    CHECK_EQ(character->physical_motion.accepted_translation.z, character->transform.translation.z);

    runtime.dematerialize_scene_characters(222, 55);
    character = runtime.find(57);
    REQUIRE(character != nullptr);
    CHECK_FALSE(character->active);
    CHECK_FALSE(character->area_present);
    CHECK_FALSE(character->scene_id.has_value());
  }

  TEST_CASE("BodyIdentity operations never rediscover a body through canonical ID") {
    const App::Omikron::IamAreaRecord area{make_area()};
    App::Character::Runtime runtime{
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          return fake_resource(name);
        }};
    runtime.set_ctl_bank_loader([](const std::string_view) {
      return std::expected<std::shared_ptr<const App::Omikron::CtlControlSet>, std::string>{
          fake_ctl_resource()};
    });
    const auto materialized{runtime.ensure_area_character(118, area, 310)};
    REQUIRE(materialized.has_value());
    App::Character::RuntimeCharacter* body{runtime.find_body(materialized->body_identity)};
    REQUIRE(body != nullptr);
    body->character_id = 999;

    const auto controller{
        runtime.ensure_adventure_controller(materialized->body_identity, "TESTCTL")};
    INFO(controller.error_or(""));
    REQUIRE(controller.has_value());
    CHECK(body->ctl_controller.has_value());
    REQUIRE(runtime.deactivate_body(materialized->body_identity).has_value());
    CHECK_FALSE(body->active);
    CHECK_FALSE(body->area_present);
  }

  TEST_CASE("SCENE preload returns and preserves the exact transferred BodyIdentity") {
    const App::Omikron::IamSceneRecord scene{make_scene()};
    App::Character::Runtime runtime{
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          return fake_resource(name);
        }};
    const auto initial{runtime.ensure_scene_character(118, 1, scene, 57)};
    REQUIRE(initial.has_value());
    App::Character::RuntimeCharacter* body{runtime.find_body(initial->body_identity)};
    REQUIRE(body != nullptr);
    body->transform.translation.x = 777.0F;
    body->pose_revision = 41U;
    const auto resource{body->model_resource};

    const auto preloaded{runtime.preload_scene_characters(222, 55, scene, initial->body_identity)};
    REQUIRE(preloaded.has_value());
    REQUIRE_EQ(preloaded->size(), 1U);
    CHECK_EQ(preloaded->front().body_identity, initial->body_identity);
    CHECK_FALSE(preloaded->front().newly_created);
    body = runtime.find_body(initial->body_identity);
    REQUIRE(body != nullptr);
    CHECK_EQ(body->transform.translation.x, 777.0F);
    CHECK_EQ(body->pose_revision, 41U);
    CHECK(body->model_resource == resource);

    runtime.dematerialize_scene_characters(222, 55, initial->body_identity);
    CHECK(body->active);
    CHECK(body->area_present);
    CHECK_FALSE(body->scene_id.has_value());
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
    overlay.face_vertices = {{.position = {10.0F, 0.0F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}},
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

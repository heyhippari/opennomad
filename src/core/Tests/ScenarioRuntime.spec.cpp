#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Omikron/SFX.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "Core/Script/ScriptRuntime.hpp"
#include "Core/Sprite/SpriteInstance.hpp"
#include "OmikronTestBuffer.hpp"

namespace {

/// One inert parsed command; content is not dispatched by these tests.
App::Omikron::ScxScriptCommand make_command() {
  return App::Omikron::ScxScriptCommand{.opcode = 0x04000029U,
      .value_count = 0,
      .first_value_index = 0,
      .next_linked_command_index = std::nullopt,
      .execution_limit = 0xFFFFFFFFU,
      .initial_execution_count = 0,
      .file_offset = 0};
}

/// One parsed SCX source script with the given root command count.
App::Omikron::ScxScript make_script(
    const std::string_view name, const std::size_t root_command_count) {
  App::Omikron::ScxScript script;
  script.name = std::string{name};
  script.root_command_count = static_cast<std::uint32_t>(root_command_count);
  script.linked_command_count = 0;
  if (root_command_count > 0) {
    script.root_commands.push_back(make_command());
  }
  return script;
}

void write_i16(std::vector<std::byte>& data, const std::size_t offset, const std::int16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u16(std::vector<std::byte>& data, const std::size_t offset, const std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u32(std::vector<std::byte>& data, const std::size_t offset, const std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

App::Omikron::IamAreaRecord make_character_area() {
  constexpr std::size_t k_placement_offset{App::Omikron::IamAreaRecord::k_header_size};
  constexpr std::size_t k_definition_offset{k_placement_offset + 0x14U};
  std::vector<std::byte> data(k_definition_offset + 0x114U, std::byte{});
  write_u32(data, App::Omikron::IamAreaRecord::k_offset_primary_event, 0U);
  write_u32(data, App::Omikron::IamAreaRecord::k_offset_table_offsets, k_placement_offset);
  write_u16(data, App::Omikron::IamAreaRecord::k_offset_table_counts, 1);
  write_i16(data, k_placement_offset, -1);
  write_i16(data, k_placement_offset + 0x02U, 310);
  write_u16(data, k_placement_offset + 0x12U, 468);
  write_u32(
      data, App::Omikron::IamAreaRecord::k_offset_table_offsets + (4U * 4U), k_definition_offset);
  write_u16(data, App::Omikron::IamAreaRecord::k_offset_table_counts + (4U * 2U), 1);
  constexpr std::string_view k_name{"KAY'L 669"};
  std::memcpy(data.data() + k_definition_offset + 0x08U, k_name.data(), k_name.size());
  constexpr std::string_view k_model{"HO1_FNM"};
  std::memcpy(data.data() + k_definition_offset + 0x90U, k_model.data(), k_model.size());
  write_u16(data, k_definition_offset + 0x110U, 310);
  return App::Omikron::IamAreaRecord::load(data).value();
}

std::shared_ptr<const App::Character::ModelResource> make_body_model_resource() {
  auto resource{std::make_shared<App::Character::ModelResource>()};
  resource->name = "HO1_FNM";
  resource->model.materials.push_back(App::Omikron::Material{});
  resource->model.meshes.push_back(App::Omikron::MeshDescriptor{.mesh_id = 100,
      .script_id = 2,
      .name = "RootBody",
      .parent_id = -1,
      .first_child_id = 200,
      .next_sibling_id = -1});
  resource->model.meshes.push_back(App::Omikron::MeshDescriptor{.mesh_id = 200,
      .script_id = 3,
      .name = "Child",
      .parent_id = 100,
      .first_child_id = -1,
      .next_sibling_id = -1,
      .bone_position = {.x = 2.0F, .y = 0.0F, .z = 0.0F}});
  resource->model.polygons.resize(2);
  resource->model.root_mesh_index = 0;
  resource->actor_object_index = 0U;
  resource->model.hierarchy_parent_index = {-1, 0};
  resource->model.hierarchy_first_child_index = {1, -1};
  resource->model.hierarchy_next_sibling_index = {-1, -1};
  resource->model.hierarchy_reachable = {1, 1};
  resource->model.skin_parent_index = {-1, 0};
  resource->model.runtime_objects = {App::Omikron::Model3DOData::RuntimeObjectState{},
      App::Omikron::Model3DOData::RuntimeObjectState{.local_offset = {.x = 2.0F}}};
  resource->groups.push_back(App::Omikron::MaterialGroup{});
  return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
}

std::shared_ptr<const App::Character::ModelResource> make_split_actor_model_resource() {
  auto resource{std::make_shared<App::Character::ModelResource>()};
  resource->name = "SPLIT";
  resource->model.materials.push_back(App::Omikron::Material{});
  resource->model.meshes.push_back(App::Omikron::MeshDescriptor{.mesh_id = 100,
      .script_id = 2,
      .name = "RootBody",
      .parent_id = -1,
      .first_child_id = 200,
      .next_sibling_id = -1,
      .triangle_count = 10});
  resource->model.meshes.push_back(App::Omikron::MeshDescriptor{.mesh_id = 200,
      .script_id = 3,
      .name = "Child",
      .parent_id = 100,
      .first_child_id = -1,
      .next_sibling_id = -1,
      .triangle_count = 60,
      .rectangle_count = 40,
      .bone_position = {.x = 2.0F}});
  resource->model.polygons.resize(2);
  resource->model.root_mesh_index = 0;
  resource->actor_object_index = App::Character::actor_object_index(resource->model);
  resource->model.hierarchy_parent_index = {-1, 0};
  resource->model.hierarchy_first_child_index = {1, -1};
  resource->model.hierarchy_next_sibling_index = {-1, -1};
  resource->model.hierarchy_reachable = {1, 1};
  resource->model.skin_parent_index = {-1, 0};
  resource->model.runtime_objects = {App::Omikron::Model3DOData::RuntimeObjectState{},
      App::Omikron::Model3DOData::RuntimeObjectState{.local_offset = {.x = 2.0F}}};
  resource->groups.push_back(App::Omikron::MaterialGroup{});
  return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
}

std::shared_ptr<const App::Character::ModelResource> make_cin_sfx_model_resource() {
  auto resource{std::make_shared<App::Character::ModelResource>()};
  resource->name = "CIN_SFX";
  resource->model.materials.push_back(App::Omikron::Material{});
  resource->model.meshes = {App::Omikron::MeshDescriptor{.mesh_id = 100,
                                .script_id = 2,
                                .name = "SelectedRoot",
                                .parent_id = -1,
                                .first_child_id = 300,
                                .next_sibling_id = 200},
      App::Omikron::MeshDescriptor{.mesh_id = 200,
          .script_id = 9,
          .name = "OutsideSelectedHierarchy",
          .parent_id = -1,
          .first_child_id = -1,
          .next_sibling_id = -1},
      App::Omikron::MeshDescriptor{.mesh_id = 300,
          .script_id = 3,
          .name = "SelectedChild",
          .parent_id = 100,
          .first_child_id = -1,
          .next_sibling_id = -1,
          .bone_position = {.x = 2.0F}}};
  resource->model.polygons.resize(3U);
  resource->model.root_mesh_index = 0;
  resource->actor_object_index = 0U;
  resource->model.hierarchy_parent_index = {-1, -1, 0};
  resource->model.hierarchy_first_child_index = {2, -1, -1};
  resource->model.hierarchy_next_sibling_index = {1, -1, -1};
  resource->model.hierarchy_reachable = {1U, 1U, 1U};
  resource->model.skin_parent_index = {-1, -1, 0};
  resource->model.runtime_objects = {App::Omikron::Model3DOData::RuntimeObjectState{},
      App::Omikron::Model3DOData::RuntimeObjectState{.local_offset = {.x = 50.0F}},
      App::Omikron::Model3DOData::RuntimeObjectState{.local_offset = {.x = 2.0F}}};
  resource->groups.push_back(App::Omikron::MaterialGroup{});
  return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
}

struct BodyResourcesFixture {
  App::Omikron::ScxData scx;
  std::vector<std::byte> bytes;
};

BodyResourcesFixture make_body_resources(const App::Runtime::Vec3 reference = {},
    const App::Runtime::Vec3 root_motion = {.x = 10.0F},
    const bool child_translation = false,
    const std::uint32_t sample_count = 4U) {
  Buffer path;
  path.u32(1).chars("UBas.p1", 20).u32(2).u32(3);
  for (std::uint32_t key{0}; key < 3U; ++key) {
    path.u32(key)
        .f32(-478.3933410644531F)
        .f32(-43.900245666503906F)
        .f32(27.611772537231445F)
        .f32(1.0F)
        .f32(0.0F)
        .f32(0.0F)
        .f32(0.0F);
  }

  constexpr std::uint32_t descriptor_end{8U + (2U * 0x28U)};
  const std::uint32_t root_translation_size{sample_count * 12U};
  const std::uint32_t child_translation_offset{descriptor_end + root_translation_size};
  const std::uint32_t root_rotation_offset{
      child_translation_offset + (child_translation ? root_translation_size : 0U)};
  const std::uint32_t child_rotation_offset{root_rotation_offset + (sample_count * 16U)};
  Buffer animation;
  animation.u32(sample_count - 1U).u32(2);
  animation.u32(2)
      .chars("RootBody", 20)
      .u32(sample_count)
      .u32(descriptor_end)
      .u32(sample_count)
      .u32(root_rotation_offset);
  // mesh_id is 200, but the animation must bind this channel by script_id 3.
  animation.u32(3)
      .chars("Child", 20)
      .u32(sample_count)
      .u32(child_translation ? child_translation_offset : 0U)
      .u32(sample_count)
      .u32(child_rotation_offset);
  // Translation sample zero is the reference position; each later sample is
  // an interval-local root-motion vector. Keep the vectors uniform so a
  // fractional interval is an unambiguous fraction of root_motion.
  animation.f32(reference.x).f32(reference.y).f32(reference.z);
  for (std::uint32_t frame{1}; frame < sample_count; ++frame) {
    animation.f32(root_motion.x).f32(root_motion.y).f32(root_motion.z);
  }
  if (child_translation) {
    animation.f32(reference.x).f32(reference.y).f32(reference.z);
    for (std::uint32_t frame{1}; frame < sample_count; ++frame) {
      animation.f32(root_motion.x).f32(root_motion.y).f32(root_motion.z);
    }
  }
  animation.f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
  animation.f32(0.0F).f32(0.0F).f32(0.0F).f32(1.0F);
  animation.f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
  animation.f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
  for (std::uint32_t frame{4U}; frame < sample_count; ++frame) {
    animation.f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
  }
  for (std::uint32_t frame{0}; frame < sample_count; ++frame) {
    animation.f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
  }

  BodyResourcesFixture fixture;
  fixture.bytes = path.data();
  fixture.bytes.insert(fixture.bytes.end(), animation.data().begin(), animation.data().end());
  fixture.scx.section0_records.push_back(App::Omikron::ScxSection0Record{.name = "Grid_pb.3dp"});
  fixture.scx.section0_resources.push_back(
      App::Omikron::ScxEmbeddedResource{.payload_offset = 0, .payload_size = path.data().size()});
  fixture.scx.animations.push_back(App::Omikron::ScxAnimationRecord{
      .name = "INTRO1.3DA", .serialized_field_1c = 0, .animation_id = 77});
  fixture.scx.animation_resources.push_back(App::Omikron::ScxEmbeddedResource{
      .payload_offset = path.data().size(), .payload_size = animation.data().size()});
  return fixture;
}

App::Omikron::Model3DOData make_movable_decor() {
  App::Omikron::Model3DOData decor;
  decor.materials.push_back(App::Omikron::Material{.width = 1U, .height = 1U});
  decor.meshes.push_back(App::Omikron::MeshDescriptor{.mesh_id = 1U,
      .name = "Movable",
      .parent_id = -1,
      .first_child_id = -1,
      .next_sibling_id = -1,
      .vertex_count = 3U,
      .triangle_count = 1U});
  decor.polygons.push_back(
      App::Omikron::MeshPolygons{.triangles = {App::Omikron::Triangle{
                                     .vertices = {App::Omikron::TriangleVertexRef{.index = 0U},
                                         App::Omikron::TriangleVertexRef{.index = 1U},
                                         App::Omikron::TriangleVertexRef{.index = 2U}},
                                     .material_id = 0}}});
  decor.vertices = {App::Omikron::RawVertex{.position = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                        .normal = {.z = 1.0F},
                        .color_bgra = {255U, 255U, 255U, 255U}},
      App::Omikron::RawVertex{.position = {.x = 1.0F, .y = 0.0F, .z = 0.0F},
          .normal = {.z = 1.0F},
          .color_bgra = {255U, 255U, 255U, 255U}},
      App::Omikron::RawVertex{.position = {.x = 0.0F, .y = 1.0F, .z = 0.0F},
          .normal = {.z = 1.0F},
          .color_bgra = {255U, 255U, 255U, 255U}}};
  decor.root_mesh_index = 0;
  decor.hierarchy_parent_index = {-1};
  decor.hierarchy_first_child_index = {-1};
  decor.hierarchy_next_sibling_index = {-1};
  decor.hierarchy_reachable = {1U};
  decor.skin_parent_index = {-1};
  decor.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{});
  return decor;
}

App::Omikron::Model3DOData make_camera_decor() {
  App::Omikron::Model3DOData decor;
  decor.cameras.push_back(App::Omikron::CameraRecord{.name = "CAM_A",
      .eye = {.x = 0.0F, .y = 10.0F, .z = 20.0F},
      .target = {.x = 30.0F, .y = 40.0F, .z = 50.0F},
      .roll_degrees = 0.0F,
      .horizontal_fov_degrees = 80.0F});
  decor.cameras.push_back(App::Omikron::CameraRecord{.name = "CAM_B",
      .eye = {.x = 100.0F, .y = 110.0F, .z = 120.0F},
      .target = {.x = 130.0F, .y = 140.0F, .z = 150.0F},
      .roll_degrees = 20.0F,
      .horizontal_fov_degrees = 60.0F});
  return decor;
}

void append_world_pose_path(BodyResourcesFixture& fixture) {
  Buffer path;
  path.u32(1).chars("WorldPose", 20).u32(1).u32(2);
  for (std::uint32_t parameter{0}; parameter < 2U; ++parameter) {
    path.u32(parameter)
        .f32(700.0F)
        .f32(-120.0F)
        .f32(325.0F)
        .f32(0.70710677F)
        .f32(0.0F)
        .f32(0.0F)
        .f32(0.70710677F);
  }
  const std::size_t payload_offset{fixture.bytes.size()};
  fixture.bytes.insert(fixture.bytes.end(), path.data().begin(), path.data().end());
  fixture.scx.section0_records.push_back(App::Omikron::ScxSection0Record{.name = "WorldPose.3dp"});
  fixture.scx.section0_resources.push_back(App::Omikron::ScxEmbeddedResource{
      .payload_offset = payload_offset, .payload_size = path.data().size()});
}

void append_rebase_path(BodyResourcesFixture& fixture) {
  Buffer path;
  path.u32(1).chars("Rebase", 20).u32(1).u32(2);
  path.u32(0).f32(10.0F).f32(20.0F).f32(30.0F).f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
  path.u32(1)
      .f32(12.0F)
      .f32(24.0F)
      .f32(36.0F)
      .f32(0.70710677F)
      .f32(0.0F)
      .f32(0.0F)
      .f32(0.70710677F);
  const std::size_t payload_offset{fixture.bytes.size()};
  fixture.bytes.insert(fixture.bytes.end(), path.data().begin(), path.data().end());
  fixture.scx.section0_records.push_back(App::Omikron::ScxSection0Record{.name = "Rebase.3dp"});
  fixture.scx.section0_resources.push_back(App::Omikron::ScxEmbeddedResource{
      .payload_offset = payload_offset, .payload_size = path.data().size()});
}

App::Omikron::Model3DOData make_parented_movable_decor() {
  App::Omikron::Model3DOData decor;
  decor.meshes.push_back(App::Omikron::MeshDescriptor{.mesh_id = 10U,
      .name = "Parent",
      .parent_id = -1,
      .first_child_id = 11,
      .next_sibling_id = -1});
  decor.meshes.push_back(App::Omikron::MeshDescriptor{.mesh_id = 11U,
      .name = "Movable",
      .parent_id = 10,
      .first_child_id = -1,
      .next_sibling_id = 12});
  decor.meshes.push_back(App::Omikron::MeshDescriptor{.mesh_id = 12U,
      .name = "Sibling",
      .parent_id = 10,
      .first_child_id = -1,
      .next_sibling_id = -1});
  decor.root_mesh_index = 0;
  decor.hierarchy_parent_index = {-1, 0, 0};
  decor.hierarchy_first_child_index = {1, -1, -1};
  decor.hierarchy_next_sibling_index = {-1, 2, -1};
  decor.hierarchy_reachable = {1U, 1U, 1U};
  decor.skin_parent_index = {-1, 0, 0};
  decor.runtime_objects = {
      App::Omikron::Model3DOData::RuntimeObjectState{.local_offset = {100.0F, 200.0F, 300.0F},
          .local_matrix = App::Runtime::rotation_z(1.57079632679F)},
      App::Omikron::Model3DOData::RuntimeObjectState{.local_offset = {4.0F, 0.0F, 0.0F}},
      App::Omikron::Model3DOData::RuntimeObjectState{.local_offset = {0.0F, 5.0F, 0.0F}},
  };
  return decor;
}

App::Omikron::SfxData make_script_trigger_sfx() {
  App::Omikron::SfxData data;
  data.magic = App::Omikron::k_sfx_magic;
  data.definitions.push_back(App::Omikron::SfxDefinition{.definition_id = 1,
      .sound_id = 0x0000FFFF,
      .sprite_id_raw = 9U,
      .flags = 0U,
      .direction = {},
      .vertical_acceleration = 0.0F,
      .lifetime = 5.0F,
      .sound_delay = 0.0F,
      .emission_delay = 0.0F,
      .raw_2c = 0.0F,
      .start_color_rgb = 0x00FFFFFFU,
      .end_color_rgb = 0x00FFFFFFU,
      .initial_scale = 1.0F,
      .cone_angle_degrees = 0.0F,
      .angular_velocity_degrees = 0.0F,
      .spawn_count = 1,
      .name = "test",
      .sprite_render_mode = 4U,
      .raw_4f = 0U});
  data.tracks.push_back(App::Omikron::SfxTrack{.track_id = 7,
      .label = "trk",
      .point_count = 1U,
      .mutable_duration_seed = 0.0F,
      .points = {App::Omikron::SfxTrackPoint{.point_id = 0,
          .definition_id = 1,
          .position = {.x = 5.0F, .y = 0.0F, .z = 0.0F},
          .segment_duration = 1.0F,
          .reference_type = 3,
          .reference_id = 0,
          .serialized_reference_ptr = 0U}}});
  data.nodes.push_back(App::Omikron::SfxNode{.node_id = 8,
      .label = "node",
      .trigger_type = 0,
      .trigger_id = 1,
      .track_id = 7,
      .serialized_track_ptr = 0U,
      .serialized_point_ptr = 0U,
      .serialized_runtime_position = {},
      .anchor_reference_type = 2,
      .anchor_reference_id = 0x00484F31,
      .serialized_anchor_ptr = 0U,
      .fixed_definition_id = 1,
      .startup_delay = 0.0F,
      .serialized_elapsed = 0.0F,
      .repeat_limit = 1,
      .serialized_repeat_index = 0,
      .flags = 0U});
  return data;
}

App::Omikron::SfxData make_cin_sfx_data(const std::uint32_t flags,
    const float channel1_start,
    const float channel1_end,
    const std::int32_t channel1_object_ref,
    const float channel2_start,
    const float channel2_end,
    const std::int32_t channel2_object_ref) {
  App::Omikron::SfxData data;
  data.magic = App::Omikron::k_sfx_magic;
  const auto definition = [](const std::int32_t id, const std::string_view name) {
    return App::Omikron::SfxDefinition{.definition_id = id,
        .sound_id = 0x0000FFFF,
        .sprite_id_raw = 9U,
        .flags = 0U,
        .direction = {},
        .vertical_acceleration = 0.0F,
        .lifetime = 5.0F,
        .sound_delay = 0.0F,
        .emission_delay = 0.0F,
        .raw_2c = 0.0F,
        .start_color_rgb = 0x00FFFFFFU,
        .end_color_rgb = 0x00FFFFFFU,
        .initial_scale = 1.0F,
        .cone_angle_degrees = 0.0F,
        .angular_velocity_degrees = 0.0F,
        .spawn_count = 1,
        .name = std::string{name},
        .sprite_render_mode = 4U,
        .raw_4f = 0U};
  };
  data.definitions = {definition(10, "narrow"), definition(20, "range")};
  data.records_b.push_back(App::Omikron::SfxCinAnimationRecord{.association_id = 1234U,
      .animation_lookup_raw = 77U,
      .flags = flags,
      .channel1_definition_id = 10,
      .channel1_start = channel1_start,
      .channel1_end = channel1_end,
      .channel1_object_ref = channel1_object_ref,
      .channel2_definition_id = 20,
      .channel2_start = channel2_start,
      .channel2_end = channel2_end,
      .channel2_object_ref = channel2_object_ref});
  return data;
}

void add_cin_sfx_sprite(App::Omikron::ScxData& scx) {
  scx.sprites.push_back(App::Omikron::ScxSpriteEntry{.name = "effect",
      .sprite_id = 9U,
      .runtime_sprite_placeholder = 0U,
      .serialized_field_1c = 0U,
      .file_offset = 0U});
  scx.models.push_back(App::Omikron::ScxModelResource{});
}

App::Omikron::ScxData make_sfx_script_scx() {
  App::Omikron::ScxData scx;
  scx.scripts.push_back(make_script("arrival", 1));
  scx.scripts.front().script_id = 1;
  scx.sprites.push_back(App::Omikron::ScxSpriteEntry{.name = "effect",
      .sprite_id = 9U,
      .runtime_sprite_placeholder = 0U,
      .serialized_field_1c = 0U,
      .file_offset = 0U});
  scx.models.push_back(App::Omikron::ScxModelResource{});
  return scx;
}

}  // namespace

TEST_SUITE("Core::Scenario::ScenarioRuntime") {
  TEST_CASE("Cin-SFX channels use independent inclusive logical clocks and restart on wrap") {
    BodyResourcesFixture resources{make_body_resources({}, {}, false, 14U)};
    resources.scx.section0_records.clear();
    resources.scx.section0_resources.clear();
    add_cin_sfx_sprite(resources.scx);
    App::Omikron::SfxData sfx{make_cin_sfx_data(0x98U, 10.0F, 10.0F, 4, 10.0F, 12.0F, 4)};
    const auto model{make_cin_sfx_model_resource()};

    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(resources.scx, resources.bytes, "cin-clock", nullptr, false, &sfx)
            .has_value());
    runtime.character_runtime().set_model_loader(
        [model](const std::string_view)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          return model;
        });
    REQUIRE(runtime
            .activate_character(118,
                make_character_area(),
                App::Script::AreaCharacterActivationRequest{
                    .character_id = 310, .apply_area_transform = true})
            .has_value());

    App::Script::BodyAnimationRequest request{
        .character_body_identity = runtime.character_runtime().find(310)->body_identity,
        .character_id = 310,
        .script_instance_id = 11U,
        .object_binding = "SelectedRoot",
        .animation_index = 0,
        .body_animation_vector = {},
        .authored_offset = {},
        .execution_count = 0,
        .execution_limit = 2};
    for (std::uint32_t service{0}; service < 13U; ++service) {
      request.previous_progress = static_cast<float>(service) - 0.4F;
      request.current_progress = static_cast<float>(service) + 0.6F;
      request.first_tick = service == 0U;
      REQUIRE(runtime.select_body_animation(request).has_value());
    }

    REQUIRE_EQ(runtime.cin_sfx_playbacks().size(), 1U);
    const App::CinSfxPlayback& playback{runtime.cin_sfx_playbacks().front()};
    CHECK_EQ(playback.association_id, 1234U);
    CHECK_EQ(playback.body_previous_progress, doctest::Approx(11.6F));
    CHECK_EQ(playback.body_current_progress, doctest::Approx(12.6F));
    CHECK_EQ(playback.channels.at(0).elapsed, doctest::Approx(13.0F));
    CHECK_EQ(playback.channels.at(1).elapsed, doctest::Approx(13.0F));
    CHECK_EQ(playback.channels.at(0).emissions_this_execution, 1U);
    CHECK_EQ(playback.channels.at(1).emissions_this_execution, 3U);
    CHECK_EQ(runtime.sfx_diagnostics().queued_request_count, 4U);

    request.previous_progress = 0.0F;
    request.current_progress = 1.0F;
    request.first_tick = false;
    request.execution_count = 1U;
    REQUIRE(runtime.select_body_animation(request).has_value());
    CHECK_FALSE(playback.channels.at(0).active);
    CHECK_EQ(playback.channels.at(0).elapsed, doctest::Approx(0.0F));
    CHECK_EQ(playback.channels.at(0).emissions_this_execution, 0U);

    request.previous_progress = 1.0F;
    request.current_progress = 2.0F;
    REQUIRE(runtime.select_body_animation(request).has_value());
    CHECK(playback.channels.at(0).active);
    CHECK_EQ(playback.channels.at(0).elapsed, doctest::Approx(1.0F));

    App::Script::BodyAnimationRequest second_instance{request};
    second_instance.script_instance_id = 12U;
    second_instance.previous_progress = 0.0F;
    second_instance.current_progress = 1.0F;
    second_instance.first_tick = true;
    second_instance.execution_count = 0U;
    REQUIRE(runtime.select_body_animation(second_instance).has_value());
    REQUIRE_EQ(runtime.cin_sfx_playbacks().size(), 2U);
    CHECK_EQ(runtime.cin_sfx_playbacks()[0].script_instance_id, 11U);
    CHECK_EQ(runtime.cin_sfx_playbacks()[0].channels.at(0).elapsed, doctest::Approx(1.0F));
    CHECK_EQ(runtime.cin_sfx_playbacks()[1].script_instance_id, 12U);
    CHECK_EQ(runtime.cin_sfx_playbacks()[1].channels.at(0).elapsed, doctest::Approx(1.0F));
  }

  TEST_CASE("Cin-SFX attachment stays within the selected hierarchy and uses visual world XYZ") {
    BodyResourcesFixture resources{make_body_resources({}, {}, false, 4U)};
    resources.scx.section0_records.clear();
    resources.scx.section0_resources.clear();
    add_cin_sfx_sprite(resources.scx);
    App::Omikron::SfxData sfx{make_cin_sfx_data(0x88U, 0.0F, 0.0F, 4, 0.0F, 2.0F, 10)};
    const auto model{make_cin_sfx_model_resource()};

    App::ScenarioRuntime runtime;
    REQUIRE(
        runtime.initialize(resources.scx, resources.bytes, "cin-attachment", nullptr, false, &sfx)
            .has_value());
    runtime.character_runtime().set_model_loader(
        [model](const std::string_view)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          return model;
        });
    REQUIRE(runtime
            .activate_character(118,
                make_character_area(),
                App::Script::AreaCharacterActivationRequest{
                    .character_id = 310, .apply_area_transform = true})
            .has_value());

    const App::Script::BodyAnimationRequest request{
        .character_body_identity = runtime.character_runtime().find(310)->body_identity,
        .character_id = 310,
        .object_binding = "SelectedRoot",
        .animation_index = 0,
        .previous_progress = 0.0F,
        .current_progress = 0.6F,
        .body_animation_vector = {},
        .authored_offset = {},
        .first_tick = true,
        .execution_count = 0,
        .execution_limit = 1};
    REQUIRE(runtime.select_body_animation(request).has_value());

    const App::Character::RuntimeCharacter* character{runtime.character_runtime().find(310)};
    REQUIRE(character != nullptr);
    const auto expected{character->object_world_transform(2U)};
    REQUIRE(expected.has_value());
    REQUIRE_EQ(runtime.cin_sfx_playbacks().size(), 1U);
    const App::CinSfxPlayback& playback{runtime.cin_sfx_playbacks().front()};
    CHECK_EQ(playback.channels.at(0).resolved_object_index, std::optional<std::size_t>{2U});
    CHECK_EQ(playback.channels.at(0).cached_position.x, doctest::Approx(expected->translation.x));
    CHECK_EQ(playback.channels.at(0).cached_position.y, doctest::Approx(expected->translation.y));
    CHECK_EQ(playback.channels.at(0).cached_position.z, doctest::Approx(expected->translation.z));
    CHECK_EQ(playback.channels.at(0).emissions_this_execution, 1U);
    CHECK(playback.channels.at(1).active);
    CHECK_FALSE(playback.channels.at(1).enabled);
    CHECK_EQ(playback.channels.at(1).elapsed, doctest::Approx(1.0F));
    CHECK_FALSE(playback.channels.at(1).resolved_object_index.has_value());
    CHECK(playback.channels.at(1).attachment_missing);
    CHECK_EQ(runtime.sfx_diagnostics().queued_request_count, 1U);
  }

  TEST_CASE("Structured 3DO cameras select and mutate an instance-local camera A") {
    App::ScenarioRuntime runtime;
    const App::Omikron::Model3DOData decor{make_camera_decor()};
    runtime.bind_decor_model(&decor);

    REQUIRE(runtime.select_camera("CAM_A").has_value());
    const App::Omikron::CameraRecord* selected{runtime.selected_structured_camera()};
    REQUIRE(selected != nullptr);
    CHECK_EQ(selected->name, "CAM_A");
    CHECK(selected->eye.x == doctest::Approx(0.0F));

    REQUIRE(runtime
            .interpolate_cameras(App::Script::CameraInterpolationRequest{.camera_a = "CAM_A",
                .camera_b = "CAM_B",
                .fraction = 0.25F,
                .snap_to_target = false})
            .has_value());
    selected = runtime.selected_structured_camera();
    REQUIRE(selected != nullptr);
    CHECK_EQ(selected->name, "CAM_A");
    CHECK(selected->eye.x == doctest::Approx(25.0F));
    CHECK(selected->target.z == doctest::Approx(75.0F));
    CHECK(selected->roll_degrees == doctest::Approx(5.0F));
    CHECK(selected->horizontal_fov_degrees == doctest::Approx(75.0F));

    REQUIRE(runtime
            .interpolate_cameras(App::Script::CameraInterpolationRequest{
                .camera_a = "CAM_A", .camera_b = "CAM_B", .fraction = 1.0F, .snap_to_target = true})
            .has_value());
    selected = runtime.selected_structured_camera();
    REQUIRE(selected != nullptr);
    CHECK_EQ(selected->name, "CAM_A");  // Native copy does not overwrite the record name.
    CHECK(selected->eye.x == doctest::Approx(100.0F));
    CHECK(selected->target.z == doctest::Approx(150.0F));
    CHECK(selected->roll_degrees == doctest::Approx(20.0F));
    CHECK(selected->horizontal_fov_degrees == doctest::Approx(60.0F));
  }

  TEST_CASE("Structured camera selection is frame-scoped, not persistent") {
    App::ScenarioRuntime runtime;
    const App::Omikron::Model3DOData decor{make_camera_decor()};
    runtime.bind_decor_model(&decor);

    // Frame N: an active SelectCamera publishes the camera.
    REQUIRE(runtime.select_camera("CAM_A").has_value());
    REQUIRE(runtime.selected_structured_camera() != nullptr);
    CHECK_EQ(runtime.selected_structured_camera()->name, "CAM_A");

    // Frame N+1: the slot is cleared before script service; nothing
    // republished, so no stale camera remains logically selected.
    runtime.tick(1.0F / 30.0F);
    CHECK(runtime.selected_structured_camera() == nullptr);

    // Camera-editing poses follow the same frame-scoped rule.
    REQUIRE(runtime
            .apply_camera_editing_pose(App::Script::CameraEditingPose{
                .context_id = 1, .editing_name = "edit", .segment_name = "seg"})
            .has_value());
    CHECK(runtime.selected_structured_camera() != nullptr);
    runtime.tick(1.0F / 30.0F);
    CHECK(runtime.selected_structured_camera() == nullptr);

    // A fresh selection still publishes normally after the handoff.
    REQUIRE(runtime.select_camera("CAM_B").has_value());
    REQUIRE(runtime.selected_structured_camera() != nullptr);
    CHECK_EQ(runtime.selected_structured_camera()->name, "CAM_B");
  }

  TEST_CASE("CTL sound markers resolve by SCX sound hID and tolerate misses") {
    App::Omikron::ScxData scx;
    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(scx, std::span<const std::byte>{}, "ctl_audio", nullptr, false)
            .has_value());
    // No SCX sound owns hID 999: nonfatal, never an index lookup, no throw.
    runtime.play_ctl_sound_marker(999, {.x = 1.0F, .y = 2.0F, .z = 3.0F});
    CHECK(runtime.initialized());
  }

  TEST_CASE("Initializes an empty scenario") {
    App::Omikron::ScxData scx;
    App::ScenarioRuntime runtime;

    const auto result{
        runtime.initialize(scx, std::span<const std::byte>{}, "empty", nullptr, false)};
    REQUIRE(result.has_value());
    CHECK(runtime.initialized());
    CHECK(runtime.script_runtime() != nullptr);
    CHECK_EQ(runtime.script_scenario_name(), "empty");
    CHECK_EQ(runtime.sprite_resource_count(), 0U);
    CHECK(runtime.script_runtime()->instances().empty());
  }

  TEST_CASE("Loads parsed SCX scripts without runtime instances by default") {
    App::Omikron::ScxData scx;
    scx.scripts.push_back(make_script("a", 1));
    scx.scripts.push_back(make_script("b", 1));
    scx.scripts.push_back(make_script("c", 0));
    App::ScenarioRuntime runtime;

    const auto result{
        runtime.initialize(scx, std::span<const std::byte>{}, "source-scripts", nullptr, false)};
    REQUIRE(result.has_value());
    REQUIRE(runtime.script_runtime() != nullptr);
    CHECK_EQ(runtime.script_runtime()->scx().scripts.size(), 3U);
    CHECK(runtime.script_runtime()->instances().empty());
  }

  TEST_CASE("Activating startup scripts creates one instance per rooted script") {
    App::Omikron::ScxData scx;
    scx.scripts.push_back(make_script("a", 1));
    scx.scripts.push_back(make_script("b", 0));
    App::ScenarioRuntime runtime;

    const auto result{
        runtime.initialize(scx, std::span<const std::byte>{}, "active", nullptr, true)};
    REQUIRE(result.has_value());
    REQUIRE(runtime.script_runtime() != nullptr);
    REQUIRE_EQ(runtime.script_runtime()->instances().size(), 1U);
    CHECK_EQ(runtime.script_runtime()->instances().at(0).script_name, "a");
  }

  TEST_CASE("Spawns a script instance on an inactive runtime") {
    App::Omikron::ScxData scx;
    scx.scripts.push_back(make_script("a", 1));
    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(scx, std::span<const std::byte>{}, "inactive", nullptr, false)
            .has_value());

    const auto instance{runtime.spawn_script_instance(0)};
    REQUIRE(instance.has_value());
    REQUIRE_EQ(runtime.script_runtime()->instances().size(), 1U);
    CHECK_EQ(runtime.script_runtime()->instances().at(0).script_name, "a");
  }

  TEST_CASE("successful explicit script activation triggers matching SFX exactly once") {
    App::Omikron::ScxData scx{make_sfx_script_scx()};
    App::Omikron::SfxData sfx{make_script_trigger_sfx()};
    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(scx, std::span<const std::byte>{}, "trigger", nullptr, false, &sfx)
            .has_value());
    REQUIRE(runtime.sfx_runtime() != nullptr);
    CHECK(runtime.sfx_diagnostics().active_node_count == 0U);
    REQUIRE(runtime.spawn_script_instance(0).has_value());
    CHECK(runtime.sfx_diagnostics().active_node_count == 1U);
    CHECK(runtime.sfx_runtime()->nodes().front().current_position.x == doctest::Approx(5.0F));
  }

  TEST_CASE(
      "failed character script activation does not trigger SFX and success uses live HO1 anchor") {
    App::Omikron::ScxData scx{make_sfx_script_scx()};
    App::Omikron::SfxData sfx{make_script_trigger_sfx()};
    App::ScenarioRuntime runtime;
    REQUIRE(runtime
            .initialize(
                scx, std::span<const std::byte>{}, "character-trigger", nullptr, false, &sfx)
            .has_value());
    CHECK_FALSE(runtime.spawn_character_script_instance(0, 310, 999U, 0).has_value());
    CHECK(runtime.sfx_diagnostics().active_node_count == 0U);

    runtime.character_runtime().set_model_loader(
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          auto resource{std::make_shared<App::Character::ModelResource>()};
          resource->name = name;
          resource->groups.push_back(App::Omikron::MaterialGroup{});
          return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
        });
    const App::Omikron::IamAreaRecord area{make_character_area()};
    REQUIRE(runtime
            .activate_character(118,
                area,
                App::Script::AreaCharacterActivationRequest{
                    .character_id = 310, .apply_area_transform = true})
            .has_value());
    App::Character::RuntimeCharacter* character{runtime.character_runtime().find(310)};
    REQUIRE(character != nullptr);
    character->transform.translation.x = 20.0F;

    REQUIRE(
        runtime.spawn_character_script_instance(0, 310, character->body_identity, 0).has_value());
    CHECK(runtime.sfx_diagnostics().active_node_count == 1U);
    CHECK(runtime.sfx_runtime()->nodes().front().current_position.x == doctest::Approx(25.0F));
  }

  TEST_CASE("Spawns a character-bound script only for an active runtime character") {
    App::Omikron::ScxData scx;
    scx.scripts.push_back(make_script("unrelated", 1));
    scx.scripts.push_back(make_script("1KaylArrives", 1));
    scx.scripts.at(0).script_id = 99;
    scx.scripts.at(1).script_id = 1;
    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(scx, std::span<const std::byte>{}, "character", nullptr, false)
            .has_value());

    auto missing{runtime.spawn_character_script_instance(1, 310, 999U, 0)};
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().find("does not exist") != std::string::npos);

    runtime.character_runtime().set_model_loader(
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          auto resource{std::make_shared<App::Character::ModelResource>()};
          resource->name = name;
          resource->groups.push_back(App::Omikron::MaterialGroup{});
          return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
        });
    const App::Omikron::IamAreaRecord area{make_character_area()};
    const auto activated{runtime.activate_character(118,
        area,
        App::Script::AreaCharacterActivationRequest{
            .character_id = 310, .apply_area_transform = true})};
    const std::string activation_error{activated ? std::string{} : activated.error()};
    CAPTURE(activation_error);
    REQUIRE(activated.has_value());

    const App::Character::RuntimeCharacter* const character{runtime.character_runtime().find(310)};
    REQUIRE(character != nullptr);
    const auto created{
        runtime.spawn_character_script_instance(1, 310, character->body_identity, -5)};
    REQUIRE(created.has_value());
    const App::Script::ScriptInstance* instance{
        runtime.script_runtime()->instance(created.value())};
    REQUIRE(instance != nullptr);
    CHECK_EQ(instance->source_script_index, 1U);
    CHECK_EQ(instance->script_name, "1KaylArrives");
    CHECK_EQ(instance->launch_context.character_id, std::optional<std::int16_t>{310});
    CHECK_EQ(instance->launch_context.parameter, -5);
  }

  TEST_CASE("Sprite pool lifecycle works through the runtime") {
    App::Omikron::ScxData scx;
    App::ScenarioRuntime runtime;
    REQUIRE(
        runtime.initialize(scx, std::span<const std::byte>{}, "pool", nullptr, false).has_value());

    App::Sprite::SpritePool& pool{runtime.sprite_pool()};
    const auto handle{pool.create(0, 0, 4, {1.0F, 2.0F, 3.0F})};
    REQUIRE(handle.has_value());
    CHECK(pool.find(handle.value()) != nullptr);
    REQUIRE(pool.attach(handle.value()).has_value());
    CHECK(pool.attached(handle.value()));
    REQUIRE(pool.detach(handle.value()).has_value());
    CHECK_FALSE(pool.attached(handle.value()));
    REQUIRE(pool.destroy(handle.value()).has_value());
    CHECK(pool.find(handle.value()) == nullptr);
  }

  TEST_CASE("World anchor defaults to the origin and is settable") {
    App::Omikron::ScxData scx;
    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(scx, std::span<const std::byte>{}, "anchor", nullptr, false)
            .has_value());

    CHECK_EQ(runtime.world_anchor().at(0), 0.0F);
    CHECK_EQ(runtime.world_anchor().at(1), 0.0F);
    CHECK_EQ(runtime.world_anchor().at(2), 0.0F);

    runtime.set_world_anchor({4.0F, 5.0F, 6.0F});
    CHECK_EQ(runtime.world_anchor().at(0), 4.0F);
    CHECK_EQ(runtime.world_anchor().at(1), 5.0F);
    CHECK_EQ(runtime.world_anchor().at(2), 6.0F);
  }

  TEST_CASE("Relative body animation anchors, binds by script id, and isolates shared models") {
    BodyResourcesFixture resources{make_body_resources()};
    const std::shared_ptr<const App::Character::ModelResource> shared{make_body_model_resource()};
    const auto loader = [shared](const std::string_view)
        -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
      return shared;
    };

    App::ScenarioRuntime animated_runtime;
    REQUIRE(animated_runtime.initialize(resources.scx, resources.bytes, "animated", nullptr, false)
            .has_value());
    animated_runtime.character_runtime().set_model_loader(loader);
    REQUIRE(animated_runtime
            .activate_character(118,
                make_character_area(),
                App::Script::AreaCharacterActivationRequest{
                    .character_id = 310, .apply_area_transform = true})
            .has_value());
    App::Character::RuntimeCharacter* animated_character{
        animated_runtime.character_runtime().find(310)};
    REQUIRE(animated_character != nullptr);
    // Keep this regression focused on the relative 3DP seed. Root-orientation
    // transformation is covered independently below.
    animated_character->set_principal_orientation({});

    App::ScenarioRuntime untouched_runtime;
    REQUIRE(
        untouched_runtime.initialize(resources.scx, resources.bytes, "untouched", nullptr, false)
            .has_value());
    untouched_runtime.character_runtime().set_model_loader(loader);
    REQUIRE(untouched_runtime
            .activate_character(118,
                make_character_area(),
                App::Script::AreaCharacterActivationRequest{
                    .character_id = 310, .apply_area_transform = true})
            .has_value());

    const App::Script::RelativeBodyAnimationRequest request{
        .character_body_identity = animated_character->body_identity,
        .character_id = 310,
        .object_binding = "RootBody",
        .animation_index = 0,
        .previous_progress = 0.0F,
        .current_progress = 1.0F,
        .body_animation_vector = {},
        .path_index = 0,
        .subpath_index = 0,
        .authored_offset = {0.0F, 0.0F, 0.0F},
        .first_tick = true,
        .execution_count = 0,
        .execution_limit = 1};
    const auto applied{animated_runtime.select_relative_body_animation(request)};
    REQUIRE(applied.has_value());
    CHECK_EQ(applied->max_frame_index, 3U);

    const App::Character::RuntimeCharacter* animated{
        animated_runtime.character_runtime().find(310)};
    const App::Character::RuntimeCharacter* untouched{
        untouched_runtime.character_runtime().find(310)};
    REQUIRE(animated != nullptr);
    REQUIRE(untouched != nullptr);
    CHECK_EQ(animated->body_animation.final_anchor.x, doctest::Approx(-478.393341F));
    CHECK_EQ(animated->body_animation.final_anchor.y, doctest::Approx(-43.900246F));
    CHECK_EQ(animated->body_animation.final_anchor.z, doctest::Approx(27.611773F));
    // The [0,1] root interval is additive after the absolute path anchor.
    CHECK_EQ(animated->body_animation.root_motion_delta.x, doctest::Approx(10.0F));
    CHECK_EQ(animated->transform.translation.x, doctest::Approx(-468.393341F));
    CHECK_EQ(animated->object_poses.at(0).channel_id, std::optional<std::uint32_t>{2});
    CHECK_EQ(animated->object_poses.at(1).channel_id, std::optional<std::uint32_t>{3});
    CHECK(animated->runtime_objects.at(0).animation_matrix.has_value());
    CHECK(animated->runtime_objects.at(1).animation_matrix.has_value());
    CHECK_EQ(animated->body_animation.body_animation_vector.z, doctest::Approx(0.0F));

    // Both runtime characters share the immutable resource, but only one pose changed.
    CHECK(animated->model_resource == untouched->model_resource);
    CHECK_FALSE(shared->model.runtime_objects.at(0).animation_matrix.has_value());
    CHECK_FALSE(untouched->runtime_objects.at(0).animation_matrix.has_value());
    CHECK_EQ(untouched->transform.translation.x, doctest::Approx(-1.0F));

    App::Script::RelativeBodyAnimationRequest fractional{request};
    fractional.previous_progress = 1.0F;
    fractional.current_progress = 1.5F;
    fractional.first_tick = false;
    REQUIRE(animated_runtime.select_relative_body_animation(fractional).has_value());
    animated = animated_runtime.character_runtime().find(310);
    REQUIRE(animated != nullptr);
    CHECK_EQ(animated->body_animation.root_motion_delta.x, doctest::Approx(5.0F));
    CHECK_EQ(animated->transform.translation.x, doctest::Approx(-463.393341F));
  }

  TEST_CASE("Body animation uses 3DA key zero, live orientation, and repeat reseeding") {
    BodyResourcesFixture resources{
        make_body_resources(App::Runtime::Vec3{.x = 100.0F, .y = -50.0F, .z = 25.0F})};
    resources.scx.section0_records.clear();
    resources.scx.section0_resources.clear();
    const std::shared_ptr<const App::Character::ModelResource> shared{make_body_model_resource()};
    const auto loader = [shared](const std::string_view)
        -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
      return shared;
    };

    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(resources.scx, resources.bytes, "body-world-anchor", nullptr, false)
            .has_value());
    runtime.character_runtime().set_model_loader(loader);
    REQUIRE(runtime
            .activate_character(118,
                make_character_area(),
                App::Script::AreaCharacterActivationRequest{
                    .character_id = 310, .apply_area_transform = true})
            .has_value());

    App::Character::RuntimeCharacter* character{runtime.character_runtime().find(310)};
    REQUIRE(character != nullptr);
    // Ordinary SelectBodyAnimation must ignore this pre-existing actor XYZ.
    character->transform.translation = {.x = 7000.0F, .y = -120.0F, .z = 3040.0F};
    character->set_principal_orientation({.x = 0.0F, .y = 45.0F, .z = 0.0F});

    App::Script::BodyAnimationRequest request{.character_body_identity = character->body_identity,
        .character_id = 310,
        .object_binding = "RootBody",
        .animation_index = 0,
        .previous_progress = 0.0F,
        .current_progress = 1.0F,
        .body_animation_vector = {0.0F, 90.0F, 0.0F},
        .authored_offset = {0.0F, 0.0F, 0.0F},
        .first_tick = true,
        .execution_count = 0,
        .execution_limit = 2};

    // sample[0] is the absolute seed. sample[1] is +X local motion. Runtime
    // integrates this interval through the *previously persisted* live +0x9C
    // orientation before storing this invocation's args 4/5/6. The previous
    // principal yaw is 45°, so +X becomes equal +X/+Z. Only after integration
    // do this invocation's args overwrite the principal orientation with 90°.
    // The frame-1 3DA quaternion remains excluded from the root-motion basis.
    REQUIRE(runtime.select_body_animation(request).has_value());
    character = runtime.character_runtime().find(310);
    REQUIRE(character != nullptr);
    CHECK_EQ(character->body_animation.final_anchor.x, doctest::Approx(100.0F));
    CHECK_EQ(character->body_animation.final_anchor.y, doctest::Approx(-50.0F));
    CHECK_EQ(character->body_animation.final_anchor.z, doctest::Approx(25.0F));
    CHECK_EQ(character->body_animation.root_motion_delta.x,
        doctest::Approx(7.0710678F).epsilon(0.0001F));
    CHECK_EQ(character->body_animation.root_motion_delta.y, doctest::Approx(0.0F).epsilon(0.0001F));
    CHECK_EQ(character->body_animation.root_motion_delta.z,
        doctest::Approx(7.0710678F).epsilon(0.0001F));
    CHECK_EQ(character->transform.translation.x, doctest::Approx(107.071068F).epsilon(0.0001F));
    CHECK_EQ(character->transform.translation.y, doctest::Approx(-50.0F));
    CHECK_EQ(character->transform.translation.z, doctest::Approx(32.071068F).epsilon(0.0001F));
    CHECK_EQ(character->body_animation.accumulated_visual_translation.z,
        doctest::Approx(7.0710678F).epsilon(0.0001F));
    CHECK_EQ(character->body_animation.accumulated_logical_actor_translation.z,
        doctest::Approx(7.0710678F).epsilon(0.0001F));
    CHECK_EQ(character->principal_orientation_degrees.x, doctest::Approx(0.0F));
    CHECK_EQ(character->principal_orientation_degrees.y, doctest::Approx(90.0F));
    CHECK_EQ(character->principal_orientation_degrees.z, doctest::Approx(0.0F));
    const App::Runtime::Vec3 presentation_x{App::Runtime::transform_vector(
        App::Runtime::Vec3{.x = 1.0F}, character->presentation_transform().matrix)};
    CHECK_EQ(presentation_x.x, doctest::Approx(0.0F).epsilon(0.0001));
    CHECK_EQ(presentation_x.z, doctest::Approx(1.0F).epsilon(0.0001));

    // Simulate the scheduler's next command execution. first_tick is
    // deliberately false: previous_progress==0 is itself the authoritative
    // execution-boundary signal. The pass must reseed to sample[0], then
    // integrate through the 90° principal orientation persisted by the prior
    // invocation. The same args overwrite the same principal state afterward.
    App::Script::BodyAnimationRequest repeated{request};
    repeated.first_tick = false;
    repeated.execution_count = 1;
    REQUIRE(runtime.select_body_animation(repeated).has_value());
    character = runtime.character_runtime().find(310);
    REQUIRE(character != nullptr);
    CHECK_EQ(character->body_animation.final_anchor.x, doctest::Approx(100.0F));
    CHECK_EQ(character->transform.translation.x, doctest::Approx(100.0F).epsilon(0.0001));
    CHECK_EQ(character->transform.translation.z, doctest::Approx(35.0F).epsilon(0.0001));
    CHECK_EQ(character->body_animation.accumulated_visual_translation.z,
        doctest::Approx(10.0F).epsilon(0.0001));
    CHECK_EQ(character->body_animation.accumulated_logical_actor_translation.z,
        doctest::Approx(10.0F).epsilon(0.0001));
    CHECK_EQ(character->body_animation.root_motion_delta.x, doctest::Approx(0.0F).epsilon(0.0001F));

    // Authored 7/8/9 values offset the 3DA reference, not the prior actor XYZ.
    App::Script::BodyAnimationRequest offset_request{request};
    offset_request.current_progress = 0.0F;
    offset_request.authored_offset = {10.0F, 20.0F, 30.0F};
    REQUIRE(runtime.select_body_animation(offset_request).has_value());
    character = runtime.character_runtime().find(310);
    REQUIRE(character != nullptr);
    CHECK_EQ(character->body_animation.final_anchor.x, doctest::Approx(103.937008F));
    CHECK_EQ(character->body_animation.final_anchor.y, doctest::Approx(-42.125984F));
    CHECK_EQ(character->body_animation.final_anchor.z, doctest::Approx(36.811024F));
    CHECK_EQ(character->transform.translation.x, doctest::Approx(103.937008F));
    CHECK_EQ(character->transform.translation.y, doctest::Approx(-42.125984F));
    CHECK_EQ(character->transform.translation.z, doctest::Approx(36.811024F));
  }

  TEST_CASE("body animation separates hierarchy-root visuals from actor-object logic") {
    BodyResourcesFixture resources{
        make_body_resources(App::Runtime::Vec3{.x = 100.0F, .y = -50.0F, .z = 25.0F},
            App::Runtime::Vec3{.x = 10.0F, .y = 6.0F, .z = 0.0F},
            true)};
    resources.scx.section0_records.clear();
    resources.scx.section0_resources.clear();
    const std::shared_ptr<const App::Character::ModelResource> shared{
        make_split_actor_model_resource()};
    REQUIRE_EQ(shared->model.root_mesh_index, 0);
    REQUIRE_EQ(shared->actor_object_index, std::optional<std::size_t>{1U});
    const auto loader = [shared](const std::string_view)
        -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
      return shared;
    };

    const auto make_runtime = [&resources, &loader]() {
      auto runtime{std::make_unique<App::ScenarioRuntime>()};
      REQUIRE(
          runtime->initialize(resources.scx, resources.bytes, "split", nullptr, false).has_value());
      runtime->character_runtime().set_model_loader(loader);
      REQUIRE(runtime
              ->activate_character(118,
                  make_character_area(),
                  App::Script::AreaCharacterActivationRequest{
                      .character_id = 310, .apply_area_transform = true})
              .has_value());
      return runtime;
    };

    auto visual_runtime{make_runtime()};
    App::Character::RuntimeCharacter* visual{visual_runtime->character_runtime().find(310)};
    REQUIRE(visual != nullptr);
    visual->transform.translation = {.x = 7000.0F, .y = -120.0F, .z = 3040.0F};
    visual->set_principal_orientation({});
    const App::Script::BodyAnimationRequest root_request{
        .character_body_identity = visual->body_identity,
        .character_id = 310,
        .object_binding = "RootBody",
        .animation_index = 0,
        .previous_progress = 0.0F,
        .current_progress = 1.0F,
        .body_animation_vector = {},
        .authored_offset = {},
        .first_tick = true,
        .execution_count = 0,
        .execution_limit = 1};
    REQUIRE(visual_runtime->select_body_animation(root_request).has_value());
    CHECK_EQ(visual->transform.translation.x, doctest::Approx(7000.0F));
    CHECK_EQ(visual->transform.translation.y, doctest::Approx(-120.0F));
    CHECK_EQ(visual->transform.translation.z, doctest::Approx(3040.0F));
    const auto visual_root{visual->object_world_transform(0U)};
    const auto visual_child{visual->object_world_transform(1U)};
    REQUIRE(visual_root.has_value());
    REQUIRE(visual_child.has_value());
    CHECK_EQ(visual_root->translation.x, doctest::Approx(110.0F));
    CHECK_EQ(visual_root->translation.y, doctest::Approx(-44.0F));
    CHECK_EQ(visual_root->translation.z, doctest::Approx(25.0F));
    CHECK_EQ(visual_child->translation.x - visual_root->translation.x, doctest::Approx(-2.0F));
    CHECK_EQ(visual_child->translation.y, doctest::Approx(-44.0F));
    CHECK_EQ(visual->body_animation.logical_actor_delta.x, doctest::Approx(0.0F));
    CHECK_EQ(visual->body_animation.accumulated_visual_translation.x, doctest::Approx(10.0F));

    REQUIRE(visual_runtime->select_body_animation(root_request).has_value());
    const auto reseeded_root{visual->object_world_transform(0U)};
    REQUIRE(reseeded_root.has_value());
    CHECK_EQ(visual->transform.translation.x, doctest::Approx(7000.0F));
    CHECK_EQ(reseeded_root->translation.x, doctest::Approx(110.0F));

    auto actor_runtime{make_runtime()};
    App::Character::RuntimeCharacter* actor{actor_runtime->character_runtime().find(310)};
    REQUIRE(actor != nullptr);
    actor->transform.translation = {.x = 7000.0F, .y = -120.0F, .z = 3040.0F};
    actor->set_principal_orientation({});
    App::Script::BodyAnimationRequest actor_request{root_request};
    actor_request.character_body_identity = actor->body_identity;
    actor_request.object_binding = "Child";
    REQUIRE(actor_runtime->select_body_animation(actor_request).has_value());
    CHECK_EQ(actor->transform.translation.x, doctest::Approx(110.0F));
    CHECK_EQ(actor->transform.translation.y, doctest::Approx(-50.0F));
    CHECK_EQ(actor->transform.translation.z, doctest::Approx(25.0F));
    const auto actor_visual{actor->object_world_transform(1U)};
    REQUIRE(actor_visual.has_value());
    CHECK_EQ(actor_visual->translation.x, doctest::Approx(110.0F));
    CHECK_EQ(actor_visual->translation.y, doctest::Approx(-44.0F));
    CHECK_EQ(actor_visual->translation.z, doctest::Approx(25.0F));
    CHECK_EQ(actor->body_animation.logical_actor_delta.x, doctest::Approx(10.0F));
    CHECK_EQ(actor->body_animation.logical_actor_delta.y, doctest::Approx(0.0F));
    CHECK_EQ(actor->body_animation.accumulated_visual_translation.y, doctest::Approx(6.0F));
    CHECK_EQ(actor->body_animation.accumulated_logical_actor_translation.y, doctest::Approx(0.0F));
  }

  TEST_CASE("Body root motion keeps logical Y fixed and moves the visual root") {
    BodyResourcesFixture resources{
        make_body_resources(App::Runtime::Vec3{.x = 100.0F, .y = -50.0F, .z = 25.0F},
            App::Runtime::Vec3{.x = 10.0F, .y = 6.0F, .z = 0.0F})};
    resources.scx.section0_records.clear();
    resources.scx.section0_resources.clear();
    const std::shared_ptr<const App::Character::ModelResource> shared{make_body_model_resource()};
    const auto loader = [shared](const std::string_view)
        -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
      return shared;
    };

    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(resources.scx, resources.bytes, "body-vertical-root", nullptr, false)
            .has_value());
    runtime.character_runtime().set_model_loader(loader);
    REQUIRE(runtime
            .activate_character(118,
                make_character_area(),
                App::Script::AreaCharacterActivationRequest{
                    .character_id = 310, .apply_area_transform = true})
            .has_value());
    App::Character::RuntimeCharacter* character{runtime.character_runtime().find(310)};
    REQUIRE(character != nullptr);
    character->set_principal_orientation({});

    const App::Script::BodyAnimationRequest request{
        .character_body_identity = character->body_identity,
        .character_id = 310,
        .object_binding = "RootBody",
        .animation_index = 0,
        .previous_progress = 0.0F,
        .current_progress = 1.0F,
        .body_animation_vector = {},
        .authored_offset = {},
        .first_tick = true,
        .execution_count = 0,
        .execution_limit = 1};
    REQUIRE(runtime.select_body_animation(request).has_value());
    character = runtime.character_runtime().find(310);
    REQUIRE(character != nullptr);

    CHECK_EQ(character->body_animation.root_motion_delta.x, doctest::Approx(10.0F));
    CHECK_EQ(character->body_animation.root_motion_delta.y, doctest::Approx(6.0F));
    CHECK_EQ(character->transform.translation.x, doctest::Approx(110.0F));
    CHECK_EQ(character->transform.translation.y, doctest::Approx(-50.0F));
    CHECK_EQ(character->runtime_objects.at(0).world_translation.y, doctest::Approx(6.0F));
  }

  TEST_CASE("Body animation uses the 3DA reference anchor without a 3DP resource") {
    BodyResourcesFixture resources{make_body_resources()};
    resources.scx.section0_records.clear();
    resources.scx.section0_resources.clear();
    const std::shared_ptr<const App::Character::ModelResource> shared{make_body_model_resource()};
    const auto loader = [shared](const std::string_view)
        -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
      return shared;
    };

    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(resources.scx, resources.bytes, "body", nullptr, false).has_value());
    runtime.character_runtime().set_model_loader(loader);
    REQUIRE(runtime
            .activate_character(118,
                make_character_area(),
                App::Script::AreaCharacterActivationRequest{
                    .character_id = 310, .apply_area_transform = true})
            .has_value());
    App::Character::RuntimeCharacter* before{runtime.character_runtime().find(310)};
    REQUIRE(before != nullptr);
    const std::uint64_t initial_revision{before->pose_revision};
    // Keep this test focused on authored offset conversion and hierarchy
    // binding. The dedicated regression immediately above covers non-zero
    // sample-zero anchors and live root orientation.
    before->transform.translation = {};
    before->set_principal_orientation({});

    const App::Script::BodyAnimationRequest root_request{
        .character_body_identity = before->body_identity,
        .character_id = 310,
        .object_binding = "RootBody",
        .animation_index = 0,
        .previous_progress = 0.0F,
        .current_progress = 1.0F,
        .body_animation_vector = {},
        .authored_offset = {10.0F, 20.0F, 30.0F},
        .first_tick = true,
        .execution_count = 0,
        .execution_limit = 1};
    const auto root_applied{runtime.select_body_animation(root_request)};
    REQUIRE(root_applied.has_value());
    CHECK_EQ(root_applied->max_frame_index, 3U);

    const App::Character::RuntimeCharacter* rooted{runtime.character_runtime().find(310)};
    REQUIRE(rooted != nullptr);
    CHECK_EQ(rooted->body_animation.final_anchor.x, doctest::Approx(3.93700778F));
    CHECK_EQ(rooted->body_animation.final_anchor.y, doctest::Approx(7.87401556F));
    CHECK_EQ(rooted->body_animation.final_anchor.z, doctest::Approx(11.8110233F));
    CHECK_EQ(rooted->body_animation.root_motion_delta.x, doctest::Approx(10.0F));
    CHECK_EQ(rooted->transform.translation.x, doctest::Approx(13.93700778F));
    CHECK_EQ(rooted->object_poses.at(0).channel_id, std::optional<std::uint32_t>{2});
    // This proves the 3DA channel binds by the 3DO script ID (3), not mesh ID 200.
    CHECK_EQ(rooted->object_poses.at(1).channel_id, std::optional<std::uint32_t>{3});
    CHECK(rooted->runtime_objects.at(0).animation_matrix.has_value());
    CHECK(rooted->runtime_objects.at(1).animation_matrix.has_value());
    CHECK_GT(rooted->pose_revision, initial_revision);

    const float character_x_before_child{rooted->transform.translation.x};
    App::Script::BodyAnimationRequest child_request{root_request};
    child_request.object_binding = "Child";
    child_request.authored_offset = {10.0F, 0.0F, 0.0F};
    const auto child_applied{runtime.select_body_animation(child_request)};
    REQUIRE(child_applied.has_value());

    const App::Character::RuntimeCharacter* child{runtime.character_runtime().find(310)};
    REQUIRE(child != nullptr);
    CHECK_EQ(child->body_animation.selected_object_index, std::size_t{1});
    CHECK_FALSE(child->object_poses.at(0).channel_index.has_value());
    CHECK_EQ(child->object_poses.at(1).channel_id, std::optional<std::uint32_t>{3});
    CHECK_FALSE(child->runtime_objects.at(0).animation_matrix.has_value());
    CHECK(child->runtime_objects.at(1).animation_matrix.has_value());
    // Ordinary anchoring always uses the 3DA reference (zero in this fixture),
    // even when the selected hierarchy object is parented.
    CHECK_EQ(child->body_animation.final_anchor.x, doctest::Approx(3.93700778F));
    CHECK_EQ(child->transform.translation.x + child->runtime_objects.at(1).world_translation.x,
        doctest::Approx(3.93700778F));
    CHECK_EQ(child->transform.translation.x, doctest::Approx(character_x_before_child));
    CHECK_EQ(child->body_animation.root_motion_delta.x, doctest::Approx(0.0F));
  }

  TEST_CASE("MoveObjectOnPath changes instance-local decor pose only") {
    BodyResourcesFixture resources{make_body_resources()};
    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(resources.scx, resources.bytes, "decor-path", nullptr, false)
            .has_value());

    App::Omikron::Model3DOData decor{make_movable_decor()};
    const auto before{App::Omikron::Model3DO::build_posed_geometry(decor, decor.runtime_objects)};
    REQUIRE(before.has_value());
    REQUIRE_EQ(before->size(), 1U);
    REQUIRE_EQ(before->front().vertices.size(), 3U);
    CHECK_EQ(before->front().vertices.front().position.at(0), doctest::Approx(0.0F));

    runtime.bind_decor_model(&decor);
    CHECK_EQ(runtime.decor_pose_revision(), 0U);
    const App::Script::MoveObjectOnPathRequest request{.object_binding = "Movable",
        .path_descriptor_index = 0U,
        .subpath_index = 0U,
        .interpolation_mode = 1U,
        .direction = 0U,
        .transform_rebase_mode = 0U,
        .duration_frames = 2.0F,
        .previous_parameter = 0.0F,
        .current_parameter = 1.0F};
    const auto applied{runtime.move_object_on_path(request)};
    REQUIRE(applied.has_value());
    CHECK_EQ(applied->max_parameter, 2U);
    CHECK_EQ(runtime.decor_pose_revision(), 1U);
    CHECK_EQ(decor.runtime_objects.front().local_offset.x, doctest::Approx(0.0F));

    const auto after{
        App::Omikron::Model3DO::build_posed_geometry(decor, runtime.decor_runtime_objects())};
    REQUIRE(after.has_value());
    REQUIRE_EQ(after->size(), 1U);
    CHECK_EQ(after->front().vertices.front().position.at(0), doctest::Approx(-478.393341F));
  }

  TEST_CASE("MoveObjectOnPath converts a sampled world pose below a rotated parent") {
    BodyResourcesFixture resources{make_body_resources()};
    append_world_pose_path(resources);
    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(resources.scx, resources.bytes, "parented-decor", nullptr, false)
            .has_value());

    App::Omikron::Model3DOData decor{make_parented_movable_decor()};
    REQUIRE(App::Omikron::Model3DO::resolve_runtime_transforms(decor, decor.runtime_objects)
            .has_value());
    const auto sibling_before{decor.runtime_objects.at(2)};
    const auto immutable_child{decor.meshes.at(1)};
    runtime.bind_decor_model(&decor);

    const App::Script::MoveObjectOnPathRequest request{.object_binding = "Movable",
        .path_descriptor_index = 1U,
        .subpath_index = 0U,
        .interpolation_mode = 1U,
        .direction = 0U,
        .transform_rebase_mode = 0U,
        .duration_frames = 1.0F,
        .previous_parameter = 0.0F,
        .current_parameter = 1.0F};
    REQUIRE(runtime.move_object_on_path(request).has_value());
    CHECK_EQ(runtime.decor_pose_revision(), 1U);

    const auto poses{runtime.decor_runtime_objects()};
    const auto& moved{poses[1]};
    CHECK_EQ(moved.world_translation.x, doctest::Approx(700.0F));
    CHECK_EQ(moved.world_translation.y, doctest::Approx(-120.0F));
    CHECK_EQ(moved.world_translation.z, doctest::Approx(325.0F));
    const App::Runtime::Matrix3 expected_world{
        App::Runtime::quaternion_matrix({.w = 0.70710677F, .z = 0.70710677F})};
    for (std::size_t index{0}; index < expected_world.values.size(); ++index) {
      CHECK_EQ(moved.world_matrix.values.at(index),
          doctest::Approx(expected_world.values.at(index)).epsilon(0.0001));
    }

    const auto& sibling_after{poses[2]};
    CHECK_EQ(
        sibling_after.world_translation.x, doctest::Approx(sibling_before.world_translation.x));
    CHECK_EQ(
        sibling_after.world_translation.y, doctest::Approx(sibling_before.world_translation.y));
    CHECK_EQ(
        sibling_after.world_translation.z, doctest::Approx(sibling_before.world_translation.z));
    CHECK_EQ(decor.meshes.at(1).name, immutable_child.name);
    CHECK_EQ(decor.meshes.at(1).parent_id, immutable_child.parent_id);
    CHECK_EQ(decor.meshes.at(1).mesh_id, immutable_child.mesh_id);
  }

  TEST_CASE("MoveObjectOnPath rebases a child world pose without cumulative drift") {
    BodyResourcesFixture resources{make_body_resources()};
    append_rebase_path(resources);
    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(resources.scx, resources.bytes, "rebased-child", nullptr, false)
            .has_value());

    App::Omikron::Model3DOData decor{make_parented_movable_decor()};
    REQUIRE(App::Omikron::Model3DO::resolve_runtime_transforms(decor, decor.runtime_objects)
            .has_value());
    const App::Runtime::Vec3 initial_world{decor.runtime_objects.at(1).world_translation};
    runtime.bind_decor_model(&decor);

    App::Script::MoveObjectOnPathRequest request{.object_binding = "Movable",
        .path_descriptor_index = 1U,
        .subpath_index = 0U,
        .interpolation_mode = 1U,
        .direction = 0U,
        .transform_rebase_mode = 1U,
        .duration_frames = 1.0F,
        .previous_parameter = 0.0F,
        .current_parameter = 0.0F,
        .capture_rebase_translation = true};
    const auto captured{runtime.move_object_on_path(request)};
    REQUIRE(captured.has_value());
    REQUIRE(captured->captured_rebase_translation.has_value());
    const std::array<float, 3> base{captured->captured_rebase_translation.value()};
    CHECK_EQ(base.at(0), doctest::Approx(initial_world.x));
    CHECK_EQ(base.at(1), doctest::Approx(initial_world.y));
    CHECK_EQ(base.at(2), doctest::Approx(initial_world.z));
    CHECK_EQ(
        runtime.decor_runtime_objects()[1].world_translation.x, doctest::Approx(initial_world.x));
    CHECK_EQ(
        runtime.decor_runtime_objects()[1].world_translation.y, doctest::Approx(initial_world.y));
    CHECK_EQ(
        runtime.decor_runtime_objects()[1].world_translation.z, doctest::Approx(initial_world.z));

    request.current_parameter = 1.0F;
    request.rebase_translation = base;
    request.capture_rebase_translation = false;
    REQUIRE(runtime.move_object_on_path(request).has_value());
    const auto moved{runtime.decor_runtime_objects()[1]};
    CHECK_EQ(moved.world_translation.x, doctest::Approx(initial_world.x + 2.0F));
    CHECK_EQ(moved.world_translation.y, doctest::Approx(initial_world.y + 4.0F));
    CHECK_EQ(moved.world_translation.z, doctest::Approx(initial_world.z + 6.0F));
    const App::Runtime::Matrix3 expected_world{
        App::Runtime::quaternion_matrix({.w = 0.70710677F, .z = 0.70710677F})};
    for (std::size_t index{0}; index < expected_world.values.size(); ++index) {
      CHECK_EQ(moved.world_matrix.values.at(index),
          doctest::Approx(expected_world.values.at(index)).epsilon(0.0001));
    }

    REQUIRE(runtime.move_object_on_path(request).has_value());
    const auto repeated{runtime.decor_runtime_objects()[1]};
    CHECK_EQ(repeated.world_translation.x, doctest::Approx(initial_world.x + 2.0F));
    CHECK_EQ(repeated.world_translation.y, doctest::Approx(initial_world.y + 4.0F));
    CHECK_EQ(repeated.world_translation.z, doctest::Approx(initial_world.z + 6.0F));
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)

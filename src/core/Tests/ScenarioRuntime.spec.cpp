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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Omikron/SFX.hpp"
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
  write_u32(
      data, App::Omikron::IamAreaRecord::k_offset_script, static_cast<std::uint32_t>(data.size()));
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

struct BodyResourcesFixture {
  App::Omikron::ScxData scx;
  std::vector<std::byte> bytes;
};

BodyResourcesFixture make_body_resources() {
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
  constexpr std::uint32_t root_rotation_offset{descriptor_end + (4U * 12U)};
  constexpr std::uint32_t child_rotation_offset{root_rotation_offset + (4U * 16U)};
  Buffer animation;
  animation.u32(3).u32(2);
  animation.u32(2)
      .chars("RootBody", 20)
      .u32(4)
      .u32(descriptor_end)
      .u32(4)
      .u32(root_rotation_offset);
  // mesh_id is 200, but the animation must bind this channel by script_id 3.
  animation.u32(3).chars("Child", 20).u32(4).u32(0).u32(4).u32(child_rotation_offset);
  for (std::uint32_t frame{0}; frame < 4U; ++frame) {
    animation.f32(static_cast<float>(frame) * 10.0F).f32(0.0F).f32(0.0F);
  }
  animation.f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
  animation.f32(0.0F).f32(0.0F).f32(0.0F).f32(1.0F);
  animation.f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
  animation.f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
  for (std::uint32_t frame{0}; frame < 4U; ++frame) {
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
    CHECK_FALSE(runtime.spawn_character_script_instance(0, 310, 0).has_value());
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

    REQUIRE(runtime.spawn_character_script_instance(0, 310, 0).has_value());
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

    auto missing{runtime.spawn_character_script_instance(1, 310, 0)};
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

    const auto created{runtime.spawn_character_script_instance(1, 310, -5)};
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

    const App::Script::RelativeBodyAnimationRequest request{.character_id = 310,
        .object_binding = "RootBody",
        .animation_index = 0,
        .previous_progress = 0.0F,
        .current_progress = 1.0F,
        .body_animation_vector = {4.0F, 5.0F, 6.0F},
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
    CHECK_EQ(animated->body_animation.body_animation_vector.z, doctest::Approx(6.0F));

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

  TEST_CASE("Body animation uses the selected object anchor without a 3DP resource") {
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
    const App::Character::RuntimeCharacter* before{runtime.character_runtime().find(310)};
    REQUIRE(before != nullptr);
    const std::uint64_t initial_revision{before->pose_revision};

    const App::Script::BodyAnimationRequest root_request{.character_id = 310,
        .object_binding = "RootBody",
        .animation_index = 0,
        .previous_progress = 0.0F,
        .current_progress = 1.0F,
        .body_animation_vector = {4.0F, 5.0F, 6.0F},
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
    CHECK_EQ(child->body_animation.final_anchor.x, doctest::Approx(5.93700778F));
    CHECK_EQ(child->runtime_objects.at(1).world_translation.x, doctest::Approx(5.93700778F));
    CHECK_EQ(child->transform.translation.x, doctest::Approx(character_x_before_child));
    CHECK_EQ(child->body_animation.root_motion_delta.x, doctest::Approx(0.0F));
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)

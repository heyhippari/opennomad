#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/GameState.hpp"
#include "Core/Omikron/CtlControlSet.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Scenario/ScenarioStartupController.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "Core/Script/ScriptRuntime.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"
#include "IamStartTestData.hpp"
#include "OmikronTestBuffer.hpp"

#include "ScenarioSessionFixtures.hpp"

TEST_SUITE("Core::Scenario::ScenarioStartupController") {
  TEST_CASE("AREA character residency is bound before the primary event can execute") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    StartedSession session{temp};
    App::ScenarioRuntime* const runtime{&session.world(0U)};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    REQUIRE(runtime != nullptr);
    REQUIRE_EQ(runtime->character_runtime().characters().size(), 2U);
    REQUIRE_EQ(controller.character_reference_entries().size(), 2U);
    for (const App::RuntimeCharacterReferenceEntry& entry :
        controller.character_reference_entries()) {
      CHECK(entry.source == App::CharacterReferenceSource::k_area);
      CHECK(entry.binding_state == App::CharacterPlacementBindingState::k_bound);
      REQUIRE(entry.body_identity.has_value());
      const App::Character::RuntimeCharacter* const body{
          runtime->character_runtime().find_body(entry.body_identity.value())};
      REQUIRE(body != nullptr);
      CHECK_FALSE(body->active);
      CHECK_FALSE(body->area_present);
    }
    CHECK_FALSE(controller.ticked());
  }

  TEST_CASE("new session requires a valid IAM/GLOBAL before compact execution") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    SUBCASE("missing GLOBAL") {
      std::filesystem::remove(temp.root() / "IAM" / "GLOBAL");
      App::ScenarioManager manager;
      App::ScenarioStartupController controller;
      const auto result{controller.initialize(manager)};
      REQUIRE_FALSE(result.has_value());
      CHECK(result.error().find("IAM/GLOBAL") != std::string::npos);
    }

    SUBCASE("malformed GLOBAL") {
      write_bytes(temp.root() / "IAM" / "GLOBAL", std::vector<std::byte>(0x1FU, std::byte{}));
      App::ScenarioManager manager;
      App::ScenarioStartupController controller;
      const auto result{controller.initialize(manager)};
      REQUIRE_FALSE(result.has_value());
      CHECK(result.error().find("IAM/GLOBAL: too small") != std::string::npos);
    }
  }

  TEST_CASE("compact cameras use resident slot order then GLOBAL regardless of active slot") {
    struct LookupCase {
      CameraNamespaceFixture fixture;
      std::int32_t expected_marker{0};
    };
    const std::array cases{LookupCase{.fixture = {.slot_0_area = true,
                                          .slot_0_scene = true,
                                          .slot_1_area = true,
                                          .slot_1_scene = true,
                                          .global = true},
                               .expected_marker = 100},
        LookupCase{.fixture = {.slot_0_scene = true,
                       .slot_1_area = true,
                       .slot_1_scene = true,
                       .global = true},
            .expected_marker = 200},
        LookupCase{.fixture = {.slot_1_area = true, .slot_1_scene = true, .global = true},
            .expected_marker = 300},
        LookupCase{.fixture = {.slot_1_scene = true, .global = true}, .expected_marker = 400},
        LookupCase{.fixture = {.global = true}, .expected_marker = 500}};

    for (const LookupCase& lookup : cases) {
      const TempDirectory temp;
      write_camera_namespace_fixtures(temp, lookup.fixture);
      StartedSession session{temp};
      App::ScenarioManager& manager{session.manager};
      App::ScenarioStartupController& controller{session.controller};
      REQUIRE(controller.tick().has_value());
      REQUIRE(controller.tick().has_value());
      CHECK_EQ(controller.active_area_slot(), 0U);
      REQUIRE_EQ(manager.world_presentation().pending_camera_count(), 1U);
      const auto command{manager.world_presentation().take_camera()};
      REQUIRE(command.has_value());
      CHECK_EQ(command->serialized_eye.at(0), lookup.expected_marker);
      CHECK_EQ(command->horizontal_fov_units, 800 + lookup.expected_marker);
    }
  }

  TEST_CASE("slot1 SCENE caller does not reorder definitions and retains owner metadata") {
    SUBCASE("slot0 AREA shadows the caller's slot1 SCENE") {
      const TempDirectory temp;
      write_camera_namespace_fixtures(temp,
          CameraNamespaceFixture{
              .slot_0_area = true, .slot_1_scene = true, .request_from_slot_1_scene = true});
      StartedSession session{temp};
      App::ScenarioManager& manager{session.manager};
      App::ScenarioStartupController& controller{session.controller};
      REQUIRE(controller.tick().has_value());
      REQUIRE(controller.tick().has_value());
      const auto command{manager.world_presentation().take_camera()};
      REQUIRE(command.has_value());
      CHECK_EQ(command->serialized_eye.at(0), 100);
      const App::RuntimeAreaSlot* owner{controller.runtime_area_slot(1)};
      REQUIRE(owner != nullptr);
      const auto contexts{manager.world_contexts()};
      const App::WorldSceneContext& context{contexts[owner->world_scene_id]};
      CHECK_EQ(command->scene_id, context.scene_id);
      CHECK_EQ(command->scene_generation, context.generation);
      CHECK_EQ(command->source_area_id, 222);
    }

    SUBCASE("GLOBAL supplies fields without becoming the owner world") {
      const TempDirectory temp;
      write_camera_namespace_fixtures(
          temp, CameraNamespaceFixture{.global = true, .request_from_slot_1_scene = true});
      StartedSession session{temp};
      App::ScenarioManager& manager{session.manager};
      App::ScenarioStartupController& controller{session.controller};
      REQUIRE(controller.tick().has_value());
      REQUIRE(controller.tick().has_value());
      const auto command{manager.world_presentation().take_camera()};
      REQUIRE(command.has_value());
      CHECK_EQ(command->serialized_eye.at(0), 500);
      const App::RuntimeAreaSlot* owner{controller.runtime_area_slot(1)};
      REQUIRE(owner != nullptr);
      const auto contexts{manager.world_contexts()};
      const App::WorldSceneContext& context{contexts[owner->world_scene_id]};
      CHECK_EQ(command->scene_id, context.scene_id);
      CHECK_EQ(command->scene_generation, context.generation);
      CHECK_EQ(command->source_area_id, 222);
    }
  }

  TEST_CASE("missing camera is a no-op without emitting a command or blocking") {
    const TempDirectory temp;
    write_camera_namespace_fixtures(temp, CameraNamespaceFixture{.tracked = true});
    StartedSession session{temp};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    REQUIRE(controller.tick().has_value());
    const auto result{controller.tick()};
    REQUIRE(result.has_value());
    CHECK_EQ(manager.world_presentation().pending_camera_count(), 0U);
  }

  TEST_CASE("compact camera captures the controlled body identity") {
    const TempDirectory temp;
    write_camera_namespace_fixtures(temp, CameraNamespaceFixture{.global = true});
    StartedSession session{temp};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* const runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);
    constexpr App::Character::BodyIdentity k_body_identity{4100U};
    REQUIRE(runtime->character_runtime()
            .adopt_character(App::Character::RuntimeCharacter{
                .body_identity = k_body_identity, .character_id = 41, .active = true})
            .has_value());
    manager.set_controlled_character(App::ControlledCharacterRef{
        .character_id = 41, .body_identity = k_body_identity, .world_scene_id = 0});

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick().has_value());
    const auto command{manager.world_presentation().take_camera()};
    REQUIRE(command.has_value());
    CHECK_EQ(command->attachment_participants.participant_a_body_identity,
        std::optional<App::Character::BodyIdentity>{k_body_identity});
    CHECK_EQ(command->attachment_participants.participant_a_character_id, 41);
  }

  TEST_CASE("tracked GLOBAL camera completion resumes the exact slot1 SCENE context") {
    const TempDirectory temp;
    write_camera_namespace_fixtures(temp,
        CameraNamespaceFixture{.global = true, .request_from_slot_1_scene = true, .tracked = true});
    StartedSession session{temp};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick().has_value());
    const auto command{manager.world_presentation().take_camera()};
    REQUIRE(command.has_value());
    REQUIRE(command->operation_generation.has_value());
    const App::RuntimeAreaSlot* owner{controller.runtime_area_slot(1)};
    REQUIRE(owner != nullptr);
    REQUIRE(owner->scene_script.has_value());
    CHECK(owner->scene_script->state() == AreaScriptState::k_waiting);
    CHECK_EQ(owner->scene_script->wait_state(), 7);
    manager.world_presentation().enqueue_camera_completion(App::WorldCameraOperationCompletion{
        .operation_generation = command->operation_generation.value(),
        .scene_id = command->scene_id,
        .scene_generation = command->scene_generation,
        .source_area_id = command->source_area_id,
        .camera_id = command->camera_id});
    REQUIRE(controller.tick().has_value());
    owner = controller.runtime_area_slot(1);
    REQUIRE(owner != nullptr);
    REQUIRE(owner->scene_script.has_value());
    CHECK(owner->scene_script->state() == AreaScriptState::k_ready);
  }

  TEST_CASE("IMPASSE 3795 suppresses control while waiting then restores and self-deactivates") {
    const TempDirectory temp;
    write_zone_contact_fixtures(temp);
    StartedSession session{temp, {.model_loader = make_stub_model_loader(), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(manager.game_state() != nullptr);
    CHECK(manager.game_state()->zone_flag(3795).value());
    CHECK_EQ(controller.zone_contact_count(), 1U);
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    REQUIRE(character->ctl_controller.has_value());
    App::Character::CtlController* const ctl_controller{&character->ctl_controller.value()};
    const App::Runtime::Vec3 tutorial_position{character->transform.translation};
    CHECK_EQ(character->current_move_id(), std::optional<std::int16_t>{100});
    CHECK_FALSE(character->controller_enabled);
    CHECK(ctl_controller->ordinary_control_suppressed());

    const App::ZoneContactContext* contact{controller.zone_contact(0)};
    REQUIRE(contact != nullptr);
    REQUIRE(contact->script != nullptr);
    CHECK(contact->script->state() == AreaScriptState::k_waiting);
    CHECK(contact->script->wait_info().kind == App::Script::AreaWaitKind::k_camera);

    // Remaining spatially inside while the tutorial waits retains its one
    // contact and does not manufacture another event context.
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 1U);
    CHECK_EQ(controller.active_zone_contact_registration_count(), 1U);
    CHECK_FALSE(character->controller_enabled);
    CHECK(ctl_controller->ordinary_control_suppressed());

    const auto camera{manager.world_presentation().take_camera()};
    REQUIRE(camera.has_value());
    REQUIRE(camera->operation_generation.has_value());
    manager.world_presentation().enqueue_camera_completion(App::WorldCameraOperationCompletion{
        .operation_generation = camera->operation_generation.value(),
        .scene_id = camera->scene_id,
        .scene_generation = camera->scene_generation,
        .source_area_id = camera->source_area_id,
        .camera_id = camera->camera_id});
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK(character->controller_enabled);
    CHECK_FALSE(ctl_controller->ordinary_control_suppressed());
    CHECK_EQ(&character->ctl_controller.value(), ctl_controller);
    CHECK_EQ(character->current_move_id(), std::optional<std::int16_t>{100});
    CHECK_EQ(character->transform.translation.x, tutorial_position.x);
    CHECK_EQ(character->transform.translation.z, tutorial_position.z);
    CHECK_FALSE(manager.game_state()->zone_flag(3795).value());

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 0U);

    // The disabled zone is not recreated while the actor remains inside it.
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 0U);
    CHECK(character->controller_enabled);
    CHECK_FALSE(ctl_controller->ordinary_control_suppressed());
  }

  TEST_CASE("registered proxy permits fresh contact while controller is disabled") {
    constexpr std::int16_t k_zone_id{12};
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, k_zone_id);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.bounds_radius = 50.0F, .actor_meshes = true}), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(manager.game_state() != nullptr);
    CHECK_FALSE(manager.game_state()->zone_flag(k_zone_id).value());
    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    REQUIRE(character->ctl_controller.has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    const App::CurrentCharacterTriggerProxy proxy{
        controller.current_character_trigger_proxy().value()};
    CHECK(proxy.registered);
    CHECK_EQ(proxy.owner.character_id, 136);
    CHECK_EQ(proxy.owner.world_scene_id, 0U);
    CHECK_EQ(proxy.radius, 42.5F);
    CHECK_EQ(proxy.actor_object_name, "ActorRoot");
    CHECK_EQ(character->model_resource->bounds_radius, 50.0F);
    CHECK_EQ(character->model_resource->model.root_mesh_index, 0);
    CHECK_EQ(character->model_resource->actor_object_index, std::optional<std::size_t>{0U});
    CHECK_EQ(controller.zone_contact_count(), 1U);
    CHECK_EQ(character->current_move_id(), std::optional<std::int16_t>{100});
    CHECK_FALSE(character->controller_enabled);
    CHECK(character->ctl_controller->ordinary_control_suppressed());
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x, proxy.position.x);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.y, proxy.position.y);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.z, proxy.position.z);
  }

  TEST_CASE("zone contact in progress survives control release before its wait") {
    constexpr std::int16_t k_zone_id{13};
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, true, k_zone_id, true);
    StartedSession session{temp, {.model_loader = make_stub_model_loader(), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    REQUIRE(character->ctl_controller.has_value());
    CHECK_EQ(character->current_move_id(), std::optional<std::int16_t>{100});
    CHECK(character->controller_enabled);
    CHECK_FALSE(character->ctl_controller->ordinary_control_suppressed());
    CHECK_EQ(controller.zone_contact_count(), 1U);
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK(controller.current_character_trigger_proxy()->registered);

    const App::ZoneContactContext* contact{controller.zone_contact(0)};
    REQUIRE(contact != nullptr);
    REQUIRE(contact->script != nullptr);
    CHECK(contact->script->state() == AreaScriptState::k_waiting);
    CHECK(contact->script->wait_info().kind == App::Script::AreaWaitKind::k_camera);
  }

  TEST_CASE("compact address placement updates the trigger proxy through ordinary service") {
    constexpr std::int16_t k_zone_id{14};
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, k_zone_id, false, true);
    StartedSession session{temp, {.model_loader = make_stub_model_loader(
                                       {.collision_spheres = {App::Omikron::CollisionSphere{.center = {.y = 5.0F}, .radius = 10.0F},
                                           App::Omikron::CollisionSphere{.center = {.y = 20.0F}, .radius = 7.0F}}}), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    const App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    const App::Runtime::Vec3 address_position{
        App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{500, 999, 500})};
    CHECK_EQ(character->transform.translation.x, doctest::Approx(address_position.x));
    CHECK_EQ(character->transform.translation.y, doctest::Approx(address_position.y - 27.0F));
    CHECK_EQ(character->transform.translation.z, doctest::Approx(address_position.z));
    CHECK_EQ(character->current_move_id(), std::optional<std::int16_t>{100});
    CHECK(character->controller_enabled);
    REQUIRE(character->ctl_controller.has_value());
    CHECK_FALSE(character->ctl_controller->ordinary_control_suppressed());
    CHECK_EQ(character->ordinary_actor_service_generation, 0U);
    CHECK_EQ(character->physical_motion.candidate_translation.y,
        doctest::Approx(character->transform.translation.y));
    CHECK_EQ(character->physical_motion.accepted_translation.y,
        doctest::Approx(character->transform.translation.y));
    CHECK_EQ(character->physical_motion.vertical_velocity, doctest::Approx(0.0F));
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK_EQ(character->serialized_area_position.at(0), 500);
    CHECK_EQ(character->serialized_area_position.at(1), 999);
    CHECK_NE(controller.current_character_trigger_proxy()->position.x,
        character->transform.translation.x);
    CHECK(controller.current_character_trigger_proxy()->synchronization_suspended);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, 1U);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x,
        character->transform.translation.x);
    CHECK_FALSE(controller.current_character_trigger_proxy()->synchronization_suspended);
    CHECK_EQ(controller.zone_contact_count(), 0U);
  }

  TEST_CASE(
      "structured ownership resolves through the live runtime body and ordinary service "
      "generation") {
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, 18, false, false, 50U, 4090, 0, true);
    StartedSession session{temp, {.model_loader = make_stub_model_loader(), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, 0U);
    CHECK_EQ(
        controller.current_character_trigger_proxy()->last_consumed_actor_spatial_generation, 0U);

    const auto instance_id{runtime->script_runtime()->instances().front().instance_id};
    REQUIRE(instance_id > 0U);
    const auto& launch_context{runtime->script_runtime()->instances().front().launch_context};
    CHECK_EQ(launch_context.character_id, std::optional<std::int16_t>{136});
    CHECK_EQ(launch_context.character_body_identity,
        std::optional<std::uint64_t>{character->body_identity});

    character->transform.translation.x += 100.0F;
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, 0U);
    CHECK_EQ(
        controller.current_character_trigger_proxy()->last_consumed_actor_spatial_generation, 0U);
    CHECK(controller.current_character_trigger_proxy()->synchronization_suspended);
  }

  TEST_CASE("exact structured-child completion defers ordinary service until a later dispatch") {
    const TempDirectory temp;
    write_structured_child_completion_fixtures(temp);
    StartedSession session{temp, {.model_loader = [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          auto resource{std::make_shared<App::Character::ModelResource>()};
          resource->name = name;
          resource->bounds_radius = 50.0F;
          resource->model.root_mesh_index = 0;
          resource->model.meshes.resize(1U);
          resource->model.meshes.at(0).name = "ActorRoot";
          resource->model.meshes.at(0).triangle_count = 26U;
          resource->model.meshes.at(0).bounding_radius = 42.5F;
          resource->model.hierarchy_parent_index = {-1};
          resource->model.hierarchy_first_child_index = {-1};
          resource->model.hierarchy_next_sibling_index = {-1};
          resource->actor_object_index = App::Character::actor_object_index(resource->model);
          resource->groups.push_back(App::Omikron::MaterialGroup{});
          return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
        }, .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    INFO(controller.last_error());
    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    const App::Runtime::Vec3 frozen_position{
        controller.current_character_trigger_proxy()->position};
    App::Character::PhysicalMotionService::synchronize(*character);
    character->physical_motion.vertical_velocity = 50.0F;
    character->physical_motion.fall_stage = 3;
    character->physical_motion.accumulated_fall_travel = 120.0F;
    character->physical_motion.maximum_support_gap = 140.0F;
    const App::Runtime::Vec3 stale_candidate{character->physical_motion.candidate_translation};
    const auto* instance{runtime->script_runtime()->instance(
        runtime->script_runtime()->instances().front().instance_id)};
    REQUIRE(instance != nullptr);
    CHECK_FALSE(instance->completed);

    runtime->script_runtime()->tick(1.0F / 30.0F);
    CHECK(instance->completed);
    character->transform.translation.x += 100.0F;
    const App::Runtime::Vec3 structured_position{character->transform.translation};

    const auto tick_result{controller.tick(1.0F / 30.0F)};
    INFO(controller.last_error());
    REQUIRE(tick_result.has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, 0U);
    CHECK_EQ(
        controller.current_character_trigger_proxy()->last_consumed_actor_spatial_generation, 0U);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x, frozen_position.x);
    CHECK_EQ(character->physical_motion.candidate_translation.x, stale_candidate.x);
    CHECK_EQ(character->physical_motion.vertical_velocity, 50.0F);
    CHECK_EQ(character->physical_motion.fall_stage, 3U);
    CHECK_EQ(character->physical_motion.accumulated_fall_travel, 120.0F);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, 1U);
    CHECK_EQ(
        controller.current_character_trigger_proxy()->last_consumed_actor_spatial_generation, 1U);
    CHECK(controller.current_character_trigger_proxy()->contact_ready);
    CHECK_EQ(character->physical_motion.candidate_translation.x, structured_position.x);
    CHECK_EQ(character->physical_motion.accepted_translation.x, structured_position.x);
    CHECK_EQ(character->transform.translation.x, structured_position.x);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x, structured_position.x);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.y, structured_position.y);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.z, structured_position.z);
    CHECK_EQ(controller.current_character_trigger_proxy()->radius, 42.5F);
    CHECK_EQ(controller.current_character_trigger_proxy()->actor_object_name, "ActorRoot");
    CHECK(character->physical_motion.vertical_velocity ==
          doctest::Approx(
              App::Character::PhysicalMotionService::K_DEFAULT_GRAVITY_VELOCITY_DELTA_PER_TICK));
    CHECK_EQ(character->physical_motion.fall_stage, 0U);
    CHECK_EQ(character->physical_motion.accumulated_fall_travel, 0.0F);
    CHECK_EQ(character->physical_motion.maximum_support_gap, 0.0F);
  }

  TEST_CASE("chained structured children never expose an ordinary actor service gap") {
    const TempDirectory temp;
    write_chained_structured_child_fixtures(temp);
    StartedSession session{temp, {.model_loader = make_stub_model_loader()}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.begin_tick(1.0F / 30.0F).has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    const App::Runtime::Vec3 frozen_position{
        controller.current_character_trigger_proxy()->position};
    runtime->tick(1.0F / 30.0F);
    REQUIRE(controller.finish_tick(1.0F / 30.0F).has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, 0U);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x, frozen_position.x);

    REQUIRE(controller.begin_tick(1.0F / 30.0F).has_value());
    REQUIRE_EQ(runtime->script_runtime()->instances().size(), 2U);
    runtime->tick(1.0F / 30.0F);
    REQUIRE(controller.finish_tick(1.0F / 30.0F).has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, 0U);
    CHECK_EQ(
        controller.current_character_trigger_proxy()->last_consumed_actor_spatial_generation, 0U);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, 1U);
    CHECK_EQ(
        controller.current_character_trigger_proxy()->last_consumed_actor_spatial_generation, 1U);
  }

  TEST_CASE("session reset clears structured owner proxy references and pending actor phase") {
    const TempDirectory temp;
    write_structured_child_completion_fixtures(temp);
    StartedSession session{temp, {.model_loader = make_stub_model_loader()}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.begin_tick(1.0F / 30.0F).has_value());
    CHECK(controller.structured_character_owner_active());
    CHECK(controller.actor_phase_pending());
    CHECK(controller.current_character_trigger_proxy().has_value());
    CHECK_GT(controller.character_reference_entry_count(), 0U);

    controller.reset_session();
    CHECK_FALSE(controller.structured_character_owner_active());
    CHECK_FALSE(controller.actor_phase_pending());
    CHECK_FALSE(controller.current_character_trigger_proxy().has_value());
    CHECK_EQ(controller.character_reference_entry_count(), 0U);
  }

  TEST_CASE("structured owner follows BodyIdentity transfer and expires with script world") {
    const TempDirectory temp;
    write_structured_child_completion_fixtures(temp);
    write_bytes(temp.root() / "SCPTDATA" / "B.SCX", make_minimal_scx());
    write_bytes(temp.root() / "SCPTDATA" / "C.SCX", make_minimal_scx());
    StartedSession session{temp, {.model_loader = make_stub_model_loader()}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    REQUIRE(manager.load_world_context(2U, std::nullopt, "SCPTDATA/B.SCX").has_value());
    App::ScenarioRuntime* source{manager.world_runtime(0U)};
    App::ScenarioRuntime* destination{manager.world_runtime(2U)};
    REQUIRE(source != nullptr);
    REQUIRE(destination != nullptr);

    REQUIRE(controller.begin_tick(1.0F / 30.0F).has_value());
    REQUIRE(manager.controlled_character().has_value());
    const App::Character::BodyIdentity body_identity{manager.controlled_character()->body_identity};
    REQUIRE(manager.transfer_controlled_character(0U, 2U).has_value());
    CHECK(controller.structured_character_owner_active());
    REQUIRE(controller.finish_tick(1.0F / 30.0F).has_value());
    App::Character::RuntimeCharacter* body{
        destination->character_runtime().find_body(body_identity)};
    REQUIRE(body != nullptr);
    CHECK_EQ(body->ordinary_actor_service_generation, 0U);
    CHECK(controller.structured_character_owner_active());

    REQUIRE(controller.begin_tick(1.0F / 30.0F).has_value());
    REQUIRE(manager.activate_world_context(2U).has_value());
    REQUIRE(manager.set_current_world_context(2U).has_value());
    REQUIRE(manager.deactivate_world_context(0U).has_value());
    REQUIRE(manager.unload_world_context(0U).has_value());
    REQUIRE(manager.load_world_context(3U, std::nullopt, "SCPTDATA/C.SCX").has_value());
    REQUIRE(controller.finish_tick(1.0F / 30.0F).has_value());
    CHECK_FALSE(controller.structured_character_owner_active());
    CHECK_EQ(body->ordinary_actor_service_generation, 1U);
  }

  TEST_CASE("0x38 possession cycles preserve exact bodies and mutable state") {
    const TempDirectory temp;
    write_possession_cycle_fixtures(temp);
    std::size_t model_loads{0};
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.load_counter = &model_loads})}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{&session.world(0U)};

    REQUIRE(controller.begin_tick(1.0F / 30.0F).has_value());
    App::Character::RuntimeCharacter* body_a{runtime->character_runtime().find(310)};
    REQUIRE(body_a != nullptr);
    const App::Character::BodyIdentity identity_a{body_a->body_identity};
    body_a->transform.translation.x = 111.0F;
    body_a->pose_revision = 21U;
    const auto resource_a{body_a->model_resource};
    runtime->tick(1.0F / 30.0F);
    REQUIRE(controller.finish_tick(1.0F / 30.0F).has_value());

    REQUIRE(controller.begin_tick(1.0F / 30.0F).has_value());
    App::Character::RuntimeCharacter* body_b{runtime->character_runtime().find(136)};
    REQUIRE(body_b != nullptr);
    const App::Character::BodyIdentity identity_b{body_b->body_identity};
    body_b->transform.translation.x = 222.0F;
    body_b->pose_revision = 31U;
    const auto resource_b{body_b->model_resource};
    REQUIRE(manager.controlled_character().has_value());
    CHECK_EQ(manager.controlled_character()->body_identity, identity_b);
    runtime->tick(1.0F / 30.0F);
    REQUIRE(controller.finish_tick(1.0F / 30.0F).has_value());

    const auto reverse_selection{controller.tick(1.0F / 30.0F)};
    INFO(controller.last_error());
    REQUIRE(reverse_selection.has_value());
    REQUIRE(manager.controlled_character().has_value());
    CHECK_EQ(manager.controlled_character()->body_identity, identity_a);
    REQUIRE_EQ(runtime->character_runtime().characters().size(), 2U);
    body_a = runtime->character_runtime().find_body(identity_a);
    body_b = runtime->character_runtime().find_body(identity_b);
    REQUIRE(body_a != nullptr);
    REQUIRE(body_b != nullptr);
    CHECK_EQ(body_a->transform.translation.x, 111.0F);
    CHECK_EQ(body_b->transform.translation.x, 222.0F);
    CHECK_EQ(body_a->pose_revision, 21U);
    CHECK_EQ(body_b->pose_revision, 31U);
    CHECK(body_a->model_resource == resource_a);
    CHECK(body_b->model_resource == resource_b);
    CHECK_NE(identity_a, identity_b);
    CHECK_EQ(model_loads, 1U);
  }

  TEST_CASE("activation and deactivation aliases operate on resolved BodyIdentity") {
    const TempDirectory temp;
    write_activation_alias_fixtures(temp);
    StartedSession session{temp, {.model_loader = make_stub_model_loader()}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);
    REQUIRE(manager.load_world_context(2U, std::nullopt, "SCPTDATA/B.SCX").has_value());
    App::ScenarioRuntime* const other_runtime{manager.world_runtime(2U)};
    REQUIRE(other_runtime != nullptr);

    REQUIRE(controller.begin_tick(1.0F / 30.0F).has_value());
    App::Character::RuntimeCharacter* aliased_body{runtime->character_runtime().find(310)};
    REQUIRE(aliased_body != nullptr);
    const App::Character::BodyIdentity body_identity{aliased_body->body_identity};
    runtime->tick(1.0F / 30.0F);
    REQUIRE(controller.finish_tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.begin_tick(1.0F / 30.0F).has_value());
    runtime->tick(1.0F / 30.0F);
    REQUIRE(controller.finish_tick(1.0F / 30.0F).has_value());

    aliased_body = runtime->character_runtime().find_body(body_identity);
    REQUIRE(aliased_body != nullptr);
    aliased_body->character_id = 999;
    REQUIRE(runtime->character_runtime()
            .transfer_body_to(other_runtime->character_runtime(), body_identity)
            .has_value());
    aliased_body = other_runtime->character_runtime().find_body(body_identity);
    REQUIRE(aliased_body != nullptr);
    const std::shared_ptr<const App::Character::ModelResource> model_resource{
        aliased_body->model_resource};
    const std::size_t resident_body_count{runtime->character_runtime().characters().size() +
                                          other_runtime->character_runtime().characters().size()};
    REQUIRE(controller.begin_tick(1.0F / 30.0F).has_value());
    CHECK(runtime->character_runtime().find(310) == nullptr);
    CHECK_FALSE(aliased_body->active);
    CHECK_FALSE(aliased_body->area_present);
    REQUIRE(manager.game_state() != nullptr);
    CHECK_FALSE(manager.game_state()->character_flag(468).value());
    runtime->tick(1.0F / 30.0F);
    REQUIRE(controller.finish_tick(1.0F / 30.0F).has_value());

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK(aliased_body->active);
    CHECK(aliased_body->area_present);
    CHECK_EQ(aliased_body->body_identity, body_identity);
    CHECK(aliased_body->model_resource == model_resource);
    CHECK_EQ(runtime->character_runtime().characters().size() +
                 other_runtime->character_runtime().characters().size(),
        resident_body_count);
    // After the 0x38 310 -> 0x38 136 possession cycle table-0 placement 1 (state bit 469) holds the
    // 310 reference and its body, so 0x4E 310 persists placement 1's bit. Placement 0 (bit 468) was
    // left explicitly unbound by the first selection.
    CHECK(manager.game_state()->character_flag(469).value());
    CHECK_FALSE(manager.game_state()->character_flag(468).value());
  }

  TEST_CASE("activation does not rematerialize an explicitly-unbound placement") {
    const TempDirectory temp;
    write_explicitly_unbound_activation_fixtures(temp);
    StartedSession session{temp, {.model_loader = make_stub_model_loader()}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE_EQ(runtime->character_runtime().characters().size(), 2U);
    const App::Character::RuntimeCharacter* const original_body{
        runtime->character_runtime().find(310)};
    REQUIRE(original_body != nullptr);
    const App::Character::BodyIdentity original_identity{original_body->body_identity};
    const auto result{controller.tick()};
    REQUIRE(result.has_value());
    REQUIRE_EQ(runtime->character_runtime().characters().size(), 2U);
    REQUIRE(runtime->character_runtime().find_body(original_identity) != nullptr);
  }

  TEST_CASE("0x3B starts an explicit structured script on an inactive resident body") {
    const TempDirectory temp;
    write_inactive_character_script_fixtures(temp, false);
    StartedSession session{temp};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* const runtime{manager.world_runtime(0U)};
    REQUIRE(runtime != nullptr);
    const App::Character::RuntimeCharacter* const body{runtime->character_runtime().find(310)};
    REQUIRE(body != nullptr);
    const App::Character::BodyIdentity body_identity{body->body_identity};
    CHECK_FALSE(body->active);
    CHECK_FALSE(body->area_present);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.area_script() != nullptr);
    CHECK_EQ(controller.area_script()->instruction_pointer(), 0x404U);
    CHECK(controller.area_script()->state() != App::Script::AreaScriptState::k_waiting);
    CHECK(controller.area_script()->state() != App::Script::AreaScriptState::k_failed);
    REQUIRE_EQ(runtime->script_runtime()->instances().size(), 1U);
    const App::Script::ScriptInstance& instance{runtime->script_runtime()->instances().front()};
    CHECK_EQ(instance.launch_context.character_id, std::optional<std::int16_t>{310});
    CHECK_EQ(instance.launch_context.character_body_identity,
        std::optional<App::Character::BodyIdentity>{body_identity});
    CHECK_FALSE(body->active);
    CHECK_FALSE(body->area_present);
  }

  TEST_CASE("0x3C tracks an explicit structured script on an inactive resident body") {
    const TempDirectory temp;
    write_inactive_character_script_fixtures(temp, true);
    StartedSession session{temp};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* const runtime{manager.world_runtime(0U)};
    REQUIRE(runtime != nullptr);
    const App::Character::RuntimeCharacter* const body{runtime->character_runtime().find(310)};
    REQUIRE(body != nullptr);
    const App::Character::BodyIdentity body_identity{body->body_identity};

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.area_script() != nullptr);
    CHECK(controller.area_script()->state() == App::Script::AreaScriptState::k_waiting);
    CHECK_EQ(controller.area_script()->wait_state(), 4U);
    REQUIRE_EQ(runtime->script_runtime()->instances().size(), 1U);
    const App::Script::ScriptInstance& instance{runtime->script_runtime()->instances().front()};
    CHECK_EQ(controller.area_script()->wait_info().character_script_instance,
        std::optional<std::size_t>{instance.instance_id});
    CHECK_EQ(instance.launch_context.character_body_identity,
        std::optional<App::Character::BodyIdentity>{body_identity});
    CHECK_FALSE(body->active);
    CHECK_FALSE(body->area_present);

    runtime->tick(1.0F);
    REQUIRE(controller.tick().has_value());
    CHECK_EQ(controller.area_script()->instruction_pointer(), 0x404U);
    CHECK(controller.area_script()->state() != App::Script::AreaScriptState::k_waiting);
    CHECK(controller.area_script()->state() != App::Script::AreaScriptState::k_failed);
  }

  TEST_CASE("0x4E reuses the resident body and conditionally restores authored placement") {
    const auto verify_activation = [](const bool apply_area_transform) {
      const TempDirectory temp;
      write_character_activation_fixtures(temp, apply_area_transform);
      StartedSession session{temp};
      App::ScenarioManager& manager{session.manager};
      App::ScenarioStartupController& controller{session.controller};
      App::ScenarioRuntime* const runtime{manager.world_runtime(0U)};
      REQUIRE(runtime != nullptr);
      App::Character::RuntimeCharacter* const body{runtime->character_runtime().find(310)};
      REQUIRE(body != nullptr);
      const App::Character::BodyIdentity body_identity{body->body_identity};
      const std::shared_ptr<const App::Character::ModelResource> model_resource{
          body->model_resource};
      const std::size_t body_count{runtime->character_runtime().characters().size()};
      body->transform.translation = {.x = 11.0F, .y = 22.0F, .z = 33.0F};

      REQUIRE(controller.tick().has_value());
      CHECK_EQ(runtime->character_runtime().characters().size(), body_count);
      CHECK_EQ(body->body_identity, body_identity);
      CHECK(body->model_resource == model_resource);
      CHECK(body->active);
      CHECK(body->area_present);
      if (apply_area_transform) {
        CHECK_EQ(body->transform.translation.x,
            static_cast<float>(App::Runtime::area_position_to_inches(-2588)));
        CHECK_EQ(body->transform.translation.y,
            static_cast<float>(App::Runtime::area_position_to_inches(-271)));
        CHECK_EQ(body->transform.translation.z,
            static_cast<float>(App::Runtime::area_position_to_inches(-816)));
      } else {
        CHECK_EQ(body->transform.translation.x, 11.0F);
        CHECK_EQ(body->transform.translation.y, 22.0F);
        CHECK_EQ(body->transform.translation.z, 33.0F);
      }
      REQUIRE(manager.game_state() != nullptr);
      CHECK(manager.game_state()->character_flag(468).value());
    };

    verify_activation(false);
    verify_activation(true);
  }

  TEST_CASE("ordinary actor service advances generation for each due physical tick") {
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, 19, false, false);
    // Inline: the trace recorder must be attached before initialize() to capture startup events.
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    App::ScenarioStartupController controller;
    App::Startup::StartupTraceRecorder recorder;
    controller.set_trace_recorder(&recorder);
    REQUIRE(controller.initialize(manager).has_value());
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);
    install_resident_model_loader(runtime->character_runtime(),
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          auto resource{std::make_shared<App::Character::ModelResource>()};
          resource->name = name;
          resource->groups.push_back(App::Omikron::MaterialGroup{});
          return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
        });
    install_stub_ctl_loader(*runtime);

    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, 0U);
    CHECK_EQ(
        controller.current_character_trigger_proxy()->last_consumed_actor_spatial_generation, 0U);

    character->transform.translation.x += 200.0F;
    REQUIRE(controller.tick(1.0F / 60.0F).has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, 0U);
    CHECK_EQ(std::ranges::count_if(recorder.events(),
                 [](const auto& event) {
                   return event.name == "CurrentActor.OrdinarySpatialSample";
                 }),
        0);
    REQUIRE(controller.tick(1.0F / 60.0F).has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, 1U);
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK_EQ(controller.current_character_trigger_proxy()->generation,
        character->ordinary_actor_service_generation);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x,
        character->transform.translation.x);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.y,
        character->transform.translation.y);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.z,
        character->transform.translation.z);
    CHECK_EQ(controller.current_character_trigger_proxy()->heading_degrees,
        character->principal_orientation_degrees.y);
    CHECK_EQ(character->physical_motion.accepted_translation.x, character->transform.translation.x);
    CHECK(character->physical_motion.vertical_velocity ==
          doctest::Approx(
              App::Character::PhysicalMotionService::K_DEFAULT_GRAVITY_VELOCITY_DELTA_PER_TICK));

    character->controller_enabled = false;
    REQUIRE(character->ctl_controller.has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, 2U);
    CHECK(character->physical_motion.vertical_velocity ==
          doctest::Approx(
              2.0F *
              App::Character::PhysicalMotionService::K_DEFAULT_GRAVITY_VELOCITY_DELTA_PER_TICK));

    character->ctl_controller.reset();
    REQUIRE(controller.tick(2.0F / 30.0F).has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, 4U);
    CHECK(character->physical_motion.vertical_velocity ==
          doctest::Approx(
              4.0F *
              App::Character::PhysicalMotionService::K_DEFAULT_GRAVITY_VELOCITY_DELTA_PER_TICK));
    CHECK(character->physical_motion.initialized);
    CHECK_EQ(
        controller.current_character_trigger_proxy()->last_consumed_actor_spatial_generation, 4U);

    std::vector<std::string_view> spatial_samples;
    for (const App::Startup::StartupTraceEvent& event : recorder.events()) {
      if (event.name == "CurrentActor.OrdinarySpatialSample") {
        spatial_samples.emplace_back(event.detail);
      }
    }
    REQUIRE_EQ(spatial_samples.size(), 4U);
    CHECK_EQ(spatial_samples.at(0).find("generation=1 "), 0U);
    CHECK_EQ(spatial_samples.at(1).find("generation=2 "), 0U);
    CHECK_EQ(spatial_samples.at(2).find("generation=3 "), 0U);
    CHECK_EQ(spatial_samples.at(3).find("generation=4 "), 0U);
  }

  TEST_CASE("ordinary proxy synchronization may create contact while controller is disabled") {
    constexpr std::int16_t k_zone_id{15};
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, k_zone_id, true, false, 500U);
    StartedSession session{temp, {.model_loader = make_stub_model_loader(), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    CHECK_FALSE(character->spatial_heading_suppression_latch);
    CHECK_FALSE(character->controller_enabled);
    CHECK_EQ(controller.zone_contact_count(), 0U);
    character->transform.translation =
        App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{50, 999, 50});
    character->spatial_heading_suppression_latch = true;

    const std::uint64_t generation_before{character->ordinary_actor_service_generation};
    const App::Runtime::Vec3 proxy_position_before{
        controller.current_character_trigger_proxy()->position};
    REQUIRE(controller.tick(1.0F / 60.0F).has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, generation_before);
    CHECK_EQ(controller.current_character_trigger_proxy()->generation, generation_before);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x, proxy_position_before.x);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.y, proxy_position_before.y);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.z, proxy_position_before.z);
    CHECK_EQ(controller.zone_contact_count(), 0U);
    CHECK(character->spatial_heading_suppression_latch);

    REQUIRE(controller.tick(1.0F / 60.0F).has_value());
    CHECK_EQ(character->ordinary_actor_service_generation, generation_before + 1U);
    CHECK_EQ(controller.current_character_trigger_proxy()->generation,
        character->ordinary_actor_service_generation);
    CHECK_EQ(controller.zone_contact_count(), 1U);
    CHECK(character->spatial_heading_suppression_latch);
    const App::ZoneContactContext* contact{controller.zone_contact(0)};
    REQUIRE(contact != nullptr);
    REQUIRE(contact->script != nullptr);
    CHECK_FALSE(contact->script->active_event().has_value());
    CHECK(contact->script->queued_events().empty());
    CHECK(contact->heartbeat_state == App::ZoneContactHeartbeatState::k_entry_pending);
    CHECK(contact->spatially_registered());
    CHECK_EQ(controller.active_zone_contact_registration_count(), 1U);
    CHECK(controller.zone_contact_lifecycle_pass_pending());
    CHECK_EQ(character->current_move_id(), std::optional<std::int16_t>{7});
    CHECK_FALSE(character->controller_enabled);
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK(character->controller_enabled);
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    contact = controller.zone_contact(0);
    REQUIRE(contact != nullptr);
    CHECK(contact->heartbeat_state == App::ZoneContactHeartbeatState::k_refreshed);
    CHECK(std::ranges::none_of(contact->script->queued_events(), [](const std::uint16_t event) {
      return event == 2U;
    }));
    CHECK_EQ(character->current_move_id(), std::optional<std::int16_t>{100});
    CHECK(character->controller_enabled);
    CHECK(character->spatial_heading_suppression_latch);
    CHECK_EQ(controller.active_zone_contact_registration_count(), 1U);
  }

  TEST_CASE(
      "zone heartbeat ages only after ordinary service and permits re-entry during departure") {
    constexpr std::int16_t k_zone_id{24};
    const TempDirectory temp;
    write_zone_contact_fixtures(
        temp, false, k_zone_id, false, false, 500U, 4090, 0, false, false, false);
    StartedSession session{temp, {.model_loader = make_stub_model_loader(), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    character->transform.translation =
        App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{50, 999, 50});
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE_EQ(controller.zone_contact_count(), 1U);
    CHECK(controller.zone_contact(0)->heartbeat_state ==
          App::ZoneContactHeartbeatState::k_entry_pending);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.zone_contact(0) != nullptr);
    CHECK(
        controller.zone_contact(0)->heartbeat_state == App::ZoneContactHeartbeatState::k_refreshed);
    character->transform.translation.x = 1000.0F;

    REQUIRE(controller.tick(1.0F / 60.0F).has_value());
    CHECK(controller.zone_contact(0)->heartbeat_state ==
          App::ZoneContactHeartbeatState::k_awaiting_refresh);
    CHECK_FALSE(controller.zone_contact_lifecycle_pass_pending());
    REQUIRE(controller.tick(1.0F / 60.0F).has_value());
    CHECK(controller.zone_contact(0)->heartbeat_state ==
          App::ZoneContactHeartbeatState::k_awaiting_refresh);
    CHECK(controller.zone_contact_lifecycle_pass_pending());
    CHECK_EQ(controller.active_zone_contact_registration_count(), 1U);

    REQUIRE(controller.tick().has_value());
    CHECK(controller.zone_contact(0)->heartbeat_state ==
          App::ZoneContactHeartbeatState::k_departure_pending);
    CHECK_EQ(controller.active_zone_contact_registration_count(), 0U);
    character->transform.translation =
        App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{50, 999, 50});
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE_EQ(controller.zone_contact_count(), 2U);
    CHECK_EQ(controller.active_zone_contact_registration_count(), 1U);
    REQUIRE(controller.zone_contact(1) != nullptr);
    CHECK(controller.zone_contact(1)->heartbeat_state ==
          App::ZoneContactHeartbeatState::k_entry_pending);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    character->transform.translation.x = 1000.0F;
    REQUIRE(controller.tick(2.0F / 30.0F).has_value());
    CHECK(controller.zone_contact(1)->heartbeat_state ==
          App::ZoneContactHeartbeatState::k_awaiting_refresh);
    CHECK_EQ(controller.active_zone_contact_registration_count(), 1U);
    CHECK(controller.zone_contact_lifecycle_pass_pending());
    REQUIRE(controller.begin_tick(0.0F).has_value());
    CHECK(controller.zone_contact(1)->heartbeat_state ==
          App::ZoneContactHeartbeatState::k_departure_pending);
    CHECK_EQ(controller.active_zone_contact_registration_count(), 0U);
  }

  TEST_CASE("structured owner phase schedules missed-heartbeat aging without stale publication") {
    constexpr std::int16_t k_zone_id{25};
    const TempDirectory temp;
    write_zone_contact_fixtures(
        temp, false, k_zone_id, false, false, 50U, 4090, 0, false, true, false);
    StartedSession session{temp, {.model_loader = make_stub_model_loader(), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE_EQ(controller.zone_contact_count(), 1U);
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    CHECK(character->spatial_heading_suppression_latch);
    const std::uint64_t generation{controller.current_character_trigger_proxy()->generation};
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK(controller.structured_character_owner_active());
    CHECK_EQ(controller.current_character_trigger_proxy()->generation, generation);
    CHECK(controller.zone_contact_lifecycle_pass_pending());
    CHECK(controller.zone_contact(0)->heartbeat_state ==
          App::ZoneContactHeartbeatState::k_awaiting_refresh);

    REQUIRE(controller.begin_tick(1.0F / 30.0F).has_value());
    CHECK(controller.zone_contact(0)->heartbeat_state ==
          App::ZoneContactHeartbeatState::k_departure_pending);
    CHECK_EQ(controller.active_zone_contact_registration_count(), 0U);
    CHECK(character->spatial_heading_suppression_latch);
  }

  TEST_CASE("zone capacity counts registrations instead of retained departure contexts") {
    constexpr std::int16_t k_first_zone_id{100};
    const TempDirectory temp;
    write_zone_contact_fixtures(
        temp, false, k_first_zone_id, false, false, 50U, 4090, 0, false, false, false, 17U);
    StartedSession session{temp, {.model_loader = make_stub_model_loader(), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 16U);
    CHECK_EQ(controller.active_zone_contact_registration_count(), 16U);

    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    character->transform.translation.x = 1000.0F;
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.tick().has_value());
    CHECK_EQ(controller.zone_contact_count(), 16U);
    CHECK_EQ(controller.active_zone_contact_registration_count(), 0U);

    character->transform.translation =
        App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{50, 999, 50});
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 32U);
    CHECK_EQ(controller.active_zone_contact_registration_count(), 16U);
  }

  TEST_CASE("zone qualification rejects a raw candidate blocked outside by collision") {
    constexpr std::int16_t k_zone_id{20};
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, k_zone_id, false, false, 500U);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{.radius = 10.0F}}), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    character->controller_enabled = false;
    character->transform.translation = {.x = -15.0F, .y = 152.0F, .z = 6.0F};
    App::Character::PhysicalMotionService::synchronize(*character);
    character->physical_motion.candidate_translation = {.x = 6.0F, .y = 152.0F, .z = 6.0F};
    App::Omikron::Model3DOData decor{make_collision_decor(0.0F, 162.0F)};
    runtime->bind_decor_model(&decor);
    REQUIRE_EQ(runtime->decor_model(), &decor);
    REQUIRE_EQ(runtime->decor_runtime_objects().size(), 2U);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK(character->physical_motion.horizontal_collision.body_valid);
    CHECK_EQ(character->physical_motion.horizontal_collision.body_radius, 10.0F);
    CHECK(character->physical_motion.horizontal_collision.forward_collision);
    CHECK(character->transform.translation.x == doctest::Approx(-11.0F));
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x,
        character->transform.translation.x);
    CHECK_EQ(controller.zone_contact_count(), 0U);
  }

  TEST_CASE("zone qualification accepts the final wall-slide transform") {
    constexpr std::int16_t k_zone_id{21};
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, k_zone_id, false, false, 500U);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{.radius = 10.0F}}), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    character->controller_enabled = false;
    character->transform.translation = {.x = 0.0F, .y = 152.0F, .z = -10.0F};
    App::Character::PhysicalMotionService::synchronize(*character);
    character->physical_motion.candidate_translation = {.x = 20.0F, .y = 152.0F, .z = 10.0F};
    App::Omikron::Model3DOData decor{make_collision_decor(15.0F, 162.0F)};
    runtime->bind_decor_model(&decor);
    REQUIRE_EQ(runtime->decor_model(), &decor);
    REQUIRE_EQ(runtime->decor_runtime_objects().size(), 2U);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK(character->physical_motion.horizontal_collision.body_valid);
    CHECK_EQ(character->physical_motion.horizontal_collision.body_radius, 10.0F);
    CHECK(character->physical_motion.horizontal_collision.forward_collision);
    CHECK(character->transform.translation.x == doctest::Approx(4.0F));
    CHECK(character->transform.translation.z == doctest::Approx(10.0F));
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x,
        character->transform.translation.x);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.z,
        character->transform.translation.z);
    CHECK_EQ(controller.zone_contact_count(), 1U);
  }

  TEST_CASE("collision heading feeds spatial qualification and the next collision guard") {
    constexpr std::int16_t k_zone_id{26};
    constexpr std::int16_t k_corrected_heading_units{48};
    constexpr std::int16_t k_heading_span_units{12};
    const TempDirectory temp;
    write_zone_contact_fixtures(temp,
        false,
        k_zone_id,
        false,
        false,
        500U,
        k_corrected_heading_units,
        k_heading_span_units);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{.radius = 10.0F}}), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    character->controller_enabled = false;
    character->transform.translation = {.x = 0.0F, .y = 152.0F, .z = -10.0F};
    character->set_principal_orientation({});
    App::Character::PhysicalMotionService::synchronize(*character);
    character->physical_motion.candidate_translation = {.x = 20.0F, .y = 152.0F, .z = 10.0F};
    App::Omikron::Model3DOData decor{make_collision_decor(15.0F, 162.0F)};
    runtime->bind_decor_model(&decor);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    const auto& first_collision{character->physical_motion.horizontal_collision};
    CHECK(first_collision.forward_collision);
    CHECK(first_collision.automatic_heading_applied);
    CHECK(character->principal_orientation_degrees.y ==
          doctest::Approx(first_collision.yaw_after_degrees));
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK(controller.current_character_trigger_proxy()->heading_degrees ==
          doctest::Approx(first_collision.yaw_after_degrees));
    CHECK_EQ(controller.zone_contact_count(), 1U);
    CHECK(character->spatial_heading_suppression_latch);

    character->set_principal_orientation({.y = 90.0F});
    character->physical_motion.candidate_translation.x += 20.0F;
    character->physical_motion.candidate_translation.z += 20.0F;
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    const auto& second_collision{character->physical_motion.horizontal_collision};
    CHECK(second_collision.forward_collision);
    CHECK_FALSE(second_collision.automatic_heading_applied);
    CHECK_EQ(second_collision.automatic_heading_suppression,
        App::Character::AutomaticHeadingSuppressionReason::k_spatial_heading_latch);
    CHECK_EQ(controller.current_character_trigger_proxy()->heading_degrees, 90.0F);
    CHECK_FALSE(character->spatial_heading_suppression_latch);
    CHECK_EQ(controller.active_zone_contact_registration_count(), 1U);
  }

  TEST_CASE("mover next-tick displacement reaches each ordinary spatial sample") {
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, 27, false, false, 500U);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{.radius = 10.0F}}), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);
    REQUIRE(controller.tick().has_value());

    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    character->controller_enabled = false;
    character->transform.translation = {.x = 120.0F, .y = 152.0F, .z = 80.0F};
    App::Character::PhysicalMotionService::synchronize(*character);
    App::Omikron::Model3DOData decor{make_collision_decor(1000.0F, 162.0F)};
    decor.meshes.front().mover_flags = 0x10U;
    runtime->bind_decor_model(&decor);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK(character->physical_motion.support.mover_applied_this_tick);
    CHECK_EQ(character->physical_motion.horizontal_physical_x_per_tick, 2.0F);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x, 120.0F);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(character->physical_motion.horizontal_collision.intended_displacement.x, 2.0F);
    CHECK_EQ(character->transform.translation.x, 122.0F);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x,
        character->transform.translation.x);
  }

  TEST_CASE("class-two attachment movement reaches the next ordinary spatial sample") {
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, 28, false, false, 500U);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{.radius = 60.0F}}), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);
    REQUIRE(controller.tick().has_value());

    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    character->controller_enabled = false;
    character->transform.translation = {.x = 120.0F, .y = 7.3881507F, .z = 80.0F};
    App::Character::PhysicalMotionService::synchronize(*character);
    character->physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    character->physical_motion.vertical_velocity = 100.0F;
    App::Omikron::Model3DOData decor{make_class2_attachment_decor()};
    runtime->bind_decor_model(&decor);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK(character->physical_motion.class2_support_response.attachment_applied);
    CHECK(character->physical_motion.horizontal_physical_x_per_tick == doctest::Approx(2.0F));
    CHECK(character->physical_motion.horizontal_physical_z_per_tick == doctest::Approx(3.0F));

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK(character->physical_motion.horizontal_collision.intended_displacement.x ==
          doctest::Approx(2.0F));
    CHECK(character->physical_motion.horizontal_collision.intended_displacement.z ==
          doctest::Approx(3.0F));
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x,
        character->transform.translation.x);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.z,
        character->transform.translation.z);
  }

  TEST_CASE("ceiling-clamped live Y reaches the ordinary spatial sample") {
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, 29, false, false, 500U);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{.radius = 10.0F}}), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);
    REQUIRE(controller.tick().has_value());

    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    character->controller_enabled = false;
    character->transform.translation = {.x = 120.0F, .y = 152.0F, .z = 80.0F};
    App::Character::PhysicalMotionService::synchronize(*character);
    character->physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    character->physical_motion.vertical_velocity = -300.0F;
    App::Omikron::Model3DOData decor{make_collision_decor(1000.0F, 162.0F)};
    add_collision_ceiling(decor, 122.0F);
    runtime->bind_decor_model(&decor);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK(character->physical_motion.ceiling_collision.hit);
    CHECK(character->physical_motion.ceiling_collision.clamped);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.y,
        character->transform.translation.y);
    CHECK(character->transform.translation.y ==
          doctest::Approx(character->physical_motion.accepted_translation.y));
  }

  TEST_CASE("zone qualification observes complete no-support rollback") {
    constexpr std::int16_t k_zone_id{22};
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, k_zone_id, false, false, 500U);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{.radius = 10.0F}}), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    character->controller_enabled = false;
    character->transform.translation = {.x = 75.0F, .y = 152.0F, .z = 75.0F};
    App::Character::PhysicalMotionService::synchronize(*character);
    character->physical_motion.candidate_translation = {.x = 6.0F, .y = 152.0F, .z = 6.0F};

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(character->transform.translation.x, 75.0F);
    CHECK_EQ(character->transform.translation.y, 152.0F);
    CHECK_EQ(character->transform.translation.z, 75.0F);
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x,
        character->transform.translation.x);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.y,
        character->transform.translation.y);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.z,
        character->transform.translation.z);
    CHECK_EQ(controller.zone_contact_count(), 0U);
  }

  TEST_CASE("proxy qualification uses live degree heading instead of placement yaw") {
    constexpr std::int16_t k_zone_id{16};
    constexpr std::int16_t k_quarter_turn_units{1024};
    const TempDirectory temp;
    write_zone_contact_fixtures(
        temp, false, k_zone_id, false, false, 500U, k_quarter_turn_units, 100);
    StartedSession session{temp, {.model_loader = make_stub_model_loader(), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    CHECK_EQ(character->serialized_orientation_units, 4090);
    character->transform.translation =
        App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{50, 999, 50});
    character->set_principal_orientation(App::Runtime::Vec3{.x = 0.0F, .y = 90.0F, .z = 0.0F});

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK_EQ(controller.current_character_trigger_proxy()->heading_degrees, 90.0F);
    CHECK_EQ(controller.zone_contact_count(), 1U);
    CHECK(character->spatial_heading_suppression_latch);

    character->set_principal_orientation(App::Runtime::Vec3{});
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_FALSE(character->spatial_heading_suppression_latch);
    CHECK_EQ(controller.active_zone_contact_registration_count(), 1U);
    REQUIRE(controller.zone_contact(0) != nullptr);
    CHECK(controller.zone_contact(0)->heartbeat_state ==
          App::ZoneContactHeartbeatState::k_awaiting_refresh);
  }

  TEST_CASE("fire-and-forget current-character script freezes proxy and CTL service") {
    constexpr std::int16_t k_zone_id{17};
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, k_zone_id, false, false, 50U, 4090, 0, true);
    StartedSession session{temp, {.model_loader = make_stub_model_loader(), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK(controller.current_character_trigger_proxy()->synchronization_suspended);
    CHECK_FALSE(controller.current_character_trigger_proxy()->contact_ready);
    CHECK_EQ(controller.zone_contact_count(), 0U);
    const App::Script::ScriptRuntime* scripts{runtime->script_runtime()};
    REQUIRE(scripts != nullptr);
    REQUIRE_EQ(scripts->instances().size(), 1U);
    CHECK_FALSE(scripts->instances().front().completed);

    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    REQUIRE(character->ctl_controller.has_value());
    character->controller_enabled = true;
    character->ctl_controller->set_ordinary_control_suppressed(false);
    const float ctl_progress_before{character->ctl_controller->current_progress()};

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK(controller.current_character_trigger_proxy()->synchronization_suspended);
    CHECK_EQ(controller.current_character_trigger_proxy()->suspension_reason,
        "ordinary actor service not yet consumed");
    CHECK_FALSE(controller.current_character_trigger_proxy()->contact_ready);
    CHECK_EQ(controller.zone_contact_count(), 0U);
    CHECK_EQ(character->ctl_controller->current_progress(), ctl_progress_before);
    CHECK_FALSE(character->ctl_controller->ordinary_control_suppressed());
  }

  TEST_CASE("zone contact follows live runtime position instead of the AREA placement snapshot") {
    const TempDirectory temp;
    write_live_zone_contact_fixtures(temp);
    StartedSession session{temp, {.model_loader = make_stub_model_loader(), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 1U);
    REQUIRE(manager.game_state() != nullptr);
    CHECK(manager.game_state()->zone_flag(3795).value());

    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    CHECK_EQ(character->serialized_area_position.at(0), 50);
    CHECK_EQ(character->serialized_area_position.at(2), 50);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 1U);

    const auto camera{manager.world_presentation().take_camera()};
    REQUIRE(camera.has_value());
    REQUIRE(camera->operation_generation.has_value());
    manager.world_presentation().enqueue_camera_completion(App::WorldCameraOperationCompletion{
        .operation_generation = camera->operation_generation.value(),
        .scene_id = camera->scene_id,
        .scene_generation = camera->scene_generation,
        .source_area_id = camera->source_area_id,
        .camera_id = camera->camera_id});

    // Leave X/Z inside but move the live actor above the zone's Y slab. The
    // proxy's 3D broadphase must reject the contact before the polygon test.
    character->transform.translation.y = 1000.0F;
    REQUIRE(controller.tick(1.0F).has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 0U);
    CHECK(manager.game_state()->zone_flag(3795).value());

    // Now make the serialized snapshot explicitly outside while putting the
    // continuous live transform back inside all three proxy dimensions.
    character->serialized_area_position = {10000, 999, 10000};
    character->transform.translation =
        App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{50, 999, 50});
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 1U);
    CHECK_EQ(character->current_move_id(), std::optional<std::int16_t>{100});
    CHECK(character->controller_enabled);
  }

  TEST_CASE("zone-owned StartDialog enters the shared global dialog takeover") {
    const TempDirectory temp;
    write_zone_dialog_fixtures(temp);
    StartedSession session{temp, {.model_loader = make_stub_model_loader(), .stub_ctl_bank = true}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 1U);
    CHECK(controller.dialog_takeover_active());
    CHECK_EQ(controller.dialog_takeover_id(), std::optional<std::int16_t>{272});
    CHECK(manager.dialog_runtime().active());
  }

  TEST_CASE(
      "compact character values use current and explicit owner profiles without body reload") {
    const TempDirectory temp;
    write_character_value_fixtures(temp);
    StartedSession session{temp, {.model_loader = make_stub_model_loader()}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.area_script() != nullptr);
    CHECK(controller.area_script()->state() == AreaScriptState::k_ready);
    CHECK_EQ(controller.current_controlled_character(), std::optional<std::int16_t>{136});
    REQUIRE(manager.game_state() != nullptr);
    CHECK_EQ(manager.game_state()->global_variable(60).value(), 0);
    CHECK_EQ(manager.game_state()->global_variable(37).value(), 0);
    CHECK_EQ(manager.game_state()->global_variable(61).value(), 77);
    CHECK_EQ(manager.game_state()->global_variable(63).value(), 444);
    CHECK_EQ(manager.game_state()->global_variable(64).value(), 0);
    CHECK_EQ(manager.game_state()->global_variable(65).value(), 321);
    CHECK_EQ(manager.game_state()->character_value(136, 5).value(), 0);
    CHECK_EQ(manager.game_state()->character_value(310, 4).value(), 321);
    REQUIRE(manager.game_state()->current_character().has_value());
    CHECK_EQ(manager.game_state()->current_character()->character_id, 136);
    CHECK_EQ(manager.game_state()->current_character()->values.rings, 0);
    CHECK_EQ(manager.game_state()->current_character()->values.seteks, 0U);
    CHECK(runtime->character_runtime().find(136) != nullptr);
    // AREA load materializes every table-0 placement, so 310 is resident but never became the
    // current character and stays inactive (its persistent state bit is clear).
    const App::Character::RuntimeCharacter* const resident_310{
        runtime->character_runtime().find(310)};
    REQUIRE(resident_310 != nullptr);
    CHECK_FALSE(resident_310->active);
    CHECK_FALSE(resident_310->area_present);
  }

  TEST_CASE("current-character script bridge rejects missing and nonresident session targets") {
    SUBCASE("no current controlled character") {
      const TempDirectory temp;
      write_current_character_script_fixtures(temp);
      StartedSession session{temp};
      App::ScenarioManager& manager{session.manager};
      App::ScenarioStartupController& controller{session.controller};

      const auto result{controller.tick()};
      REQUIRE_FALSE(result.has_value());
      CHECK(result.error().find("current controlled character is not established") !=
            std::string::npos);
      REQUIRE(controller.area_script() != nullptr);
      CHECK(controller.area_script()->state() == AreaScriptState::k_failed);
    }

    SUBCASE("current controlled character references a nonresident body") {
      const TempDirectory temp;
      write_current_character_script_fixtures(temp);
      StartedSession session{temp};
      App::ScenarioManager& manager{session.manager};
      App::ScenarioStartupController& controller{session.controller};
      manager.set_controlled_character(
          App::ControlledCharacterRef{.character_id = 49, .world_scene_id = 1});

      const auto result{controller.tick()};
      REQUIRE_FALSE(result.has_value());
      CHECK(
          result.error().find("runtime body 0 (reference 49) does not exist") != std::string::npos);
      REQUIRE(controller.area_script() != nullptr);
      CHECK(controller.area_script()->state() == AreaScriptState::k_failed);
    }
  }

  TEST_CASE("retail AREA handoff detaches explicit current AREA before physical transfer") {
    const TempDirectory temp;
    write_handoff_fixtures(temp);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{.center = {.y = 8.0F}, .radius = 12.0F}})}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* source_runtime{manager.world_runtime(0)};
    REQUIRE(source_runtime != nullptr);

    REQUIRE(controller.tick().has_value());  // Select body and accept 0x2F.
    REQUIRE(controller.tick().has_value());  // Prepare target, then execute 0x47/0x49/0x30/0x03.
    const App::RuntimeAreaSlot* source{controller.runtime_area_slot(0)};
    const App::RuntimeAreaSlot* destination{controller.runtime_area_slot(1)};
    REQUIRE(source != nullptr);
    REQUIRE(destination != nullptr);
    CHECK_EQ(source->primary_area_id, 118);
    CHECK_EQ(destination->primary_area_id, 222);
    CHECK_EQ(destination->scene_id, 55);
    CHECK_EQ(controller.area_mapping(222), std::optional<std::int32_t>{55});
    CHECK(controller.area_script()->state() == AreaScriptState::k_ready);
    CHECK(controller.area_transition_state() == App::AreaTransitionState::k_idle);
    CHECK_EQ(manager.world_contexts()[0].residency, WorldSceneResidencyState::ResidentDetached);
    CHECK_EQ(manager.world_contexts()[1].residency, WorldSceneResidencyState::ResidentAttached);
    REQUIRE(manager.attached_world_head_context() != nullptr);
    CHECK_EQ(manager.attached_world_head_context()->scene_id, 1U);
    CHECK_EQ(controller.active_area_slot(), 0U);
    REQUIRE(manager.current_world_context() != nullptr);
    CHECK_EQ(manager.current_world_context()->scene_id, 0U);
    REQUIRE(manager.controlled_character().has_value());
    CHECK_EQ(manager.controlled_character()->world_scene_id, 0U);
    App::Character::RuntimeCharacter* source_character{
        source_runtime->character_runtime().find(136)};
    REQUIRE(source_character != nullptr);
    const App::Character::BodyIdentity body_identity{source_character->body_identity};
    const App::Runtime::Vec3 placed_transform{source_character->transform.translation};

    App::ScenarioRuntime* destination_runtime{manager.world_runtime(1)};
    REQUIRE(destination_runtime != nullptr);
    App::Omikron::Model3DOData destination_decor{
        make_collision_decor(1000.0F, placed_transform.y + 20.0F)};
    for (std::size_t index{0}; index < 3U; ++index) {
      destination_decor.vertices.at(index).position.x += placed_transform.x;
      destination_decor.vertices.at(index).position.z += placed_transform.z;
    }
    destination_runtime->bind_decor_model(&destination_decor);
    App::Character::PhysicalMotionService::synchronize(*source_character);
    source_character->physical_motion.vertical_velocity = 0.0F;
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());

    REQUIRE(manager.current_world_context() != nullptr);
    CHECK_EQ(manager.current_world_context()->scene_id, 1U);
    CHECK_EQ(controller.active_area_slot(), 1U);
    REQUIRE(manager.controlled_character().has_value());
    CHECK_EQ(manager.controlled_character()->body_identity, body_identity);
    CHECK_EQ(manager.controlled_character()->world_scene_id, 1U);
    CHECK(source_runtime->character_runtime().find(136) == nullptr);
    const App::Character::RuntimeCharacter* transferred{
        destination_runtime->character_runtime().find(136)};
    REQUIRE(transferred != nullptr);
    CHECK_EQ(transferred->body_identity, body_identity);
    CHECK_EQ(transferred->area_id, 222);
    CHECK_EQ(transferred->transform.translation.x, placed_transform.x);
    CHECK_EQ(transferred->transform.translation.y, placed_transform.y);
    CHECK_EQ(transferred->transform.translation.z, placed_transform.z);
    CHECK(transferred->physical_motion.initialized);
    CHECK(transferred->physical_motion.support.valid);
    CHECK_EQ(transferred->physical_motion.support.source_world_scene_id,
        std::optional<std::uint32_t>{1U});
    CHECK_EQ(manager.world_contexts()[0].residency, WorldSceneResidencyState::ResidentDetached);
    REQUIRE(manager.attached_world_head_context() != nullptr);
    CHECK_EQ(manager.attached_world_head_context()->scene_id, 1U);
  }

  TEST_CASE("a SCENE event that detaches its own SCENE is applied after the script returns") {
    const TempDirectory temp;
    write_handoff_fixtures(temp, 118, false, false, true);
    StartedSession session{temp,
        {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{
                                                     .center = {.y = 8.0F}, .radius = 12.0F}})}};

    REQUIRE(session.controller.tick().has_value());  // Accept 0x2F.
    // Prepare AREA 222, run 0x47 (attaching SCENE 55) and run the SCENE event 1 in the same tick;
    // its 0x48 must not destroy the executing context or the bytes it borrows.
    const auto ticked{session.controller.tick()};
    const std::string tick_error{ticked.has_value() ? std::string{} : ticked.error()};
    REQUIRE_MESSAGE(ticked.has_value(), tick_error);

    const App::RuntimeAreaSlot* const destination{session.controller.runtime_area_slot(1)};
    REQUIRE(destination != nullptr);
    CHECK_EQ(destination->primary_area_id, 222);
    CHECK_FALSE(destination->scene.has_value());
    CHECK_FALSE(destination->scene_script.has_value());
    CHECK_EQ(destination->scene_id, -1);
    CHECK_EQ(session.controller.area_mapping(222), std::optional<std::int32_t>{-1});
    CHECK_EQ(session.controller.zone_contact_count(), 0U);
    CHECK(session.controller.tick(1.0F / 30.0F).has_value());
  }

  TEST_CASE("a corrupt destination AREA record fails the transition without touching residency") {
    const TempDirectory temp;
    write_handoff_fixtures(temp);
    std::vector<std::byte> area{make_handoff_area_archive(118, false, false)};
    write_u32(area, (222U * 8U) + 4U, 0x00FFFFFFU);  // Record extends far past the archive.
    write_bytes(temp.root() / "IAM" / "AREA", area);
    StartedSession session{temp};

    REQUIRE(session.controller.tick().has_value());  // Source accepts 0x2F.
    REQUIRE(session.controller.area_transition_pending());
    const auto failed{session.controller.tick()};
    REQUIRE_FALSE(failed.has_value());
    CHECK_FALSE(failed.error().empty());

    // Nothing was destroyed or half-built: the source stays resident and current, and the
    // destination slot and world context were never populated.
    const App::RuntimeAreaSlot* const source{session.controller.runtime_area_slot(0)};
    const App::RuntimeAreaSlot* const destination{session.controller.runtime_area_slot(1)};
    REQUIRE(source != nullptr);
    REQUIRE(destination != nullptr);
    CHECK_EQ(source->primary_area_id, 118);
    CHECK_FALSE(destination->primary.has_value());
    CHECK_EQ(destination->primary_area_id, -1);
    CHECK_EQ(session.manager.world_contexts()[1].residency, WorldSceneResidencyState::Free);
    CHECK_EQ(session.manager.world_contexts()[0].residency, WorldSceneResidencyState::ResidentAttached);
    CHECK_EQ(session.controller.active_area_slot(), 0U);

    // The failure latches: the coordinator never silently restarts a failed transaction.
    CHECK_FALSE(session.controller.tick().has_value());
  }

  TEST_CASE("a multi-step actor tick carries its accumulator across an event-9 body transfer") {
    const TempDirectory temp;
    write_handoff_fixtures(temp);
    StartedSession session{temp,
        {.model_loader = make_stub_model_loader(
             {.collision_sphere = App::Omikron::CollisionSphere{.center = {.y = 8.0F}, .radius = 12.0F}})}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime& source_runtime{session.world(0U)};

    REQUIRE(controller.tick().has_value());  // Select body and accept 0x2F.
    REQUIRE(controller.tick().has_value());  // Prepare target, then execute 0x47/0x49/0x30/0x03.
    App::Character::RuntimeCharacter* source_character{source_runtime.character_runtime().find(136)};
    REQUIRE(source_character != nullptr);
    const App::Character::BodyIdentity body_identity{source_character->body_identity};
    const App::Runtime::Vec3 placed{source_character->transform.translation};

    App::ScenarioRuntime& destination_runtime{session.world(1U)};
    App::Omikron::Model3DOData destination_decor{make_collision_decor(1000.0F, placed.y + 20.0F)};
    for (std::size_t index{0}; index < 3U; ++index) {
      destination_decor.vertices.at(index).position.x += placed.x;
      destination_decor.vertices.at(index).position.z += placed.z;
    }
    destination_runtime.bind_decor_model(&destination_decor);
    App::Character::PhysicalMotionService::synchronize(*source_character);
    source_character->physical_motion.vertical_velocity = 0.0F;

    // 3.5 logic steps in one call: the handoff happens on the first step, so the remaining steps
    // must keep consuming the accumulator that travelled with the body.
    constexpr float k_step{App::Character::PhysicalMotionService::K_LOGIC_STEP_SECONDS};
    REQUIRE(controller.tick(3.5F * k_step).has_value());

    REQUIRE(manager.controlled_character().has_value());
    CHECK_EQ(manager.controlled_character()->world_scene_id, 1U);
    const App::Character::RuntimeCharacter* const transferred{
        destination_runtime.character_runtime().find_body(body_identity)};
    REQUIRE(transferred != nullptr);
    CHECK_EQ(transferred->ordinary_actor_service_generation, 3U);
    CHECK(transferred->physical_motion.accumulator_seconds < k_step);
    CHECK(transferred->physical_motion.accumulator_seconds == doctest::Approx(0.5F * k_step));
    CHECK(source_runtime.character_runtime().find_body(body_identity) == nullptr);
  }

  TEST_CASE("physical support commits logical AREA when body storage already matches destination") {
    const TempDirectory temp;
    write_handoff_fixtures(temp);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{.center = {.y = 8.0F}, .radius = 12.0F}})}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* source_runtime{manager.world_runtime(0)};
    REQUIRE(source_runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* source_character{
        source_runtime->character_runtime().find(136)};
    REQUIRE(source_character != nullptr);
    const App::Character::BodyIdentity body_identity{source_character->body_identity};
    const App::Runtime::Vec3 position{source_character->transform.translation};
    REQUIRE(manager.transfer_controlled_character(0, 1).has_value());
    App::ScenarioRuntime* destination_runtime{manager.world_runtime(1)};
    REQUIRE(destination_runtime != nullptr);
    App::Character::RuntimeCharacter* destination_character{
        destination_runtime->character_runtime().find_body(body_identity)};
    REQUIRE(destination_character != nullptr);
    App::Omikron::Model3DOData destination_decor{make_collision_decor(1000.0F, position.y + 20.0F)};
    for (std::size_t index{0}; index < 3U; ++index) {
      destination_decor.vertices.at(index).position.x += position.x;
      destination_decor.vertices.at(index).position.z += position.z;
    }
    destination_runtime->bind_decor_model(&destination_decor);
    App::Character::PhysicalMotionService::synchronize(*destination_character);
    destination_character->physical_motion.vertical_velocity = 0.0F;

    REQUIRE_EQ(controller.active_area_slot(), 0U);
    REQUIRE_EQ(manager.current_world_context()->scene_id, 0U);
    REQUIRE_EQ(manager.controlled_character()->world_scene_id, 1U);
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());

    CHECK_EQ(controller.active_area_slot(), 1U);
    CHECK_EQ(manager.current_world_context()->scene_id, 1U);
    CHECK_EQ(manager.game_state()->current_area(), 222);
    CHECK_EQ(manager.controlled_character()->world_scene_id, 1U);
    CHECK_EQ(manager.controlled_character()->body_identity, body_identity);
    CHECK(destination_runtime->character_runtime().find_body(body_identity) != nullptr);
    CHECK(source_runtime->character_runtime().find_body(body_identity) == nullptr);
    REQUIRE(manager.attached_world_head_context() != nullptr);
    CHECK_EQ(manager.attached_world_head_context()->scene_id, 1U);
  }

  TEST_CASE("physical support matching logical current does not follow split body storage") {
    const TempDirectory temp;
    write_handoff_fixtures(temp, 9999);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{.center = {.y = 8.0F}, .radius = 12.0F}})}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* source_runtime{manager.world_runtime(0)};
    REQUIRE(source_runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* source_character{
        source_runtime->character_runtime().find(136)};
    REQUIRE(source_character != nullptr);
    const App::Character::BodyIdentity body_identity{source_character->body_identity};
    const App::Runtime::Vec3 position{source_character->transform.translation};
    REQUIRE(manager.transfer_controlled_character(0, 1).has_value());
    App::ScenarioRuntime* destination_runtime{manager.world_runtime(1)};
    REQUIRE(destination_runtime != nullptr);
    App::Character::RuntimeCharacter* destination_character{
        destination_runtime->character_runtime().find_body(body_identity)};
    REQUIRE(destination_character != nullptr);
    App::Omikron::Model3DOData source_decor{make_collision_decor(1000.0F, position.y + 20.0F)};
    for (std::size_t index{0}; index < 3U; ++index) {
      source_decor.vertices.at(index).position.x += position.x;
      source_decor.vertices.at(index).position.z += position.z;
    }
    source_runtime->bind_decor_model(&source_decor);
    App::Character::PhysicalMotionService::synchronize(*destination_character);
    destination_character->physical_motion.vertical_velocity = 0.0F;

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());

    CHECK_EQ(controller.active_area_slot(), 0U);
    CHECK_EQ(manager.current_world_context()->scene_id, 0U);
    CHECK_EQ(manager.game_state()->current_area(), 118);
    CHECK_EQ(manager.controlled_character()->world_scene_id, 1U);
    CHECK_EQ(manager.controlled_character()->body_identity, body_identity);
  }

  TEST_CASE("slot1 AREA transition requester survives event-9 logical current handoff") {
    const TempDirectory temp;
    write_handoff_fixtures(temp, 118, true);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{.center = {.y = 8.0F}, .radius = 12.0F}})}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* source_runtime{manager.world_runtime(0)};
    REQUIRE(source_runtime != nullptr);

    REQUIRE(controller.tick().has_value());  // AREA118 requests AREA222.
    REQUIRE(controller.begin_tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.area_transition_pending());  // AREA222 requests AREA142.
    REQUIRE_EQ(controller.active_area_slot(), 0U);
    REQUIRE(controller.area_script(1) != nullptr);
    CHECK(controller.area_script(1)->state() == AreaScriptState::k_waiting);

    App::Character::RuntimeCharacter* source_character{
        source_runtime->character_runtime().find(136)};
    REQUIRE(source_character != nullptr);
    const App::Character::BodyIdentity body_identity{source_character->body_identity};
    const App::Runtime::Vec3 position{source_character->transform.translation};
    REQUIRE(manager.transfer_controlled_character(0, 1).has_value());
    App::ScenarioRuntime* destination_runtime{manager.world_runtime(1)};
    REQUIRE(destination_runtime != nullptr);
    App::Character::RuntimeCharacter* destination_character{
        destination_runtime->character_runtime().find_body(body_identity)};
    REQUIRE(destination_character != nullptr);
    App::Omikron::Model3DOData destination_decor{make_collision_decor(1000.0F, position.y + 20.0F)};
    for (std::size_t index{0}; index < 3U; ++index) {
      destination_decor.vertices.at(index).position.x += position.x;
      destination_decor.vertices.at(index).position.z += position.z;
    }
    destination_runtime->bind_decor_model(&destination_decor);
    App::Character::PhysicalMotionService::synchronize(*destination_character);
    destination_character->physical_motion.vertical_velocity = 0.0F;

    REQUIRE(controller.finish_tick(1.0F / 30.0F).has_value());
    REQUIRE_EQ(controller.active_area_slot(), 1U);
    REQUIRE_EQ(manager.current_world_context()->scene_id, 1U);
    REQUIRE(controller.area_transition_pending());

    REQUIRE(controller.begin_tick(1.0F / 30.0F).has_value());
    CHECK_FALSE(controller.area_transition_pending());
    REQUIRE(controller.runtime_area_slot(0) != nullptr);
    CHECK_EQ(controller.runtime_area_slot(0)->primary_area_id, 142);
    CHECK_EQ(controller.active_area_slot(), 1U);
    CHECK_EQ(manager.current_world_context()->scene_id, 1U);
    CHECK(controller.area_script(1)->state() == AreaScriptState::k_ready);
    REQUIRE(controller.finish_tick(1.0F / 30.0F).has_value());
  }

  TEST_CASE("SelectCurrentCharacter changes body storage ownership but not logical current AREA") {
    const TempDirectory temp;
    write_handoff_fixtures(temp, 118, false, true);
    StartedSession session{temp, {.model_loader = make_stub_model_loader()}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* source_runtime{manager.world_runtime(0)};
    REQUIRE(source_runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    const auto selected{controller.tick()};
    const std::string selection_error{selected.has_value() ? std::string{} : selected.error()};
    INFO(selection_error);
    REQUIRE(selected.has_value());

    REQUIRE(manager.controlled_character().has_value());
    CHECK_EQ(manager.controlled_character()->character_id, 310);
    CHECK_EQ(manager.controlled_character()->world_scene_id, 1U);
    CHECK_EQ(controller.active_area_slot(), 0U);
    REQUIRE(manager.current_world_context() != nullptr);
    CHECK_EQ(manager.current_world_context()->scene_id, 0U);
    CHECK_EQ(manager.game_state()->current_area(), 118);

    App::ScenarioRuntime* destination_runtime{manager.world_runtime(1)};
    REQUIRE(destination_runtime != nullptr);
    App::Character::RuntimeCharacter* character{destination_runtime->character_runtime().find_body(
        manager.controlled_character()->body_identity)};
    REQUIRE(character != nullptr);
    auto model{std::make_shared<App::Character::ModelResource>()};
    model->name = "TARGET";
    model->groups.push_back(App::Omikron::MaterialGroup{});
    model->model.header.collision_sphere_count = 1U;
    model->model.header.collision_sphere_slots.at(0) =
        App::Omikron::CollisionSphere{.center = {.y = 8.0F}, .radius = 12.0F};
    character->model_resource = std::move(model);
    const App::Character::BodyIdentity body_identity{character->body_identity};
    const App::Runtime::Vec3 position{character->transform.translation};
    App::Omikron::Model3DOData destination_decor{make_collision_decor(1000.0F, position.y + 20.0F)};
    for (std::size_t index{0}; index < 3U; ++index) {
      destination_decor.vertices.at(index).position.x += position.x;
      destination_decor.vertices.at(index).position.z += position.z;
    }
    destination_runtime->bind_decor_model(&destination_decor);
    App::Character::PhysicalMotionService::synchronize(*character);
    character->physical_motion.vertical_velocity = 0.0F;

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.active_area_slot(), 1U);
    CHECK_EQ(manager.current_world_context()->scene_id, 1U);
    CHECK_EQ(manager.game_state()->current_area(), 222);
    CHECK_EQ(manager.controlled_character()->world_scene_id, 1U);
    CHECK_EQ(manager.controlled_character()->body_identity, body_identity);
  }

  TEST_CASE("explicit nonresident AREA release is a successful no-op") {
    const TempDirectory temp;
    write_handoff_fixtures(temp, 9999);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{.center = {.y = 8.0F}, .radius = 12.0F}})}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);
    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick().has_value());
    CHECK(controller.area_script()->state() == AreaScriptState::k_ready);
    CHECK(controller.area_transition_state() == App::AreaTransitionState::k_idle);
    CHECK_EQ(manager.world_contexts()[0].residency, WorldSceneResidencyState::ResidentAttached);
    CHECK_EQ(manager.world_contexts()[1].residency, WorldSceneResidencyState::ResidentAttached);
  }

  TEST_CASE("minus one release still detaches the non-current resident AREA") {
    const TempDirectory temp;
    write_handoff_fixtures(temp, -1);
    StartedSession session{temp, {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{.center = {.y = 8.0F}, .radius = 12.0F}})}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);
    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick().has_value());
    CHECK(controller.area_script()->state() == AreaScriptState::k_ready);
    CHECK(controller.area_transition_state() == App::AreaTransitionState::k_idle);
    CHECK_EQ(manager.world_contexts()[0].residency, WorldSceneResidencyState::ResidentAttached);
    CHECK_EQ(manager.world_contexts()[1].residency, WorldSceneResidencyState::ResidentDetached);
    REQUIRE(manager.current_world_context() != nullptr);
    CHECK_EQ(manager.current_world_context()->scene_id, 0U);
  }

  TEST_CASE("Startup reaches the main menu through START, AREA 118 and interface 29") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    // Inline: asserts the pre-tick state and installs its model loader only after those checks.
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    App::ScenarioStartupController controller;

    REQUIRE(controller.initialize(manager).has_value());

    CHECK_EQ(controller.initial_area_id(), 118);
    CHECK_EQ(controller.linked_area_id(), -1);
    CHECK_EQ(controller.area_mapping(118), std::optional<std::int32_t>{-1});
    REQUIRE(manager.game_state() != nullptr);
    CHECK_EQ(manager.game_state()->current_area(), 118);
    CHECK_EQ(manager.game_state()->linked_area(), -1);

    REQUIRE(controller.area_record() != nullptr);
    CHECK_EQ(controller.area_record()->record_size(), 0x9C0U);
    CHECK_EQ(controller.area_record()->primary_event_offset(), 0x3FCU);
    CHECK_EQ(controller.area_record()->model3do_name(), "GRID");
    CHECK_EQ(controller.area_record()->scenario_scx_name(), "GRID");
    CHECK(manager.world_contexts()[0].decor_model.has_value());

    // GRID.SCX in world context 0, context 1 free, mode slot loaded.
    CHECK_EQ(manager.world_contexts()[0].scene_id, 0U);
    CHECK_EQ(manager.world_contexts()[0].residency, WorldSceneResidencyState::ResidentAttached);
    CHECK_EQ(manager.world_contexts()[0].scenario_path, "SCPTDATA/GRID.SCX");
    CHECK(manager.world_contexts()[1].residency == WorldSceneResidencyState::Free);
    CHECK(manager.gameplay_mode_scx() != nullptr);

    // Loaded, queued and activated — but not executed until tick().
    REQUIRE(controller.area_script() != nullptr);
    CHECK(controller.area_script()->state() == AreaScriptState::k_ready);
    CHECK_FALSE(controller.main_menu_active());

    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);
    install_resident_model_loader(runtime->character_runtime(),
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          auto resource{std::make_shared<App::Character::ModelResource>()};
          resource->name = name;
          resource->groups.push_back(App::Omikron::MaterialGroup{});
          return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
        });

    controller.dispatcher().set_interface_open_sink(
        [](const App::InterfaceOpenRequest& request)
            -> std::expected<App::InterfaceHandle, std::string> {
          return App::InterfaceHandle{.interface_id = request.interface_id, .generation = 1};
        });

    REQUIRE(controller.tick().has_value());
    CHECK(controller.ticked());
    CHECK(controller.main_menu_active());
    CHECK(controller.area_script()->state() == AreaScriptState::k_waiting);
    CHECK_EQ(controller.area_script()->wait_state(), 6U);
    CHECK_EQ(controller.current_controlled_character(), std::optional<std::int16_t>{136});
    const App::Character::RuntimeCharacter* current{runtime->character_runtime().find(136)};
    REQUIRE(current != nullptr);
    CHECK_FALSE(current->presentation_enabled);
    CHECK_FALSE(current->renderable());
    CHECK_FALSE(current->controller_enabled);
  }

  TEST_CASE("A negative initial area ID fails startup cleanly") {
    const TempDirectory temp;
    write_boot_fixtures(temp);

    std::vector<std::byte> start{make_start()};
    write_u16(start, 0x586, 0xFFFF);  // -1.
    write_bytes(temp.root() / "IAM" / "START", start);

    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    App::ScenarioStartupController controller;
    const auto result{controller.initialize(manager)};
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("negative") != std::string::npos);
  }

  TEST_CASE("New Game tracked 1KaylArrives completes and reaches dialog 272") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_complete_kayl_arrives_scx());
    write_bytes(temp.root() / "IAM" / "DIALOG", make_dialog_archive(272));
    StartedSession session{temp, {.model_loader = [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          auto resource{std::make_shared<App::Character::ModelResource>()};
          resource->name = name;
          resource->model.materials.push_back(App::Omikron::Material{});
          resource->model.meshes.push_back(App::Omikron::MeshDescriptor{.mesh_id = 1U,
              .script_id = 1U,
              .name = "UBassin",
              .parent_id = -1,
              .first_child_id = -1,
              .next_sibling_id = -1});
          resource->model.polygons.resize(1U);
          resource->model.root_mesh_index = 0;
          resource->actor_object_index = 0U;
          resource->model.hierarchy_parent_index = {-1};
          resource->model.hierarchy_first_child_index = {-1};
          resource->model.hierarchy_next_sibling_index = {-1};
          resource->model.hierarchy_reachable = {1U};
          resource->model.skin_parent_index = {-1};
          resource->model.runtime_objects = {App::Omikron::Model3DOData::RuntimeObjectState{}};
          resource->groups.push_back(App::Omikron::MaterialGroup{});
          return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
        }}};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    controller.dispatcher().set_interface_open_sink(
        [](const App::InterfaceOpenRequest& request)
            -> std::expected<App::InterfaceHandle, std::string> {
          return App::InterfaceHandle{.interface_id = request.interface_id, .generation = 1};
        });

    App::WorldSceneContext* context{manager.current_world_context()};
    REQUIRE(context != nullptr);
    REQUIRE(context->runtime != nullptr);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller
            .complete_interface(App::InterfaceCompletion{
                .handle = App::InterfaceHandle{.interface_id = 29, .generation = 1}, .result = 3})
            .has_value());
    REQUIRE_EQ(manager.world_presentation().pending_fade_count(), 1U);
    const auto bootstrap_fade{manager.world_presentation().take_fade()};
    REQUIRE(bootstrap_fade.has_value());
    CHECK_EQ(bootstrap_fade->mode, 1U);
    CHECK_EQ(bootstrap_fade->color, 0U);
    CHECK_EQ(bootstrap_fade->duration_units, 0);
    CHECK_EQ(bootstrap_fade->delay_units, 0);
    const auto launched{controller.tick()};
    const std::string launch_error{launched ? std::string{} : launched.error()};
    CAPTURE(launch_error);
    REQUIRE(launched.has_value());
    REQUIRE_EQ(manager.world_presentation().pending_letterbox_count(), 1U);
    const auto letterbox{manager.world_presentation().take_letterbox()};
    REQUIRE(letterbox.has_value());
    CHECK(letterbox->enabled);
    REQUIRE_EQ(manager.world_presentation().pending_fade_count(), 1U);
    const auto fade{manager.world_presentation().take_fade()};
    REQUIRE(fade.has_value());
    CHECK_EQ(fade->mode, 2U);
    CHECK_EQ(fade->color, 0U);
    CHECK_EQ(fade->duration_units, 30);
    CHECK_EQ(fade->delay_units, 0);
    const App::Script::AreaScriptRuntime* area_script{controller.area_script()};
    REQUIRE(area_script != nullptr);
    CHECK(area_script->state() == App::Script::AreaScriptState::k_waiting);
    CHECK_EQ(area_script->runtime_state(), 4U);
    CHECK_EQ(area_script->instruction_pointer(), 0x498U);
    CHECK(area_script->wait_info().kind == App::Script::AreaWaitKind::k_character_script);
    REQUIRE(area_script->wait_info().character_script_instance.has_value());
    REQUIRE(area_script->last_character_script_request().has_value());
    CHECK(area_script->last_character_script_request()->target ==
          App::Script::AreaCharacterScriptTarget::k_explicit);
    CHECK_EQ(area_script->last_character_script_request()->character_id,
        std::optional<std::int16_t>{310});
    CHECK_EQ(area_script->last_character_script_request()->camera_duration_units, 0);

    const App::Character::RuntimeCharacter* character{
        context->runtime->character_runtime().find(310)};
    REQUIRE(character != nullptr);
    CHECK(character->active);
    CHECK(character->area_present);
    CHECK_EQ(character->model_resource_name, "HO1_FNM");
    CHECK_EQ(character->transform.translation.x, -399.0F);
    CHECK_EQ(character->transform.translation.y, -42.0F);
    CHECK_EQ(character->transform.translation.z, -126.0F);

    App::Script::ScriptRuntime* script_runtime{context->runtime->script_runtime()};
    REQUIRE(script_runtime != nullptr);
    REQUIRE_EQ(script_runtime->instances().size(), 1U);
    const std::size_t instance_id{area_script->wait_info().character_script_instance.value()};
    const App::Script::ScriptInstance* instance{script_runtime->instance(instance_id)};
    REQUIRE(instance != nullptr);
    CHECK_EQ(instance->script_name, "1KaylArrives");
    CHECK_EQ(instance->launch_context.character_id, std::optional<std::int16_t>{310});
    CHECK_EQ(instance->launch_context.parameter, 0);

    context->runtime->tick(1.0F / 30.0F);
    instance = script_runtime->instance(instance_id);
    REQUIRE(instance != nullptr);
    CHECK_FALSE(instance->paused);
    CHECK_FALSE(instance->completed);
    context->runtime->tick(1.0F / 30.0F);
    CHECK(instance->completed);

    // Startup polls the exact completed child, resumes at record offset 0x498,
    // launches script 310/6, continues through presentation, then explicitly
    // returns from compact dispatch after entering DIALOG 272.
    REQUIRE(controller.tick().has_value());
    CHECK_EQ(area_script->instruction_pointer(), 0x4ABU);
    REQUIRE_EQ(script_runtime->instances().size(), 2U);
    CHECK(controller.dialog_takeover_active());
    CHECK_EQ(controller.dialog_takeover_id(), std::optional<std::int16_t>{272});
    CHECK(manager.dialog_runtime().active());
    CHECK_EQ(area_script->runtime_state(), 1U);
    CHECK_EQ(area_script->instruction_pointer(), 0x4ABU);
    CHECK(area_script->wait_info().kind == App::Script::AreaWaitKind::k_none);
  }

  TEST_CASE("OBJECTS submit authored Runtime world text with raw-byte lifetime") {
    const TempDirectory temp;
    const std::string authored_text{"{fD}You have been the victim of a violent attack..."};
    write_bytes(temp.root() / "IAM" / "START", make_start());
    write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
    write_bytes(temp.root() / "IAM" / "AREA", make_object_activation_area_archive(141U));
    write_bytes(temp.root() / "IAM" / "OBJECT",
        make_object_archive(141U, 7U, "ZVO M010 Agression", authored_text));
    write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
    write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
    StartedSession session{temp};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    REQUIRE(controller.tick().has_value());

    const auto voice{manager.world_presentation().take_voice_over()};
    REQUIRE(voice.has_value());
    CHECK_EQ(voice->object_id, 141);
    CHECK_EQ(voice->audio_path, "VOICEOFF/ZVO M010 Agression.ADP");
    // Every activation releases Runtime's single OBJECTS image slot.
    const auto image{manager.world_presentation().take_world_image()};
    REQUIRE(image.has_value());
    CHECK(image->image_path.empty());
    const auto world_text{manager.world_presentation().take_world_text()};
    REQUIRE(world_text.has_value());
    CHECK_EQ(world_text->document.authored_bytes(), authored_text);
    CHECK_EQ(App::Interface::runtime_text_plain_bytes(world_text->document),
        "You have been the victim of a violent attack...");
    CHECK_EQ(world_text->duration_ms, static_cast<std::uint32_t>(authored_text.size()) * 80U);
    CHECK_EQ(world_text->provenance.source_kind, App::TextSourceKind::k_iam_object);
    CHECK_EQ(world_text->provenance.object_id, 141);
    CHECK_EQ(world_text->provenance.audio_resource, "VOICEOFF/ZVO M010 Agression.ADP");
    CHECK_EQ(world_text->provenance.role, App::TextPresentationRole::k_unknown);
    CHECK_EQ(
        world_text->provenance.modernization_policy, App::TextModernizationPolicy::k_faithful_only);
    const App::Script::AreaScriptRuntime* runtime{controller.area_script()};
    REQUIRE(runtime != nullptr);
    CHECK_FALSE(runtime->last_run_returned_early());
    CHECK(runtime->state() == App::Script::AreaScriptState::k_ready);
    CHECK(runtime->wait_info().kind == App::Script::AreaWaitKind::k_none);
  }

  TEST_CASE("OBJECTS ZVOT substitution, -1, and type 0x10 remain safe") {
    SUBCASE("ZVOT uses the recovered JINGOFF3 replacement") {
      const TempDirectory temp;
      write_bytes(temp.root() / "IAM" / "START", make_start());
      write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
      write_bytes(temp.root() / "IAM" / "AREA", make_object_activation_area_archive(141U));
      write_bytes(
          temp.root() / "IAM" / "OBJECT", make_object_archive(141U, 0U, "ZVOT Intro", "short"));
      write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
      write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
      StartedSession session{temp};
      App::ScenarioManager& manager{session.manager};
      App::ScenarioStartupController& controller{session.controller};
      REQUIRE(controller.tick().has_value());
      const auto voice{manager.world_presentation().take_voice_over()};
      REQUIRE(voice.has_value());
      CHECK_EQ(voice->audio_path, "VOICEOFF/JINGOFF3.ADP");
      const auto world_text{manager.world_presentation().take_world_text()};
      REQUIRE(world_text.has_value());
      CHECK_EQ(world_text->duration_ms, 2000U);
    }

    SUBCASE("-1 does not require IAM/OBJECT") {
      const TempDirectory temp;
      write_bytes(temp.root() / "IAM" / "START", make_start());
      write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
      write_bytes(temp.root() / "IAM" / "AREA", make_object_activation_area_archive(0xFFFFU));
      write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
      write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
      StartedSession session{temp};
      App::ScenarioManager& manager{session.manager};
      App::ScenarioStartupController& controller{session.controller};
      REQUIRE(controller.tick().has_value());
      CHECK_EQ(manager.world_presentation().pending_voice_over_count(), 0U);
      CHECK_EQ(manager.world_presentation().pending_world_text_count(), 0U);
    }

    SUBCASE("type 0x10 presents IMAGES/<stem>.BMP instead of a voice-over") {
      const TempDirectory temp;
      write_bytes(temp.root() / "IAM" / "START", make_start());
      write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
      write_bytes(temp.root() / "IAM" / "AREA", make_object_activation_area_archive(141U));
      write_bytes(temp.root() / "IAM" / "OBJECT",
          make_object_archive(141U, 0x10U, "ZVOG001", "{X030040}{f3}"));
      write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
      write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
      StartedSession session{temp};
      App::ScenarioManager& manager{session.manager};
      App::ScenarioStartupController& controller{session.controller};
      REQUIRE(controller.tick().has_value());
      CHECK_EQ(manager.world_presentation().pending_voice_over_count(), 0U);
      const auto image{manager.world_presentation().take_world_image()};
      REQUIRE(image.has_value());
      CHECK_EQ(image->object_id, 141);
      CHECK_EQ(image->image_path, "IMAGES/ZVOG001.BMP");
      // Runtime still submits the (invisible) authored text for image objects.
      const auto world_text{manager.world_presentation().take_world_text()};
      REQUIRE(world_text.has_value());
      CHECK(world_text->provenance.audio_resource.empty());
    }
  }

  TEST_CASE("0x47 maps a nonresident AREA without loading its SCENE") {
    const TempDirectory temp;
    write_bytes(temp.root() / "IAM" / "START", make_start());
    write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());

    std::vector<std::byte> area_archive{make_area_archive()};
    Buffer script;
    script.u8(0x47).u16(237).u16(57).u8(0x03);
    constexpr std::size_t k_record_offset{0x800U};
    constexpr std::size_t k_script_offset{0x3FCU};
    std::memcpy(area_archive.data() + k_record_offset + k_script_offset,
        script.data().data(),
        script.data().size());
    write_bytes(temp.root() / "IAM" / "AREA", area_archive);

    // Deliberately do not provide IAM/SCENE. Runtime's nonresident 0x47 path
    // updates only the AREA -> SCENE mapping and cannot require SCENE bytes.
    write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
    write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());

    StartedSession session{temp};
    App::ScenarioManager& manager{session.manager};
    App::ScenarioStartupController& controller{session.controller};
    REQUIRE(controller.tick().has_value());

    CHECK_EQ(controller.area_mapping(237), std::optional<std::int32_t>{57});
    REQUIRE(manager.game_state() != nullptr);
    CHECK_EQ(manager.game_state()->current_area(), 118);
    CHECK_EQ(controller.active_area_slot(), 0U);

    const App::RuntimeAreaSlot* const source{controller.runtime_area_slot(0)};
    const App::RuntimeAreaSlot* const alternate{controller.runtime_area_slot(1)};
    REQUIRE(source != nullptr);
    REQUIRE(alternate != nullptr);
    CHECK_EQ(source->primary_area_id, 118);
    CHECK_FALSE(alternate->primary.has_value());
    CHECK_EQ(alternate->primary_area_id, -1);

    REQUIRE_EQ(manager.world_contexts().size(), 2U);
    CHECK(manager.world_contexts()[0].residency == WorldSceneResidencyState::ResidentAttached);
    CHECK(manager.world_contexts()[1].residency == WorldSceneResidencyState::Free);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

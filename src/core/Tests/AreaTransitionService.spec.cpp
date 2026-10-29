#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Scenario/AreaTransitionService.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioStartupController.hpp"
#include "Core/Scenario/ZoneContactService.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "ScenarioSessionFixtures.hpp"

namespace {

/// Models with one authored collision sphere, which the handoff fixtures need for 0x49 placement.
SessionOptions body_with_collision_sphere() {
  return {.model_loader = make_stub_model_loader({.collision_sphere = App::Omikron::CollisionSphere{
                                                      .center = {.y = 8.0F}, .radius = 12.0F}})};
}

constexpr std::int16_t K_CROSSING_ZONE_ID{24};
constexpr std::uint16_t K_PRE_CROSSING_SCRIPT_ID{248};
constexpr std::uint16_t K_POST_CROSSING_SCRIPT_ID{161};
constexpr float K_ACTOR_STEP_SECONDS{1.0F / 30.0F};

/// A session whose zone event requests 0x2F(222, 248, 161) once the controlled body enters.
struct RichTransitionSession {
  TempDirectory temp;
  std::optional<StartedSession> session;
  App::Character::RuntimeCharacter* character{nullptr};

  RichTransitionSession() {
    write_rich_transition_fixtures(
        temp, K_CROSSING_ZONE_ID, K_PRE_CROSSING_SCRIPT_ID, K_POST_CROSSING_SCRIPT_ID);
    session.emplace(
        temp, SessionOptions{.model_loader = make_stub_model_loader(), .stub_ctl_bank = true});
    REQUIRE(controller().tick().has_value());
    character = source_world().character_runtime().find(136);
    REQUIRE(character != nullptr);
  }

  [[nodiscard]] App::ScenarioStartupController& controller() {
    return session->controller;
  }
  [[nodiscard]] App::ScenarioRuntime& source_world() {
    return session->world(0U);
  }
  [[nodiscard]] App::AreaTransitionState state() {
    return controller().area_transition_state();
  }
  [[nodiscard]] const App::Script::AreaScriptRuntime& requester() {
    const App::ZoneContactContext* const contact{controller().zone_contact(0)};
    REQUIRE(contact != nullptr);
    REQUIRE(contact->script != nullptr);
    return *contact->script;
  }

  void tick(const float delta_seconds = K_ACTOR_STEP_SECONDS) {
    const auto ticked{controller().tick(delta_seconds)};
    INFO(controller().last_error());
    REQUIRE(ticked.has_value());
  }
  /// Advances the source world's structured scripts, completing any running crossing script.
  void run_crossing_scripts() {
    source_world().script_runtime()->tick(K_ACTOR_STEP_SECONDS);
  }

  /// Enters the zone; its event 1 executes 0x2F and the next service prepares AREA 222.
  void enter_zone_and_prepare() {
    character->transform.translation =
        App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{50, 999, 50});
    tick();  // Heartbeat registers the contact.
    tick();  // Event 1 runs 0x2F.
    REQUIRE(state() == App::AreaTransitionState::k_rich_loading);
    tick();  // The destination is prepared and the pre-crossing script starts.
    REQUIRE(state() == App::AreaTransitionState::k_rich_pre_running);
  }
  /// Leaves the zone and ticks until the coordinator observes the queued departure event.
  void leave_zone(const App::AreaTransitionState expected) {
    character->transform.translation.x = 1000.0F;
    for (std::size_t attempt{0}; attempt < 8U && state() != expected; ++attempt) {
      tick();
    }
    REQUIRE(state() == expected);
  }
};

}  // namespace

TEST_SUITE("Core::Scenario::AreaTransitionService") {
  TEST_CASE("a seamless transition is accepted, prepared, completed and released") {
    const TempDirectory temp;
    write_handoff_fixtures(temp);
    StartedSession session{temp, body_with_collision_sphere()};
    App::ScenarioStartupController& controller{session.controller};

    CHECK_FALSE(controller.area_transition_pending());
    CHECK(controller.area_transition_state() == App::AreaTransitionState::k_idle);

    // The source's 0x2F is accepted and its context waits in Runtime state 10.
    REQUIRE(controller.tick().has_value());
    CHECK(controller.area_transition_pending());
    CHECK(controller.area_transition_state() == App::AreaTransitionState::k_seamless_loading);
    CHECK_FALSE(controller.runtime_area_slot(1)->primary.has_value());

    // The next service prepares the alternate resident AREA, attaches its decor, resumes the
    // source (0x47/0x49/0x30) and the source's release returns the coordinator to idle.
    REQUIRE(controller.tick().has_value());
    CHECK_FALSE(controller.area_transition_pending());
    CHECK(controller.area_transition_state() == App::AreaTransitionState::k_idle);
    const App::RuntimeAreaSlot* const destination{controller.runtime_area_slot(1)};
    REQUIRE(destination != nullptr);
    CHECK_EQ(destination->primary_area_id, 222);
    CHECK_EQ(destination->scene_id, 55);
    CHECK_EQ(session.manager.world_contexts()[1].residency, WorldSceneResidencyState::ResidentAttached);
    CHECK_EQ(session.manager.world_contexts()[0].residency, WorldSceneResidencyState::ResidentDetached);
    // Preparation never changes the logical current AREA by itself.
    CHECK_EQ(controller.active_area_slot(), 0U);
  }

  TEST_CASE("a corrupt destination record rolls back to the untouched source") {
    const TempDirectory temp;
    write_handoff_fixtures(temp);
    std::vector<std::byte> area{make_handoff_area_archive(118, false, false)};
    write_u32(area, (222U * 8U) + 4U, 0x00FFFFFFU);
    write_bytes(temp.root() / "IAM" / "AREA", area);
    StartedSession session{temp};

    REQUIRE(session.controller.tick().has_value());
    const auto failed{session.controller.tick()};
    REQUIRE_FALSE(failed.has_value());
    CHECK_FALSE(failed.error().empty());
    CHECK_EQ(session.controller.runtime_area_slot(0)->primary_area_id, 118);
    CHECK_EQ(session.controller.runtime_area_slot(1)->primary_area_id, -1);
    CHECK_EQ(session.manager.world_contexts()[1].residency, WorldSceneResidencyState::Free);
    // The failure latches until the session is reset.
    CHECK_FALSE(session.controller.tick().has_value());
  }

  TEST_CASE("a destination AREA whose characters fail to materialize leaves no residue") {
    const TempDirectory temp;
    write_handoff_fixtures(temp, 118, false, true);
    std::vector<std::byte> area{make_handoff_area_archive(118, false, true)};
    // Break the table-4 definition's character id so the table-0 placement has no definition.
    write_u16(area, 0xC00U + 0xC8U + 0x110U, 999);
    write_bytes(temp.root() / "IAM" / "AREA", area);
    StartedSession session{temp, body_with_collision_sphere()};

    REQUIRE(session.controller.tick().has_value());
    REQUIRE_FALSE(session.controller.tick().has_value());
    CHECK_EQ(session.controller.runtime_area_slot(0)->primary_area_id, 118);
    CHECK_FALSE(session.controller.runtime_area_slot(1)->primary.has_value());
    CHECK_EQ(session.manager.world_contexts()[1].residency, WorldSceneResidencyState::Free);
    CHECK_EQ(session.controller.active_area_slot(), 0U);
  }

  TEST_CASE("a destination slot that is still current is refused before anything is destroyed") {
    const TempDirectory temp;
    // 0x30 -1 detaches the non-current resident (222), leaving 118 current, attached and owning the
    // body. AREA 222's own event then asks for AREA 142, whose destination slot is 118's.
    write_handoff_fixtures(temp, -1, true);
    StartedSession session{temp, body_with_collision_sphere()};
    App::ScenarioStartupController& controller{session.controller};

    REQUIRE(controller.tick().has_value());   // 118 accepts 0x2F -> 222.
    REQUIRE(controller.tick().has_value());   // 222 prepared, 118 resumes and releases 222.
    REQUIRE_EQ(controller.active_area_slot(), 0U);
    REQUIRE_EQ(controller.runtime_area_slot(0)->primary_area_id, 118);

    const auto refused{controller.tick()};
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().find("logical current AREA") != std::string::npos);
    CHECK_EQ(controller.runtime_area_slot(0)->primary_area_id, 118);
    CHECK(controller.runtime_area_slot(0)->primary.has_value());
    CHECK_EQ(session.manager.world_contexts()[0].residency, WorldSceneResidencyState::ResidentAttached);
    CHECK_EQ(controller.active_area_slot(), 0U);
  }

  TEST_CASE("a rich transition runs the pre script, then the post script after departure") {
    RichTransitionSession rich;
    rich.enter_zone_and_prepare();

    // The destination is attached beside the source; the zone requester stays in state 10.
    const App::RuntimeAreaSlot* const destination{rich.controller().runtime_area_slot(1)};
    REQUIRE(destination != nullptr);
    CHECK_EQ(destination->primary_area_id, 222);
    CHECK_EQ(rich.session->manager.world_contexts()[1].residency,
        WorldSceneResidencyState::ResidentAttached);
    CHECK_EQ(rich.requester().runtime_state(), 10U);
    REQUIRE_EQ(rich.source_world().script_runtime()->instances().size(), 1U);
    CHECK_EQ(rich.source_world().script_runtime()->instances().front().script_name, "PreCrossing");

    // The pre script finishing first waits for departure in state 7.
    rich.run_crossing_scripts();
    rich.tick();
    CHECK(rich.state() == App::AreaTransitionState::k_rich_pre_finished_first);
    CHECK_EQ(rich.requester().runtime_state(), 10U);

    rich.leave_zone(App::AreaTransitionState::k_rich_post_running);
    REQUIRE_EQ(rich.source_world().script_runtime()->instances().size(), 2U);
    CHECK_EQ(rich.source_world().script_runtime()->instances().back().script_name, "PostCrossing");
    CHECK_EQ(rich.requester().runtime_state(), 10U);

    // The post script finishing detaches the non-current resident and resumes the requester.
    rich.run_crossing_scripts();
    rich.tick();
    CHECK(rich.state() == App::AreaTransitionState::k_idle);
    CHECK_FALSE(rich.controller().area_transition_pending());
    CHECK_EQ(rich.controller().active_area_slot(), 0U);
    CHECK_EQ(rich.session->manager.world_contexts()[0].residency,
        WorldSceneResidencyState::ResidentAttached);
    CHECK_EQ(rich.session->manager.world_contexts()[1].residency,
        WorldSceneResidencyState::ResidentDetached);
    CHECK(rich.controller().runtime_area_slot(1)->primary.has_value());
  }

  TEST_CASE("departure before the pre script finishes starts the post script on its completion") {
    RichTransitionSession rich;
    rich.enter_zone_and_prepare();

    rich.leave_zone(App::AreaTransitionState::k_rich_departed_first);
    CHECK_EQ(rich.source_world().script_runtime()->instances().size(), 1U);

    rich.run_crossing_scripts();
    rich.tick();
    CHECK(rich.state() == App::AreaTransitionState::k_rich_post_running);
    REQUIRE_EQ(rich.source_world().script_runtime()->instances().size(), 2U);

    rich.run_crossing_scripts();
    rich.tick();
    CHECK(rich.state() == App::AreaTransitionState::k_idle);
    CHECK_EQ(rich.session->manager.world_contexts()[1].residency,
        WorldSceneResidencyState::ResidentDetached);
  }

  TEST_CASE("the phase watchdog resumes a rich transition after its running crossing script") {
    RichTransitionSession rich;
    rich.enter_zone_and_prepare();

    // Sixty seconds without the pre script finishing or a departure expire the phase.
    for (std::size_t second{0}; second < 61U; ++second) {
      rich.tick(1.0F);
      if (rich.state() == App::AreaTransitionState::k_idle) {
        break;
      }
    }
    REQUIRE(rich.state() == App::AreaTransitionState::k_idle);
    CHECK_FALSE(rich.controller().area_transition_pending());
    // The requester stays blocked until the pre script it was tracking finishes.
    CHECK_EQ(rich.requester().runtime_state(), 10U);
    CHECK_EQ(rich.source_world().script_runtime()->instances().size(), 1U);

    rich.run_crossing_scripts();
    rich.tick();
    CHECK(rich.requester().wait_info().kind != App::Script::AreaWaitKind::k_area_transition);
    CHECK_EQ(rich.session->manager.world_contexts()[1].residency,
        WorldSceneResidencyState::ResidentDetached);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

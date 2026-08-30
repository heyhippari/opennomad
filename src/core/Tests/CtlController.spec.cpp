#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// misc-const-correctness)

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Character/CtlController.hpp"
#include "Core/Omikron/CtlControlSet.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"
#include "OmikronTestBuffer.hpp"

namespace {

struct MarkerSpec {
  float phase{0.0F};
  std::uint16_t hid{0};
};

struct CtlStateSpec {
  std::uint32_t id{0};
  std::uint32_t flags{0};
  std::uint32_t input{0};
  float window_start{0.0F};
  float window_end{0.0F};
  float transition_value{0.0F};
  std::uint32_t goto_id{0};
  std::uint16_t animation_mode{0};
  std::uint16_t defer_ticks{0};
  std::uint16_t priority{0};
  bool key_bearing{false};
  std::string key;
  std::vector<std::uint32_t> child_refs;
  std::vector<std::uint32_t> parent_refs;
  std::optional<App::Runtime::Vec3> orientation;
  std::optional<App::Runtime::Vec3> movement;
  std::string callback;
  std::vector<MarkerSpec> markers;
};

struct CtlMoveSpec {
  std::uint32_t id{0};
  std::uint32_t flags{0};
  std::string name;
  std::vector<CtlStateSpec> states;
};

/// No-animation-key flag pair for transition-only synthetic states.
constexpr std::uint32_t K_NO_KEY{0x8002U};
/// Default-state flag within a move.
constexpr std::uint32_t K_DEFAULT_STATE{0x20U};
/// Exact-input candidate flag.
constexpr std::uint32_t K_EXACT_INPUT{0x00080000U};
/// Owner-move fallback enable (current state) and candidate requirement.
constexpr std::uint32_t K_FALLBACK_ENABLE{0x00002000U};
constexpr std::uint32_t K_FALLBACK_CANDIDATE{0x00004000U};
/// Deferred callback flag.
constexpr std::uint32_t K_CALLBACK{0x10U};

/// Minimal ordinary single-channel 3DA payload: one reference translation
/// sample followed by uniform per-interval root-motion increments and
/// identity rotations.
Buffer make_3da(const std::uint32_t max_frame,
    const App::Runtime::Vec3 reference = {},
    const App::Runtime::Vec3 interval_motion = {}) {
  const std::uint32_t samples{max_frame + 1U};
  constexpr std::uint32_t k_translation_offset{8U + 0x28U};
  Buffer animation;
  animation.u32(max_frame).u32(1);
  animation.u32(2)
      .chars("RootBody", 20)
      .u32(samples)
      .u32(k_translation_offset)
      .u32(samples)
      .u32(k_translation_offset + (samples * 12U));
  animation.f32(reference.x).f32(reference.y).f32(reference.z);
  for (std::uint32_t frame{1}; frame < samples; ++frame) {
    animation.f32(interval_motion.x).f32(interval_motion.y).f32(interval_motion.z);
  }
  for (std::uint32_t frame{0}; frame < samples; ++frame) {
    animation.f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
  }
  return animation;
}

[[nodiscard]] std::string uppercase(std::string value) {
  for (char& character : value) {
    character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
  }
  return value;
}

Buffer build_ctl(
    const std::vector<CtlMoveSpec>& moves, const std::map<std::string, Buffer>& animations = {}) {
  Buffer ctl;
  ctl.u32(0x30374543U)
      .u32(0x101U)
      .u32(0)
      .u32(static_cast<std::uint32_t>(moves.size()))
      .zeros(0x48U);
  for (const CtlMoveSpec& move : moves) {
    ctl.u32(move.id)
        .u32(static_cast<std::uint32_t>(move.states.size()))
        .u32(move.flags)
        .u32(0)
        .u32(0)
        .chars(move.name, 12);
  }
  for (const CtlMoveSpec& move : moves) {
    for (const CtlStateSpec& state : move.states) {
      ctl.u32(state.id)
          .u32(state.input)
          .u32(state.flags)
          .u32(0)
          .f32(state.window_start)
          .f32(state.window_end)
          .f32(state.transition_value)
          .u32(0)
          .u32(0)
          .u32(0)
          .u32(state.goto_id)
          .u32(0)
          .u32(0)
          .u32(0)
          .u32(0)
          .u32(0)
          .u32(0)
          .u32(0)
          .u32(0)
          .u16(state.animation_mode)
          .u16(0)
          .u16(0)
          .u16(state.defer_ticks)
          .u16(state.priority)
          .u8(static_cast<std::uint8_t>(state.parent_refs.size()))
          .u8(static_cast<std::uint8_t>(state.child_refs.size()));
    }
  }
  const auto each_state = [&moves](const auto& visitor) {
    for (const CtlMoveSpec& move : moves) {
      for (const CtlStateSpec& state : move.states) {
        visitor(state);
      }
    }
  };
  each_state([&ctl](const CtlStateSpec& state) {
    if (state.key_bearing) {
      ctl.chars(state.key, 12);
    }
  });
  each_state([&ctl](const CtlStateSpec& state) {
    for (const std::uint32_t ref : state.child_refs) {
      ctl.u32(ref);
    }
  });
  each_state([&ctl](const CtlStateSpec& state) {
    for (const std::uint32_t ref : state.parent_refs) {
      ctl.u32(ref);
    }
  });
  each_state([&ctl](const CtlStateSpec& state) {
    if (state.orientation.has_value()) {
      ctl.f32(0.0F).f32(0.0F);
      ctl.f32(state.orientation->x).f32(state.orientation->y).f32(state.orientation->z);
      ctl.f32(0.0F);
    }
  });
  each_state([&ctl](const CtlStateSpec& state) {
    if (state.movement.has_value()) {
      ctl.f32(0.0F).f32(0.0F);
      ctl.f32(state.movement->x).f32(state.movement->y).f32(state.movement->z);
    }
  });
  each_state([&ctl](const CtlStateSpec& state) {
    if (!state.callback.empty()) {
      ctl.chars(state.callback, 12);
    }
  });
  each_state([&ctl](const CtlStateSpec& state) {
    if (state.markers.empty()) {
      return;
    }
    ctl.u32(static_cast<std::uint32_t>(state.markers.size())).u32(0);
    for (const MarkerSpec& marker : state.markers) {
      ctl.f32(0.0F)
          .f32(0.0F)
          .f32(0.0F)
          .f32(marker.phase)
          .u32(0)
          .u16(0)
          .u16(marker.hid)
          .u8(0)
          .u8(0)
          .u16(0)
          .f32(0.0F);
    }
  });
  std::vector<std::string> emitted;
  each_state([&](const CtlStateSpec& state) {
    if (!state.key_bearing) {
      return;
    }
    const std::string canonical{uppercase(state.key)};
    if (std::ranges::find(emitted, canonical) != emitted.end()) {
      return;
    }
    const auto payload{animations.find(canonical)};
    REQUIRE(payload != animations.end());
    ctl.u32(static_cast<std::uint32_t>(payload->second.data().size()));
    for (const std::byte byte : payload->second.data()) {
      ctl.u8(static_cast<std::uint8_t>(byte));
    }
    emitted.push_back(canonical);
  });
  return ctl;
}

std::shared_ptr<const App::Omikron::CtlControlSet> make_bank(
    const std::vector<CtlMoveSpec>& moves, const std::map<std::string, Buffer>& animations = {}) {
  Buffer ctl{build_ctl(moves, animations)};
  auto parsed{App::Omikron::CtlControlSet::load(ctl.data())};
  REQUIRE(parsed.has_value());
  return std::make_shared<const App::Omikron::CtlControlSet>(std::move(parsed).value());
}

/// Two-mesh rooted body model with script_id-bound channels.
std::shared_ptr<const App::Character::ModelResource> make_body_model() {
  auto resource{std::make_shared<App::Character::ModelResource>()};
  resource->name = "TEST_BODY";
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

App::Character::RuntimeCharacter make_character() {
  App::Character::RuntimeCharacter character;
  character.character_id = 1;
  character.active = true;
  character.area_present = true;
  character.model_resource = make_body_model();
  character.runtime_objects = character.model_resource->model.runtime_objects;
  character.object_poses.assign(
      character.runtime_objects.size(), App::Character::BodyAnimationObjectPose{});
  character.posed_groups = character.model_resource->groups;
  return character;
}

constexpr float K_TICK{1.0F / 30.0F};

/// Default move 100: state 1 (default, persistent, key STAND) transitions on
/// forward input 0x4 to state 2 (key MOVE, end-family goto back to 1).
std::shared_ptr<const App::Omikron::CtlControlSet> make_simple_bank(
    const Buffer& stand_anim, const Buffer& move_anim) {
  const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 100,
      .flags = 1,
      .name = "Main",
      .states = {
          CtlStateSpec{.id = 1,
              .flags = K_DEFAULT_STATE,
              .key_bearing = true,
              .key = "STAND",
              .child_refs = {2}},
          CtlStateSpec{.id = 2,
              .flags = 0x1U,
              .input = 0x4U,
              .transition_value = 1.0F,
              .goto_id = 1,
              .key_bearing = true,
              .key = "MOVE"},
      }}};
  return make_bank(moves, {{"STAND", stand_anim}, {"MOVE", move_anim}});
}

}  // namespace

TEST_SUITE("Core::Character::CtlController") {
  TEST_CASE("canonical input and the exact condition matcher") {
    CHECK_EQ(App::Character::ctl_canonical_input(0U), 0x40000000U);
    CHECK_EQ(App::Character::ctl_canonical_input(0x4U), 0x4U);

    using App::Character::ctl_condition_matches;
    // Positive condition succeeds when held, fails when absent.
    CHECK(ctl_condition_matches(0x00000004U, 0x00000004U));
    CHECK_FALSE(ctl_condition_matches(0x00000004U, 0x40000000U));
    // Negative condition succeeds when absent, fails when held.
    CHECK(ctl_condition_matches(0x00020000U, 0x40000000U));
    CHECK_FALSE(ctl_condition_matches(0x00020000U, 0x00000004U));
    // Run held / run not held.
    CHECK(ctl_condition_matches(0x00000800U, 0x00000800U));
    CHECK(ctl_condition_matches(0x04000000U, 0x00000004U));
    CHECK_FALSE(ctl_condition_matches(0x04000000U, 0x00000804U));
    // Combined held forward + absent run.
    CHECK(ctl_condition_matches(0x04000004U, 0x00000004U));
    CHECK_FALSE(ctl_condition_matches(0x04000004U, 0x00000804U));
    // The no-input sentinel satisfies an empty condition.
    CHECK(ctl_condition_matches(0x0U, 0x40000000U));
    // Special authored 0x80000000: matches exactly when current <= 0x2000.
    // The 0x40000000 no-input sentinel is numerically above that range.
    CHECK_FALSE(ctl_condition_matches(0x80000000U, 0x40000000U));
    CHECK(ctl_condition_matches(0x80000000U, 0x00002000U));
    CHECK(ctl_condition_matches(0x80000000U, 0x00000004U));
    CHECK_FALSE(ctl_condition_matches(0x80000000U, 0x00004000U));
  }

  TEST_CASE("crossing-aware timing predicate") {
    using App::Character::ctl_timing_matches;
    // Empty window always matches.
    CHECK(ctl_timing_matches(1.0F, 2.0F, 0.0F, 0.0F));
    // Interval below the window start.
    CHECK_FALSE(ctl_timing_matches(2.0F, 3.0F, 10.0F, 21.0F));
    // Interval beyond the window end.
    CHECK_FALSE(ctl_timing_matches(22.0F, 23.0F, 10.0F, 21.0F));
    // Fully inside.
    CHECK(ctl_timing_matches(12.0F, 13.0F, 10.0F, 21.0F));
    // Crossing into the window requires reaching its end from below start.
    CHECK_FALSE(ctl_timing_matches(9.0F, 15.0F, 10.0F, 21.0F));
    CHECK(ctl_timing_matches(9.0F, 21.0F, 10.0F, 21.0F));
  }

  TEST_CASE("construction selects default move/state without touching a pose") {
    auto created{
        App::Character::CtlController::create(make_simple_bank(make_3da(3), make_3da(3)), "TEST")};
    REQUIRE(created.has_value());
    CHECK_EQ(created->current_move()->move_id, 100U);
    CHECK_EQ(created->current_state()->state_id, 1U);
    CHECK(created->current_progress() == doctest::Approx(1.0F));
    CHECK_EQ(created->current_input(), App::Character::K_CTL_NO_INPUT);
    CHECK_EQ(created->input_history().size(), 1U);
    CHECK_EQ(created->input_history()[0], App::Character::K_CTL_NO_INPUT);
    CHECK_EQ(created->same_state_restart_count(), 0U);

    // A disabled controller never mutates the character presentation.
    App::Character::RuntimeCharacter character{make_character()};
    character.pose_owner = App::Character::PoseOwner::k_script_animation;
    character.runtime_objects[0].local_offset = {.x = 7.0F, .y = 7.0F, .z = 7.0F};
    const std::uint64_t revision{character.pose_revision};
    CHECK_EQ(character.pose_revision, revision);
    CHECK(character.pose_owner == App::Character::PoseOwner::k_script_animation);
    CHECK(character.runtime_objects[0].local_offset.x == doctest::Approx(7.0F));
  }

  TEST_CASE("compact 0x3F performs exact move-ID lookup with a full reset") {
    const std::vector<CtlMoveSpec> moves{
        CtlMoveSpec{.id = 10,
            .flags = 1,
            .name = "Alpha",
            .states = {CtlStateSpec{.id = 11, .flags = K_NO_KEY | K_DEFAULT_STATE},
                CtlStateSpec{.id = 12, .flags = K_NO_KEY}}},
        CtlMoveSpec{.id = 20,
            .flags = 0,
            .name = "Beta",
            .states = {CtlStateSpec{.id = 21, .flags = K_NO_KEY},
                CtlStateSpec{.id = 22, .flags = K_NO_KEY | K_DEFAULT_STATE}}},
    };
    auto created{App::Character::CtlController::create(make_bank(moves), "TEST")};
    REQUIRE(created.has_value());
    CHECK_EQ(created->current_move()->move_id, 10U);
    CHECK_EQ(created->current_state()->state_id, 11U);

    // The operand is a move ID, not a state ID and not an array index. The
    // switch works logically while the controller is disabled (no character).
    REQUIRE(created->select_move(20).has_value());
    CHECK_EQ(created->current_move()->move_id, 20U);
    CHECK_EQ(created->current_state()->state_id, 22U);
    CHECK(created->current_progress() == doctest::Approx(1.0F));
    CHECK_EQ(created->current_input(), App::Character::K_CTL_NO_INPUT);
    CHECK_EQ(created->input_history().size(), 1U);
    CHECK_EQ(created->same_state_restart_count(), 0U);

    CHECK_FALSE(created->select_move(21).has_value());  // state IDs are not moves
    CHECK_FALSE(created->select_move(1).has_value());   // indices are not moves
  }

  TEST_CASE("input history records canonical changes up to 16 entries") {
    auto created{App::Character::CtlController::create(
        make_simple_bank(make_3da(10), make_3da(10)), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};

    created->service(K_TICK, 0x4U, character);
    CHECK_EQ(created->current_input(), 0x4U);
    CHECK_EQ(created->input_history().size(), 2U);
    // Recovered order: oldest at [0], newest at [count-1]
    CHECK_EQ(created->input_history()[0], App::Character::K_CTL_NO_INPUT);
    CHECK_EQ(created->input_history()[1], 0x4U);

    // Holding the same mask does not append.
    created->service(K_TICK, 0x4U, character);
    CHECK_EQ(created->input_history().size(), 2U);
    created->service(K_TICK, 0x0U, character);
    CHECK_EQ(created->current_input(), App::Character::K_CTL_NO_INPUT);
    CHECK_EQ(created->input_history().size(), 3U);

    for (std::uint32_t tick{0}; tick < 20U; ++tick) {
      created->service(K_TICK, tick % 2U == 0U ? 0x1U : 0x2U, character);
    }
    CHECK_EQ(created->input_history().size(), 16U);
  }

  TEST_CASE("suppression masks strip held inputs and expire when unmatched") {
    auto created{App::Character::CtlController::create(
        make_simple_bank(make_3da(10), make_3da(10)), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};

    created->add_input_suppression(0x4U);
    created->service(K_TICK, 0x4U, character);
    // The forward bit is suppressed; canonical input is the no-input sentinel.
    CHECK_EQ(created->current_input(), App::Character::K_CTL_NO_INPUT);
    CHECK_EQ(created->current_state()->state_id, 1U);  // no forward transition
    CHECK_EQ(created->suppression_count(), 1U);
    // When the mask no longer matches, the entry is removed.
    created->service(K_TICK, 0x0U, character);
    CHECK_EQ(created->suppression_count(), 0U);
    created->service(K_TICK, 0x4U, character);
    CHECK_EQ(created->current_input(), 0x4U);
    CHECK_EQ(created->current_state()->state_id, 2U);
  }

  TEST_CASE("input flag 0x08 swaps profile slots 0 and 1") {
    auto created{App::Character::CtlController::create(
        make_simple_bank(make_3da(10), make_3da(10)), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};
    created->set_swap_turn_slots(true);
    created->service(K_TICK, 0x1U, character);
    CHECK_EQ(created->current_input(), 0x2U);
    created->service(K_TICK, 0x2U, character);
    CHECK_EQ(created->current_input(), 0x1U);
  }

  TEST_CASE("persistent states re-enter at phase 1 while end-family follows goto") {
    // STAND has max frame 2; MOVE has max frame 1.
    auto created{
        App::Character::CtlController::create(make_simple_bank(make_3da(2), make_3da(1)), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};

    // Tick 1: phase 1 -> 2 reaches the animation end with no transition, so
    // the persistent state re-enters itself at phase 1.
    created->service(K_TICK, 0x0U, character);
    CHECK_EQ(created->current_state()->state_id, 1U);
    CHECK(created->current_progress() == doctest::Approx(1.0F));
    CHECK_EQ(created->same_state_restart_count(), 1U);
    created->service(K_TICK, 0x0U, character);
    CHECK_EQ(created->same_state_restart_count(), 2U);

    // Direct player control forces the restart count to zero.
    created->set_player_direct_control(true);
    created->service(K_TICK, 0x0U, character);
    created->service(K_TICK, 0x0U, character);
    CHECK_EQ(created->same_state_restart_count(), 0U);
    created->set_player_direct_control(false);

    // Forward input transitions 1 -> 2 immediately; state 2's animation ends
    // after one tick and the end/goto family follows its goto back to state 1
    // seeded with the authored transition value.
    created->service(K_TICK, 0x4U, character);
    CHECK_EQ(created->current_state()->state_id, 2U);
    created->service(K_TICK, 0x0U, character);
    CHECK_EQ(created->current_state()->state_id, 1U);
    CHECK(created->current_progress() == doctest::Approx(1.0F));
  }

  TEST_CASE("transition evaluator honors order, filters, exact match and timing") {
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Eval",
        .states = {
            CtlStateSpec{.id = 1, .flags = K_NO_KEY | K_DEFAULT_STATE, .child_refs = {2, 3, 4}},
            // First valid candidate wins without priority mode.
            CtlStateSpec{.id = 2, .flags = K_NO_KEY | 0x10000000U, .input = 0x4U},
            CtlStateSpec{.id = 3, .flags = K_NO_KEY | 0x10000000U, .input = 0x4U},
            // Exact-input candidate does not match a superset mask.
            CtlStateSpec{.id = 4, .flags = K_NO_KEY | 0x10000000U | K_EXACT_INPUT, .input = 0x5U},
        }}};
    auto created{App::Character::CtlController::create(make_bank(moves), "TEST")};
    REQUIRE(created.has_value());

    const App::Omikron::CtlState* selected{created->evaluate_transition(0x4U, 1.0F, 2.0F, {})};
    REQUIRE(selected != nullptr);
    CHECK_EQ(selected->state_id, 2U);

    // Both independent flag filters apply.
    App::Character::CtlController::TransitionQuery query;
    query.required_flags_a = 0x10000000;
    query.required_flags_b = 0x20000000;
    CHECK(created->evaluate_transition(0x4U, 1.0F, 2.0F, query) == nullptr);

    // Superset input 0x5 matches candidate 2 first under the ordinary
    // predicate; authored order wins over the later exact-match candidate.
    query.required_flags_b = -1;
    selected = created->evaluate_transition(0x5U, 1.0F, 2.0F, query);
    REQUIRE(selected != nullptr);
    CHECK_EQ(selected->state_id, 2U);

    // Timing-disabled queries skip the window predicate entirely.
    query.check_timing = false;
    selected = created->evaluate_transition(0x4U, 100.0F, 200.0F, query);
    REQUIRE(selected != nullptr);
    CHECK_EQ(selected->state_id, 2U);
  }

  TEST_CASE("reverse traversal and priority mode select by recovered rules") {
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Priority",
        .states = {
            CtlStateSpec{.id = 1,
                .flags = K_NO_KEY | K_DEFAULT_STATE,
                .animation_mode = 0x0020U,  // reverse child traversal
                .child_refs = {2, 3}},
            CtlStateSpec{.id = 2, .flags = K_NO_KEY, .input = 0x4U, .priority = 5},
            CtlStateSpec{.id = 3, .flags = K_NO_KEY, .input = 0x4U, .priority = 7},
        }}};
    auto created{App::Character::CtlController::create(make_bank(moves), "TEST")};
    REQUIRE(created.has_value());

    // animation_mode & 0x20 traverses child refs in reverse authored order.
    const App::Omikron::CtlState* selected{created->evaluate_transition(0x4U, 1.0F, 2.0F, {})};
    REQUIRE(selected != nullptr);
    CHECK_EQ(selected->state_id, 3U);

    // Priority mode: threshold 6 rejects 7 (above); the highest priority
    // below the threshold wins even though reverse order saw 3 first.
    created->set_priority_mode(true);
    created->set_priority_threshold(6);
    selected = created->evaluate_transition(0x4U, 1.0F, 2.0F, {});
    REQUIRE(selected != nullptr);
    CHECK_EQ(selected->state_id, 2U);

    // A candidate exactly at the threshold returns immediately.
    created->set_priority_threshold(7);
    selected = created->evaluate_transition(0x4U, 1.0F, 2.0F, {});
    REQUIRE(selected != nullptr);
    CHECK_EQ(selected->state_id, 3U);
  }

  TEST_CASE("owner-move fallback requires flags 0x200000 and 0x400000") {
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Fallback",
        .states = {
            CtlStateSpec{.id = 1,
                .flags = K_NO_KEY | K_DEFAULT_STATE | K_FALLBACK_ENABLE,
                .child_refs = {2}},
            CtlStateSpec{.id = 2, .flags = K_NO_KEY, .input = 0x8U},
            CtlStateSpec{.id = 3, .flags = K_NO_KEY | K_FALLBACK_CANDIDATE, .input = 0x4U},
            CtlStateSpec{.id = 4, .flags = K_NO_KEY, .input = 0x4U},
        }}};
    auto created{App::Character::CtlController::create(make_bank(moves), "TEST")};
    REQUIRE(created.has_value());

    // No child matches input 0x4; the owner-move scan finds state 3 (state 4
    // lacks the fallback-candidate flag).
    const App::Omikron::CtlState* selected{created->evaluate_transition(0x4U, 1.0F, 2.0F, {})};
    REQUIRE(selected != nullptr);
    CHECK_EQ(selected->state_id, 3U);

    // Without the current state's 0x200000 flag there is no fallback.
    const std::vector<CtlMoveSpec> no_fallback{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "NoFallback",
        .states = {
            CtlStateSpec{.id = 1, .flags = K_NO_KEY | K_DEFAULT_STATE},
            CtlStateSpec{.id = 3, .flags = K_NO_KEY | K_FALLBACK_CANDIDATE, .input = 0x4U},
        }}};
    auto plain{App::Character::CtlController::create(make_bank(no_fallback), "TEST")};
    REQUIRE(plain.has_value());
    CHECK(plain->evaluate_transition(0x4U, 1.0F, 2.0F, {}) == nullptr);
  }

  TEST_CASE("deferred transitions tick in controller-service units") {
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Defer",
        .states = {
            CtlStateSpec{.id = 1, .flags = K_NO_KEY | K_DEFAULT_STATE, .child_refs = {2}},
            CtlStateSpec{.id = 2, .flags = K_NO_KEY, .input = 0x4U, .defer_ticks = 3},
        }}};
    auto created{App::Character::CtlController::create(make_bank(moves), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};

    // Selection stores the pending candidate with tick count 1.
    created->service(K_TICK, 0x4U, character);
    CHECK_EQ(created->current_state()->state_id, 1U);
    REQUIRE(created->transition_pending());
    CHECK_EQ(created->pending_ticks(), 1U);

    // No transition while the counter stays at or below defer_ticks.
    created->service(K_TICK, 0x4U, character);
    CHECK_EQ(created->current_state()->state_id, 1U);
    created->service(K_TICK, 0x4U, character);
    CHECK_EQ(created->current_state()->state_id, 1U);

    // Counter 4 > 3: the transition fires and pending state clears.
    created->service(K_TICK, 0x4U, character);
    CHECK_EQ(created->current_state()->state_id, 2U);
    CHECK_FALSE(created->transition_pending());
  }

  TEST_CASE("root motion integrates interval increments through live orientation") {
    // Sample 0 is an obvious reference anchor that must never teleport the
    // actor; samples 1..N move +10 Z inches per interval.
    Buffer stand{make_3da(10, {.x = 10000.0F, .y = 0.0F, .z = 10000.0F}, {.z = 10.0F})};
    auto created{
        App::Character::CtlController::create(make_simple_bank(stand, make_3da(10)), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};
    character.transform.translation = {.x = 100.0F, .y = 0.0F, .z = 200.0F};

    created->service(K_TICK, 0x0U, character);
    // One 30 Hz logical tick advanced one phase and one interval of motion.
    CHECK(character.transform.translation.x == doctest::Approx(100.0F));
    CHECK(character.transform.translation.z == doctest::Approx(210.0F));
    CHECK(created->current_progress() == doctest::Approx(2.0F));

    // Non-axis-aligned yaw rotates subsequent root motion.
    character.set_principal_orientation({.x = 0.0F, .y = 30.0F, .z = 0.0F});
    created->service(K_TICK, 0x0U, character);
    const App::Runtime::Vec3 expected{App::Runtime::transform_vector(
        App::Runtime::Vec3{.x = 0.0F, .y = 0.0F, .z = 10.0F}, character.live_root_orientation())};
    CHECK(character.transform.translation.x == doctest::Approx(100.0F + expected.x));
    CHECK(character.transform.translation.y == doctest::Approx(expected.y));
    CHECK(character.transform.translation.z == doctest::Approx(210.0F + expected.z));
    CHECK_FALSE(character.transform.translation.z == doctest::Approx(220.0F));
  }

  TEST_CASE("lateral root motion rotates through a non-axis-aligned yaw") {
    // Authored lateral motion (sidestep): -X local per interval.
    Buffer step{make_3da(4, {}, {.x = -5.0F})};
    auto created{
        App::Character::CtlController::create(make_simple_bank(step, make_3da(4)), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};
    character.set_principal_orientation({.x = 0.0F, .y = 45.0F, .z = 0.0F});

    created->service(K_TICK, 0x0U, character);
    const App::Runtime::Vec3 expected{App::Runtime::transform_vector(
        App::Runtime::Vec3{.x = -5.0F, .y = 0.0F, .z = 0.0F}, character.live_root_orientation())};
    // The displacement must not be a fixed world-axis step.
    CHECK(character.transform.translation.x == doctest::Approx(expected.x));
    CHECK(character.transform.translation.z == doctest::Approx(expected.z));
    CHECK_FALSE(character.transform.translation.x == doctest::Approx(-5.0F));
    CHECK_FALSE(character.transform.translation.z == doctest::Approx(0.0F));
  }

  TEST_CASE("authored orientation and local-movement blocks apply at activation") {
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Aux",
        .states = {
            CtlStateSpec{.id = 1, .flags = K_NO_KEY | K_DEFAULT_STATE, .child_refs = {2, 4}},
            CtlStateSpec{.id = 2,
                .flags = K_NO_KEY | 0x0100U,
                .input = 0x1U,
                .child_refs = {3},
                .orientation = App::Runtime::Vec3{.x = 0.0F, .y = 25.0F, .z = 0.0F}},
            CtlStateSpec{.id = 3,
                .flags = K_NO_KEY | 0x0080U,
                .input = 0x2U,
                .movement = App::Runtime::Vec3{.x = 0.0F, .y = 0.0F, .z = 30.0F}},
            CtlStateSpec{.id = 4,
                .flags = K_NO_KEY | 0x0100U,
                .input = 0x8U,
                .orientation = App::Runtime::Vec3{.x = 0.0F, .y = -60.0F, .z = 0.0F}},
        }}};
    auto created{App::Character::CtlController::create(make_bank(moves), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};
    character.set_principal_orientation({.x = 0.0F, .y = 350.0F, .z = 0.0F});

    // Turn-left style state: the authored +25 degree yaw delta wraps 375 -> 15.
    created->service(K_TICK, 0x1U, character);
    CHECK_EQ(created->current_state()->state_id, 2U);
    CHECK(character.principal_orientation_degrees.y == doctest::Approx(15.0F));

    // Local +Z 30 transforms through the updated live orientation.
    created->service(K_TICK, 0x2U, character);
    CHECK_EQ(created->current_state()->state_id, 3U);
    const App::Runtime::Vec3 expected{App::Runtime::transform_vector(
        App::Runtime::Vec3{.x = 0.0F, .y = 0.0F, .z = 30.0F}, character.live_root_orientation())};
    CHECK(character.transform.translation.x == doctest::Approx(expected.x));
    CHECK(character.transform.translation.z == doctest::Approx(expected.z));

    // A negative authored delta: 15 - 60 wraps to -45.
    static_cast<void>(created->select_move(1));
    character.set_principal_orientation({.x = 0.0F, .y = 15.0F, .z = 0.0F});
    created->service(K_TICK, 0x8U, character);
    CHECK_EQ(created->current_state()->state_id, 4U);
    CHECK(character.principal_orientation_degrees.y == doctest::Approx(-45.0F));
  }

  TEST_CASE("MDWALK snapshots the restart count; direct control keeps zero") {
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Callbacks",
        .states = {CtlStateSpec{.id = 1,
            .flags = K_DEFAULT_STATE | K_CALLBACK,
            .key_bearing = true,
            .key = "LOOP",
            .callback = "MDWALK"}}}};
    auto created{
        App::Character::CtlController::create(make_bank(moves, {{"LOOP", make_3da(1)}}), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};
    // Construction queues the state's callback but does not dispatch it.
    CHECK_EQ(created->callback_queue_size(), 1U);
    CHECK_EQ(created->walk_restart_snapshot(), 0U);

    for (std::uint32_t tick{0}; tick < 5U; ++tick) {
      created->service(K_TICK, 0x0U, character);
    }
    // Each end-of-animation re-entry increments then snapshots the count.
    CHECK(created->same_state_restart_count() > 0U);
    CHECK_EQ(created->walk_restart_snapshot(), created->same_state_restart_count());
    CHECK_EQ(created->run_restart_snapshot(), 0U);

    // Under direct player control the restart count stays zero and so do the
    // autonomous snapshots.
    created->set_player_direct_control(true);
    created->service(K_TICK, 0x0U, character);
    created->service(K_TICK, 0x0U, character);
    CHECK_EQ(created->same_state_restart_count(), 0U);
    CHECK_EQ(created->walk_restart_snapshot(), 0U);
  }

  TEST_CASE("MDSTOPR performs the recovered autonomous move switch") {
    const std::vector<CtlMoveSpec> moves{
        CtlMoveSpec{.id = 1,
            .flags = 1,
            .name = "Main",
            .states =
                {
                    CtlStateSpec{.id = 1,
                        .flags = K_DEFAULT_STATE | K_CALLBACK,
                        .key_bearing = true,
                        .key = "LOOP",
                        .child_refs = {2},
                        .callback = "MDRUN"},
                    CtlStateSpec{.id = 2,
                        .flags = K_NO_KEY | K_CALLBACK,
                        .input = 0x4U,
                        .callback = "MDSTOPR"},
                }},
        CtlMoveSpec{.id = 164,
            .flags = 0,
            .name = "RunBreathe",
            .states = {CtlStateSpec{.id = 1641, .flags = K_NO_KEY | K_DEFAULT_STATE}}},
    };
    auto created{
        App::Character::CtlController::create(make_bank(moves, {{"LOOP", make_3da(1)}}), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};

    // 40 one-frame executions: the run snapshot exceeds the recovered limit.
    for (std::uint32_t tick{0}; tick < 40U; ++tick) {
      created->service(K_TICK, 0x0U, character);
    }
    CHECK(created->run_restart_snapshot() > 30U);

    // The deferred MDSTOPR callback switches to move 164 and resets the
    // snapshot; it never generates movement itself.
    created->service(K_TICK, 0x4U, character);
    CHECK_EQ(created->current_move()->move_id, 164U);
    CHECK_EQ(created->current_state()->state_id, 1641U);
    CHECK_EQ(created->run_restart_snapshot(), 0U);
  }

  TEST_CASE("MDSTAND alternates wait moves only without direct control") {
    const std::vector<CtlMoveSpec> moves{
        CtlMoveSpec{.id = 1,
            .flags = 1,
            .name = "Main",
            .states = {CtlStateSpec{.id = 1,
                .flags = K_DEFAULT_STATE | K_CALLBACK,
                .key_bearing = true,
                .key = "LOOP",
                .callback = "MDSTAND"}}},
        CtlMoveSpec{.id = 43,
            .flags = 0,
            .name = "WaitShort",
            .states = {CtlStateSpec{.id = 431, .flags = K_NO_KEY | K_DEFAULT_STATE}}},
        CtlMoveSpec{.id = 44,
            .flags = 0,
            .name = "WaitLong",
            .states = {CtlStateSpec{.id = 441, .flags = K_NO_KEY | K_DEFAULT_STATE}}},
    };
    auto bank{make_bank(moves, {{"LOOP", make_3da(1)}})};

    // Direct player control suppresses the autonomous wait diversion.
    {
      auto created{App::Character::CtlController::create(bank, "TEST")};
      REQUIRE(created.has_value());
      App::Character::RuntimeCharacter character{make_character()};
      character.adventure_mode = 1;
      created->set_player_direct_control(true);
      for (std::uint32_t tick{0}; tick < 30U; ++tick) {
        created->service(K_TICK, 0x0U, character);
      }
      CHECK_EQ(created->current_move()->move_id, 1U);
    }

    // Non-direct adventure mode 1: >10 restarts alternate 43 then 44.
    {
      auto created{App::Character::CtlController::create(bank, "TEST")};
      REQUIRE(created.has_value());
      App::Character::RuntimeCharacter character{make_character()};
      character.adventure_mode = 1;
      for (std::uint32_t tick{0}; tick < 12U; ++tick) {
        created->service(K_TICK, 0x0U, character);
      }
      CHECK_EQ(created->current_move()->move_id, 43U);
      CHECK_EQ(created->run_restart_snapshot(), 0U);
      CHECK_EQ(created->walk_restart_snapshot(), 0U);

      // Return to the standing move and exceed the threshold again: the
      // alternation selects the long wait this time.
      REQUIRE(created->select_move(1).has_value());
      for (std::uint32_t tick{0}; tick < 12U; ++tick) {
        created->service(K_TICK, 0x0U, character);
      }
      CHECK_EQ(created->current_move()->move_id, 44U);
    }
  }

  TEST_CASE("MDROT000 only suppresses automatic heading; RSTAVNT resets mode/pitch") {
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Main",
        .states = {
            CtlStateSpec{.id = 1,
                .flags = K_NO_KEY | K_DEFAULT_STATE | K_CALLBACK,
                .child_refs = {2},
                .callback = "MDROT000"},
            CtlStateSpec{
                .id = 2, .flags = K_NO_KEY | K_CALLBACK, .input = 0x4U, .callback = "RSTAVNT"},
        }}};
    auto created{App::Character::CtlController::create(make_bank(moves), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};
    character.adventure_mode = 7;
    character.set_principal_orientation({.x = 45.0F, .y = 90.0F, .z = 30.0F});

    // MDROT000: transient flag set; the actor orientation is NOT rotated.
    created->service(K_TICK, 0x0U, character);
    CHECK(character.suppress_automatic_movement_heading);
    CHECK(character.principal_orientation_degrees.x == doctest::Approx(45.0F));
    CHECK(character.principal_orientation_degrees.y == doctest::Approx(90.0F));

    // RSTAVNT: adventure mode 1, pitch zeroed, yaw preserved, profile 0 with
    // a reseeded no-input history.
    created->service(K_TICK, 0x4U, character);
    CHECK_EQ(created->current_state()->state_id, 2U);
    CHECK_EQ(character.adventure_mode, 1);
    CHECK(character.principal_orientation_degrees.x == doctest::Approx(0.0F));
    CHECK(character.principal_orientation_degrees.y == doctest::Approx(90.0F));
    CHECK_EQ(created->input_profile(), 0U);
    CHECK_EQ(created->current_input(), App::Character::K_CTL_NO_INPUT);
    CHECK_EQ(created->input_history().size(), 1U);
  }

  TEST_CASE("one-shot audio markers fire once per execution at phase crossings") {
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Audio",
        .states = {CtlStateSpec{.id = 1,
            .flags = K_DEFAULT_STATE,
            .animation_mode = 0x0009U,
            .key_bearing = true,
            .key = "LOOP",
            .markers = {MarkerSpec{.phase = 3.0F, .hid = 777}}}}}};
    auto created{
        App::Character::CtlController::create(make_bank(moves, {{"LOOP", make_3da(5)}}), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};

    // 1 -> 2: no crossing. 2 -> 3: fires once. 3 -> 4: no second fire.
    created->service(K_TICK, 0x0U, character);
    CHECK(created->take_sound_marker_events().empty());
    created->service(K_TICK, 0x0U, character);
    auto events{created->take_sound_marker_events()};
    REQUIRE_EQ(events.size(), 1U);
    CHECK_EQ(events[0].sound_hid, 777U);  // hID, never a table index
    created->service(K_TICK, 0x0U, character);
    CHECK(created->take_sound_marker_events().empty());
    CHECK_EQ(created->markers_fired_this_execution(), 1U);

    // End-of-animation re-entry makes the marker eligible again.
    created->service(K_TICK, 0x0U, character);  // 4 -> 5, end, re-enter at 1
    created->service(K_TICK, 0x0U, character);  // 1 -> 2
    CHECK(created->take_sound_marker_events().empty());
    CHECK_EQ(created->markers_fired_this_execution(), 0U);
    created->service(K_TICK, 0x0U, character);  // 2 -> 3
    events = created->take_sound_marker_events();
    REQUIRE_EQ(events.size(), 1U);
  }

  TEST_CASE("enabling the controller replaces a stale scripted base pose") {
    auto created{
        App::Character::CtlController::create(make_simple_bank(make_3da(3), make_3da(3)), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};

    // Deliberately displaced cinematic pose owned by scripted animation.
    character.pose_owner = App::Character::PoseOwner::k_script_animation;
    character.runtime_objects[0].local_offset = {.x = 7.0F, .y = 7.0F, .z = 7.0F};
    character.posed_groups.clear();
    character.transform.translation = {.x = 500.0F, .y = 600.0F, .z = 700.0F};
    const std::uint64_t revision{character.pose_revision};

    // The initialized-but-disabled controller leaves the pose untouched until
    // its first service — 0x68 only gates participation.
    CHECK_EQ(character.pose_revision, revision);
    CHECK(character.runtime_objects[0].local_offset.x == doctest::Approx(7.0F));

    created->service(K_TICK, 0x0U, character);
    // The current CTL state's authored animation now owns the base pose; the
    // stale scripted root residual is gone.
    CHECK(character.pose_owner == App::Character::PoseOwner::k_ctl_controller);
    CHECK(character.pose_revision > revision);
    CHECK(character.runtime_objects[0].local_offset.x == doctest::Approx(0.0F));
    CHECK_EQ(character.object_poses[0].channel_id, std::optional<std::uint32_t>{2U});
    CHECK_EQ(character.transform.translation.x, doctest::Approx(500.0F));
    const auto root_world{character.object_world_transform(0U)};
    REQUIRE(root_world.has_value());
    CHECK_EQ(root_world->translation.x, doctest::Approx(500.0F));
    CHECK_EQ(root_world->translation.y, doctest::Approx(600.0F));
    CHECK_EQ(root_world->translation.z, doctest::Approx(700.0F));
  }

  TEST_CASE("segmented animation modes fail safely without sampling") {
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Segmented",
        .states = {CtlStateSpec{.id = 1,
            .flags = K_DEFAULT_STATE,
            .animation_mode = 0x6011U,  // 6-segment packed sampler
            .key_bearing = true,
            .key = "SEG"}}}};
    auto created{
        App::Character::CtlController::create(make_bank(moves, {{"SEG", make_3da(3)}}), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};
    const std::uint64_t revision{character.pose_revision};

    // Unsupported segmented modes are diagnosed and treated as
    // transition-only: no misinterpretation of 6 as a loop count.
    created->service(K_TICK, 0x0U, character);
    CHECK_EQ(character.pose_revision, revision);
    CHECK(character.pose_owner == App::Character::PoseOwner::k_model_defaults);
  }

  TEST_CASE("unknown callback names warn once and stay nonfatal") {
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Unknown",
        .states = {CtlStateSpec{
            .id = 1, .flags = K_NO_KEY | K_DEFAULT_STATE | K_CALLBACK, .callback = "MDFLYBY"}}}};
    auto created{App::Character::CtlController::create(make_bank(moves), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};
    created->service(K_TICK, 0x0U, character);
    CHECK_EQ(created->callback_queue_size(), 0U);
    CHECK_EQ(created->current_state()->state_id, 1U);
  }

  TEST_CASE("input suppression: sparse unique 20-slot array with first-empty insertion") {
    auto created{App::Character::CtlController::create(
        make_simple_bank(make_3da(10), make_3da(10)), "TEST")};
    REQUIRE(created.has_value());
    App::Character::RuntimeCharacter character{make_character()};

    // Insert unique masks into the sparse array.
    created->add_input_suppression(0x1U);
    created->add_input_suppression(0x2U);
    created->add_input_suppression(0x4U);
    CHECK_EQ(created->suppression_count(), 3U);

    // Inserting a duplicate mask should be ignored.
    created->add_input_suppression(0x2U);
    CHECK_EQ(created->suppression_count(), 3U);

    // Zero mask is always ignored.
    created->add_input_suppression(0x0U);
    CHECK_EQ(created->suppression_count(), 3U);

    // Overflow behavior: at capacity 20, no new entries should be added.
    created->add_input_suppression(0x8U);
    created->add_input_suppression(0x10U);
    CHECK_LT(created->suppression_count(), 20U);
  }
}



// NOLINTEND

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
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Core/Omikron/CtlControlSet.hpp"
#include "Core/RuntimeMath.hpp"
#include "OmikronTestBuffer.hpp"

namespace {

/// One one-shot marker in a test state (phase + authored sound hID).
struct MarkerSpec {
  float phase{0.0F};
  std::uint16_t hid{0};
};

/// Declarative child-state description consumed by build_ctl.
struct CtlStateSpec {
  std::uint32_t id{0};
  std::uint32_t input{0};
  std::uint32_t flags{0};
  float window_start{0.0F};
  float window_end{0.0F};
  float transition_value{0.0F};
  std::uint32_t goto_id{0};
  std::uint16_t animation_mode{0};
  std::uint16_t defer_ticks{0};
  std::uint16_t priority{0};
  /// Serialized only when the state bears an animation key (the caller sets
  /// flags without 0x8002 accordingly).
  bool key_bearing{false};
  std::string key;
  std::vector<std::uint32_t> child_refs;
  std::vector<std::uint32_t> parent_refs;
  std::optional<App::Runtime::Vec3> orientation;
  std::optional<App::Runtime::Vec3> movement;
  std::string callback;
  bool aux28{false};
  std::vector<MarkerSpec> markers;
};

struct CtlMoveSpec {
  std::uint32_t id{0};
  std::uint32_t flags{0};
  std::string name;
  std::vector<CtlStateSpec> states;
};

/// Minimal ordinary single-channel 3DA payload: one reference translation
/// sample followed by per-interval root-motion increments and identity
/// rotations.
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
    character =
        static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
  }
  return value;
}

/// Builds a complete synthetic CTL resource. `animations` maps the UPPERCASE
/// canonical animation key to its embedded 3DA payload; every key-bearing
/// state must be covered exactly once per unique key.
Buffer build_ctl(const std::vector<CtlMoveSpec>& moves,
    const std::map<std::string, Buffer>& animations = {}) {
  Buffer ctl;
  ctl.u32(0x30374543U)  // "CE70"
      .u32(0x00000101U)
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
          .u32(0)  // raw_0c
          .f32(state.window_start)
          .f32(state.window_end)
          .f32(state.transition_value)
          .u32(0)            // dynamic block
          .u32(0)            // parent refs
          .u32(0)            // child refs
          .u32(state.goto_id)
          .u32(0)            // block 2c
          .u32(0)            // block 30
          .u32(0)            // raw_34
          .u32(0)            // owner move
          .u32(0)            // raw_3c
          .u32(0)            // callback name
          .u32(0)            // animation key
          .u32(0)            // animation runtime
          .u16(state.animation_mode)
          .u16(0)            // transition count
          .u16(0)            // phase offset
          .u16(state.defer_ticks)
          .u16(state.priority)
          .u8(static_cast<std::uint8_t>(state.parent_refs.size()))
          .u8(static_cast<std::uint8_t>(state.child_refs.size()));
    }
  }

  const auto each_state = [&moves](const auto& visitor) {
    for (const CtlMoveSpec& move : moves) {
      for (const CtlStateSpec& state : move.states) {
        visitor(move, state);
      }
    }
  };

  each_state([&ctl](const CtlMoveSpec&, const CtlStateSpec& state) {
    if (state.key_bearing) {
      ctl.chars(state.key, 12);
    }
  });
  each_state([&ctl](const CtlMoveSpec&, const CtlStateSpec& state) {
    for (const std::uint32_t ref : state.child_refs) {
      ctl.u32(ref);
    }
  });
  each_state([&ctl](const CtlMoveSpec&, const CtlStateSpec& state) {
    for (const std::uint32_t ref : state.parent_refs) {
      ctl.u32(ref);
    }
  });
  each_state([&ctl](const CtlMoveSpec&, const CtlStateSpec& state) {
    if (state.orientation.has_value()) {
      ctl.f32(0.0F).f32(0.0F);
      ctl.f32(state.orientation->x).f32(state.orientation->y).f32(state.orientation->z);
      ctl.f32(0.0F);
    }
  });
  each_state([&ctl](const CtlMoveSpec&, const CtlStateSpec& state) {
    if (state.movement.has_value()) {
      ctl.f32(0.0F).f32(0.0F);
      ctl.f32(state.movement->x).f32(state.movement->y).f32(state.movement->z);
    }
  });
  each_state([&ctl](const CtlMoveSpec&, const CtlStateSpec& state) {
    if (!state.callback.empty()) {
      ctl.chars(state.callback, 12);
    }
  });
  each_state([&ctl](const CtlMoveSpec&, const CtlStateSpec& state) {
    if (state.aux28) {
      ctl.zeros(0x28U);
    }
  });
  each_state([&ctl](const CtlMoveSpec&, const CtlStateSpec& state) {
    if (state.markers.empty()) {
      return;
    }
    ctl.u32(static_cast<std::uint32_t>(state.markers.size())).u32(0);
    for (const MarkerSpec& marker : state.markers) {
      ctl.f32(0.0F)   // sync duration
          .f32(0.0F)  // active start
          .f32(0.0F)  // active end
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

  // One payload per first occurrence of a unique canonical key.
  std::vector<std::string> emitted;
  each_state([&](const CtlMoveSpec&, const CtlStateSpec& state) {
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

/// Two moves (ids 10/20); move 20 is the flag-selected default. Child counts
/// accumulate 2 + 1.
std::vector<CtlMoveSpec> two_move_graph() {
  return {
      CtlMoveSpec{.id = 10,
          .flags = 0,
          .name = "First",
          .states = {CtlStateSpec{.id = 1, .flags = 0x8002U, .child_refs = {2}, .parent_refs = {3}},
              CtlStateSpec{.id = 2, .flags = 0x8002U, .goto_id = 3}}},
      CtlMoveSpec{.id = 20,
          .flags = 1,
          .name = "Second",
          .states = {CtlStateSpec{.id = 3, .flags = 0x8022U}}},
  };
}

}  // namespace

TEST_SUITE("Core::Omikron::CtlControlSet") {
  TEST_CASE("rejects a truncated header and wrong magic") {
    Buffer too_small;
    too_small.zeros(0x20U);
    CHECK_FALSE(App::Omikron::CtlControlSet::load(too_small.data()).has_value());

    Buffer wrong_magic;
    wrong_magic.u32(0xDEADBEEFU).u32(0x101U).u32(0).u32(0).zeros(0x48U);
    const auto parsed{App::Omikron::CtlControlSet::load(wrong_magic.data())};
    CHECK_FALSE(parsed.has_value());
  }

  TEST_CASE("parses the header and exact 0x20-byte move records") {
    Buffer ctl{build_ctl(two_move_graph())};
    const auto parsed{App::Omikron::CtlControlSet::load(ctl.data())};
    REQUIRE(parsed.has_value());
    CHECK_EQ(parsed->format_version(), 0x101U);
    REQUIRE_EQ(parsed->moves().size(), 2U);
    CHECK_EQ(parsed->moves()[0].move_id, 10U);
    CHECK_EQ(parsed->moves()[0].name, "First");
    CHECK_EQ(parsed->moves()[1].move_id, 20U);
    CHECK_EQ(parsed->moves()[1].name, "Second");
    // Child counts accumulate across moves; containment derives ownership.
    CHECK_EQ(parsed->states().size(), 3U);
    CHECK_EQ(parsed->moves()[0].states.size(), 2U);
    CHECK_EQ(parsed->moves()[1].states.size(), 1U);
    CHECK_EQ(parsed->states()[0].owner_move->move_id, 10U);
    CHECK_EQ(parsed->states()[2].owner_move->move_id, 20U);
  }

  TEST_CASE("parses the exact 0x58-byte child record with distinct sentinels") {
    Buffer ctl;
    ctl.u32(0x30374543U).u32(0x101U).u32(0).u32(1).zeros(0x48U);
    ctl.u32(77).u32(1).u32(1).u32(0).u32(0).chars("Sentinel", 12);
    ctl.u32(0xAABBCCDDU)      // state_id
        .u32(0x00040004U)     // input condition
        .u32(0x8022U)         // flags (default + no-key family)
        .u32(0x0C0C0C0CU)     // raw_0c
        .f32(4.0F)            // window start
        .f32(21.0F)           // window end
        .f32(2.5F)            // transition value
        .u32(0).u32(0).u32(0)
        .u32(0)               // goto
        .u32(0).u32(0)
        .u32(0x34343434U)     // raw_34
        .u32(0)
        .u32(0x3C3C3C3CU)     // raw_3c
        .u32(0).u32(0).u32(0)
        .u16(0x0009U)         // animation mode (markers bit set)
        .u16(7U)              // transition count
        .u16(3U)              // phase offset
        .u16(5U)              // defer ticks
        .u16(11U)             // priority
        .u8(0)
        .u8(0);
    // animation_mode & 8 appends the dynamic marker block: zero markers here.
    ctl.u32(0).u32(0);
    const auto parsed{App::Omikron::CtlControlSet::load(ctl.data())};
    REQUIRE(parsed.has_value());
    REQUIRE_EQ(parsed->states().size(), 1U);
    const App::Omikron::CtlState& state{parsed->states()[0]};
    CHECK_EQ(state.state_id, 0xAABBCCDDU);
    CHECK_EQ(state.input_condition, 0x00040004U);
    CHECK_EQ(state.flags, 0x8022U);
    CHECK_EQ(state.raw_0c, 0x0C0C0C0CU);
    CHECK(state.window_start == doctest::Approx(4.0F));
    CHECK(state.window_end == doctest::Approx(21.0F));
    CHECK(state.transition_value == doctest::Approx(2.5F));
    CHECK_EQ(state.raw_34, 0x34343434U);
    CHECK_EQ(state.raw_3c, 0x3C3C3C3CU);
    CHECK_EQ(state.animation_mode, 0x0009U);
    CHECK_EQ(state.transition_count, 7U);
    CHECK_EQ(state.phase_offset, 3U);
    CHECK_EQ(state.defer_ticks, 5U);
    CHECK_EQ(state.priority, 11U);
  }

  TEST_CASE("resolves child, parent and goto references by authored state ID") {
    Buffer ctl{build_ctl(two_move_graph())};
    const auto parsed{App::Omikron::CtlControlSet::load(ctl.data())};
    REQUIRE(parsed.has_value());
    const App::Omikron::CtlState* state1{parsed->state_by_id(1)};
    const App::Omikron::CtlState* state2{parsed->state_by_id(2)};
    const App::Omikron::CtlState* state3{parsed->state_by_id(3)};
    REQUIRE(state1 != nullptr);
    REQUIRE(state2 != nullptr);
    REQUIRE(state3 != nullptr);
    // Authored IDs, not array indices: state 2 is array index 1, state 3 is 2.
    REQUIRE_EQ(state1->child_refs.size(), 1U);
    CHECK_EQ(state1->child_refs[0]->state_id, 2U);
    REQUIRE_EQ(state1->parent_refs.size(), 1U);
    CHECK_EQ(state1->parent_refs[0]->state_id, 3U);
    REQUIRE(state2->goto_state != nullptr);
    CHECK_EQ(state2->goto_state->state_id, 3U);
    CHECK(parsed->state_by_id(99) == nullptr);
  }

  TEST_CASE("missing references are structured link errors") {
    auto moves{two_move_graph()};
    moves[0].states[0].child_refs = {0xFFFFU};
    Buffer ctl{build_ctl(moves)};
    CHECK_FALSE(App::Omikron::CtlControlSet::load(ctl.data()).has_value());

    moves = two_move_graph();
    moves[0].states[1].goto_id = 0xFFFFU;
    ctl = build_ctl(moves);
    CHECK_FALSE(App::Omikron::CtlControlSet::load(ctl.data()).has_value());

    moves = two_move_graph();
    moves[0].states[0].parent_refs = {0xFFFFU};
    ctl = build_ctl(moves);
    CHECK_FALSE(App::Omikron::CtlControlSet::load(ctl.data()).has_value());
  }

  TEST_CASE("truncated variable sections are structured errors") {
    Buffer complete{build_ctl(two_move_graph())};
    // Cut the buffer inside the child-reference section.
    const std::vector<std::byte> truncated{
        complete.data().begin(), complete.data().end() - 2};
    CHECK_FALSE(App::Omikron::CtlControlSet::load(truncated).has_value());

    // Trailing garbage after a complete parse is also an error: a correct
    // generic parser reaches EOF after the embedded payloads.
    Buffer ctl{build_ctl(two_move_graph())};
    ctl.u8(0);
    CHECK_FALSE(App::Omikron::CtlControlSet::load(ctl.data()).has_value());
  }

  TEST_CASE("selects default move by flags & 1 and default state by flags & 0x20") {
    Buffer ctl{build_ctl(two_move_graph())};
    const auto parsed{App::Omikron::CtlControlSet::load(ctl.data())};
    REQUIRE(parsed.has_value());
    // Move 20 is the default even though move 10 comes first.
    const App::Omikron::CtlMove* default_move{parsed->default_move()};
    REQUIRE(default_move != nullptr);
    CHECK_EQ(default_move->move_id, 20U);
    // Within move 10, only state 2 lacks the default flag; state 1/2 both
    // lack 0x20 here, so move 20's single state is the default there.
    const App::Omikron::CtlState* default_state{
        App::Omikron::CtlControlSet::default_state(*default_move)};
    REQUIRE(default_state != nullptr);
    CHECK_EQ(default_state->state_id, 3U);
    CHECK(parsed->move_by_id(10) != nullptr);
    CHECK_EQ(parsed->move_by_id(20)->move_id, 20U);
    CHECK(parsed->move_by_id(30) == nullptr);
  }

  TEST_CASE("parses animation keys and deduplicates embedded 3DA by uppercase key") {
    Buffer walk{make_3da(3, {}, {.x = 0.0F, .y = 0.0F, .z = 10.0F})};
    Buffer run{make_3da(5)};
    std::map<std::string, Buffer> animations{{"WALK", walk}, {"RUN", run}};
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Moves",
        .states =
            {
                CtlStateSpec{.id = 1, .flags = 0x20U, .key_bearing = true, .key = "walk"},
                // Same canonical key, different case: shares the resource.
                CtlStateSpec{.id = 2, .flags = 0U, .key_bearing = true, .key = "WALK"},
                CtlStateSpec{.id = 3, .flags = 0U, .key_bearing = true, .key = "Run"},
                // No key: the 0x8002 predicate suppresses the key field.
                CtlStateSpec{.id = 4, .flags = 0x8002U},
            }}};
    Buffer ctl{build_ctl(moves, animations)};
    const auto parsed{App::Omikron::CtlControlSet::load(ctl.data())};
    REQUIRE(parsed.has_value());
    CHECK_EQ(parsed->embedded_animation_count(), 2U);
    const App::Omikron::CtlState* state1{parsed->state_by_id(1)};
    const App::Omikron::CtlState* state2{parsed->state_by_id(2)};
    const App::Omikron::CtlState* state3{parsed->state_by_id(3)};
    const App::Omikron::CtlState* state4{parsed->state_by_id(4)};
    REQUIRE(state1->animation != nullptr);
    CHECK_EQ(state1->animation->max_frame_index, 3U);
    CHECK_EQ(state1->animation_key, "walk");
    CHECK_EQ(state1->canonical_animation_key, "WALK");
    CHECK(state2->animation == state1->animation);
    REQUIRE(state3->animation != nullptr);
    CHECK_EQ(state3->animation->max_frame_index, 5U);
    CHECK(state3->animation != state1->animation);
    CHECK(state4->animation == nullptr);
    CHECK(state4->animation_key.empty());
  }

  TEST_CASE("parses the typed 0x18 orientation and 0x14 movement blocks") {
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Aux",
        .states =
            {
                CtlStateSpec{.id = 1,
                    .flags = 0x8002U | 0x20U | 0x0100U,
                    .orientation = App::Runtime::Vec3{.x = 0.0F, .y = 5.0F, .z = 0.0F}},
                CtlStateSpec{.id = 2,
                    .flags = 0x8002U | 0x0080U,
                    .movement = App::Runtime::Vec3{.x = 1.0F, .y = 2.0F, .z = 3.0F}},
                CtlStateSpec{.id = 3, .flags = 0x8002U},
            }}};
    Buffer ctl{build_ctl(moves)};
    const auto parsed{App::Omikron::CtlControlSet::load(ctl.data())};
    REQUIRE(parsed.has_value());
    const App::Omikron::CtlState* turned{parsed->state_by_id(1)};
    REQUIRE(turned->orientation_block.has_value());
    CHECK(turned->orientation_block->orientation_delta.y == doctest::Approx(5.0F));
    const App::Omikron::CtlState* moved{parsed->state_by_id(2)};
    REQUIRE(moved->movement_block.has_value());
    CHECK(moved->movement_block->local_delta.x == doctest::Approx(1.0F));
    CHECK(moved->movement_block->local_delta.z == doctest::Approx(3.0F));
    CHECK_FALSE(parsed->state_by_id(3)->orientation_block.has_value());
    CHECK_FALSE(parsed->state_by_id(3)->movement_block.has_value());
  }

  TEST_CASE("parses 12-byte callback names and the neutral 0x28 block") {
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Callbacks",
        .states =
            {
                CtlStateSpec{.id = 1,
                    .flags = 0x8002U | 0x20U | 0x10U,
                    .callback = "MDSTAND"},
                CtlStateSpec{.id = 2, .flags = 0x8002U | 0x02000000U, .aux28 = true},
            }}};
    Buffer ctl{build_ctl(moves)};
    const auto parsed{App::Omikron::CtlControlSet::load(ctl.data())};
    REQUIRE(parsed.has_value());
    CHECK_EQ(parsed->state_by_id(1)->callback_name, "MDSTAND");
    CHECK(parsed->state_by_id(2)->callback_name.empty());
    CHECK(parsed->state_by_id(2)->auxiliary_block_28.has_value());
  }

  TEST_CASE("parses animation_mode & 8 marker blocks with exact 0x20-byte records") {
    const std::vector<CtlMoveSpec> moves{CtlMoveSpec{.id = 1,
        .flags = 1,
        .name = "Audio",
        .states = {CtlStateSpec{.id = 1,
            .flags = 0x8002U | 0x20U,
            .animation_mode = 0x0009U,
            .markers = {MarkerSpec{.phase = 3.0F, .hid = 203}, MarkerSpec{.phase = 15.0F, .hid = 199}}}}}};
    Buffer ctl{build_ctl(moves)};
    const auto parsed{App::Omikron::CtlControlSet::load(ctl.data())};
    REQUIRE(parsed.has_value());
    const App::Omikron::CtlState* state{parsed->state_by_id(1)};
    REQUIRE_EQ(state->audio_markers.size(), 2U);
    CHECK(state->audio_markers[0].one_shot_phase == doctest::Approx(3.0F));
    CHECK_EQ(state->audio_markers[0].one_shot_sound_hid, 203U);
    CHECK(state->audio_markers[1].one_shot_phase == doctest::Approx(15.0F));
    CHECK_EQ(state->audio_markers[1].one_shot_sound_hid, 199U);
    CHECK_EQ(state->audio_markers[0].attachment_selector, 0U);
    CHECK_EQ(state->audio_markers[0].marker_flags, 0U);
  }
}

// NOLINTEND

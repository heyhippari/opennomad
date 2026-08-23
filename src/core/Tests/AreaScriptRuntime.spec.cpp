#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// bugprone-unchecked-optional-access)

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Core/Audio/AudioTypes.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Script/AreaScriptOpcode.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "OmikronTestBuffer.hpp"

namespace {

using App::InterfaceCompletion;
using App::InterfaceHandle;
using App::InterfaceOpenRequest;
using App::Audio::MusicTrackRequest;
using App::Script::AreaCameraRequest;
using App::Script::AreaCharacterActivationRequest;
using App::Script::AreaCinematicLetterboxRequest;
using App::Script::AreaCharacterScriptLaunchMode;
using App::Script::AreaCharacterScriptRequest;
using App::Script::AreaDialogRequest;
using App::Script::AreaPresentationRequest;
using App::Script::AreaScriptRuntime;
using App::Script::AreaScriptState;
using App::Script::AreaScxScriptRequest;
using App::Script::AreaTransitionHandle;
using App::Script::AreaTransitionRequest;
using App::Script::AreaWaitKind;

/// The confirmed area-118 startup prefix (script-relative offsets 0..0x2C).
Buffer make_startup_prefix() {
  Buffer bytes;
  bytes.u8(0x0D).u16(175);                     // 0D AF 00
  bytes.u8(0x0E).u16(170).u8(50);              // 0E AA 00 32
  bytes.u8(0x38).u16(136);                     // 38 88 00
  bytes.u8(0x4F).u16(0xFFFF);                  // 4F FF FF
  bytes.u8(0x68);                              // 68
  bytes.u8(0x5C).u16(997);                     // 5C E5 03
  bytes.u8(0x83).u16(0).u16(1);                // 83 00 00 01 00
  bytes.u8(0x67).u16(109).u16(1).u16(1);       // 67 6D 00 01 00 01 00
  bytes.u8(0x76).u32(0).u16(0).u16(0);         // 76 00 00 00 00 00 00 00 00
  bytes.u8(0x46).u16(29).u16(0xFFFF).u16(19);  // 46 1D 00 FF FF 13 00
  return bytes;
}

/// A full two-music-opcode script: 0x67 (109,1,1), 0x46 (29,-1,19), 0x67 (87,1,1).
Buffer make_menu_music_script() {
  Buffer bytes;
  bytes.u8(0x67).u16(109).u16(1).u16(1);
  bytes.u8(0x46).u16(29).u16(0xFFFF).u16(19);
  bytes.u8(0x67).u16(87).u16(1).u16(1);
  return bytes;
}

/// Retail area-118 path through New Game up to the first event terminator.
Buffer make_new_game_event_script() {
  Buffer bytes{make_startup_prefix()};
  bytes.u8(0x67).u16(87).u16(1).u16(1);  // +0x2D
  bytes.u8(0x84);                        // +0x34 letterbox
  bytes.u8(0x07).u8(0);                  // +0x35 push 0
  bytes.u8(0x0A).u16(19);                // +0x37 push global 19
  bytes.u8(0x19);                        // +0x3A equal
  bytes.u8(0x06).u16(0x003B);            // +0x3B false -> +0x79
  bytes.u8(0x77).u32(0x00FFFFFFU).u16(30).u16(20);
  bytes.u8(0x5F).u16(2152).u16(0).u16(3);
  bytes.u8(0x39).u16(20).u16(0).u16(0);
  bytes.u8(0x5F).u16(2153).u16(0).u16(1);
  bytes.u8(0x5C).u16(753);
  bytes.u8(0x60).u16(2154).u16(100).u16(1);
  bytes.u8(0x76).u32(0x00FFFFFFU).u16(5).u16(0);
  bytes.u8(0x60).u16(2158).u16(25).u16(1);
  bytes.u8(0x04).u16(0x00A6);  // +0x76 -> +0x11F

  // Retail branch at +0x79, selected when interface-29 result != 0.
  bytes.u8(0x77).u32(0).u16(30).u16(0);      // +0x79
  bytes.u8(0x5F).u16(2172).u16(0).u16(2);    // +0x82
  bytes.u8(0x5F).u16(2148).u16(130).u16(2);  // +0x89
  bytes.u8(0x4E).u16(310).u16(1);            // +0x90
  bytes.u8(0x3C).u16(310).u16(1).u16(0);     // +0x95

  bytes.zeros(0x11FU - bytes.data().size());
  bytes.u8(0x03);  // +0x11F
  return bytes;
}

/// An interface sink that records the request and returns a fixed handle.
auto recording_interface_sink(
    std::optional<InterfaceOpenRequest>& captured, const InterfaceHandle handle) {
  return [&captured, handle](
             const InterfaceOpenRequest& request) -> std::expected<InterfaceHandle, std::string> {
    captured = request;
    return handle;
  };
}

}  // namespace

TEST_SUITE("Core::Script::AreaScriptRuntime") {
  TEST_CASE("0x39 launches SCX fire-and-forget and executes the following instruction") {
    Buffer bytes;
    bytes.u8(0x39).u16(20).u16(7).u16(9);
    bytes.u8(0x0D).u16(175);
    bytes.u8(0x03);

    AreaScriptRuntime runtime{bytes.data()};
    std::vector<AreaScxScriptRequest> requests;
    std::vector<std::uint32_t> instructions;
    runtime.set_scx_script_sink(
        [&requests](const AreaScxScriptRequest& request) -> std::expected<std::size_t, std::string> {
          requests.push_back(request);
          return 42U;
        });
    runtime.set_instruction_sink(
        [&instructions](const std::uint32_t opcode, const std::vector<std::int32_t>&) {
          instructions.push_back(opcode);
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    REQUIRE_EQ(requests.size(), 1U);
    CHECK_EQ(requests.at(0).script_id, 20U);
    CHECK_EQ(requests.at(0).operand_b, 7);
    CHECK_EQ(requests.at(0).operand_c, 9);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
    CHECK_EQ(runtime.wait_state(), 0U);
    CHECK_EQ(instructions, std::vector<std::uint32_t>{0x39U, 0x0DU, 0x03U});
  }

  TEST_CASE("0x3A tracks the exact SCX instance in Runtime state 4") {
    Buffer bytes;
    bytes.u8(0x3A).u16(6).u16(2).u16(3);
    bytes.u8(0x0D).u16(175);
    bytes.u8(0x03);

    AreaScriptRuntime runtime{bytes.data()};
    std::optional<AreaScxScriptRequest> request;
    std::vector<std::uint32_t> instructions;
    runtime.set_scx_script_sink(
        [&request](const AreaScxScriptRequest& value) -> std::expected<std::size_t, std::string> {
          request = value;
          return 77U;
        });
    runtime.set_instruction_sink(
        [&instructions](const std::uint32_t opcode, const std::vector<std::int32_t>&) {
          instructions.push_back(opcode);
        });
    runtime.queue_event(1);
    runtime.activate();

    REQUIRE(runtime.run() == AreaScriptState::k_waiting);
    REQUIRE(request.has_value());
    CHECK_EQ(request->script_id, 6U);
    CHECK_EQ(runtime.wait_state(), 4U);
    CHECK_EQ(runtime.runtime_state(), 4U);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_scx_script);
    CHECK_EQ(runtime.wait_info().scx_script_instance, std::optional<std::size_t>{77U});
    CHECK_EQ(instructions, std::vector<std::uint32_t>{0x3AU});

    REQUIRE_FALSE(runtime.complete_scx_script_wait(78U).has_value());
    CHECK(runtime.state() == AreaScriptState::k_waiting);
    REQUIRE(runtime.complete_scx_script_wait(77U).has_value());
    CHECK_EQ(runtime.runtime_state(), 1U);
    CHECK(runtime.run() == AreaScriptState::k_ready);
    CHECK_EQ(instructions, std::vector<std::uint32_t>{0x3AU, 0x0DU, 0x03U});
  }

  TEST_CASE("Opcodes 0x84 and 0x85 emit operand-less cinematic requests without waiting") {
    const App::Script::AreaOpcodeInfo* begin_info{App::Script::area_opcode_info(0x84)};
    const App::Script::AreaOpcodeInfo* end_info{App::Script::area_opcode_info(0x85)};
    REQUIRE(begin_info != nullptr);
    REQUIRE(end_info != nullptr);
    CHECK_EQ(begin_info->operand_count, 0U);
    CHECK_EQ(end_info->operand_count, 0U);

    Buffer bytes;
    bytes.u8(0x84).u8(0x85);
    AreaScriptRuntime runtime{bytes.data()};
    std::vector<bool> requests;
    runtime.set_cinematic_letterbox_sink(
        [&requests](const AreaCinematicLetterboxRequest& request) {
          requests.push_back(request.enabled);
        });

    runtime.queue_event(1);
    runtime.activate();
    CHECK(runtime.run() == AreaScriptState::k_completed);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
    CHECK_EQ(runtime.wait_state(), 0U);
    REQUIRE_EQ(requests.size(), 2U);
    CHECK(requests.at(0));
    CHECK_FALSE(requests.at(1));
    CHECK_FALSE(runtime.cinematic_letterbox_requested());
  }

  TEST_CASE("Opcode 0x84 emits exactly one enable request and keeps running") {
    Buffer bytes;
    bytes.u8(0x84);
    AreaScriptRuntime runtime{bytes.data()};
    std::size_t calls{0};
    bool enabled{false};
    runtime.set_cinematic_letterbox_sink(
        [&calls, &enabled](const AreaCinematicLetterboxRequest& request) {
          ++calls;
          enabled = request.enabled;
        });

    runtime.queue_event(1);
    runtime.activate();
    CHECK(runtime.run() == AreaScriptState::k_completed);
    CHECK(runtime.cinematic_letterbox_requested());
    CHECK(enabled);
    CHECK_EQ(calls, 1U);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
  }

  TEST_CASE("Opcode 0x85 emits exactly one disable request and keeps running") {
    Buffer bytes;
    bytes.u8(0x85);
    AreaScriptRuntime runtime{bytes.data()};
    std::size_t calls{0};
    bool enabled{true};
    runtime.set_cinematic_letterbox_sink(
        [&calls, &enabled](const AreaCinematicLetterboxRequest& request) {
          ++calls;
          enabled = request.enabled;
        });

    runtime.queue_event(1);
    runtime.activate();
    CHECK(runtime.run() == AreaScriptState::k_completed);
    CHECK_FALSE(runtime.cinematic_letterbox_requested());
    CHECK_FALSE(enabled);
    CHECK_EQ(calls, 1U);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
  }

  TEST_CASE("Opcode 0x4E dispatches once and continues without yielding") {
    Buffer bytes;
    bytes.u8(0x4E).u16(310).u16(1);
    AreaScriptRuntime runtime{bytes.data()};
    std::optional<AreaCharacterActivationRequest> captured;
    std::size_t calls{0};
    runtime.set_character_activation_sink(
        [&captured, &calls](const AreaCharacterActivationRequest& request)
            -> std::expected<void, std::string> {
          captured = request;
          ++calls;
          return {};
        });

    runtime.queue_event(1);
    runtime.activate();
    CHECK(runtime.run() == AreaScriptState::k_completed);
    REQUIRE(captured.has_value());
    CHECK_EQ(captured->character_id, 310);
    CHECK(captured->apply_area_transform);
    CHECK_EQ(calls, 1U);
    CHECK_EQ(runtime.instruction_pointer(), bytes.data().size());
  }

  TEST_CASE("Opcode 0x4E sink failure becomes a structured AREA failure") {
    Buffer bytes;
    bytes.u8(0x4E).u16(310).u16(1).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    runtime.set_character_activation_sink(
        [](const AreaCharacterActivationRequest&) -> std::expected<void, std::string> {
          return std::expected<void, std::string>{std::unexpect, "model missing"};
        });

    runtime.queue_event(1);
    runtime.activate();
    CHECK(runtime.run() == AreaScriptState::k_failed);
    CHECK_EQ(runtime.pause_info().opcode, 0x4EU);
    CHECK(runtime.pause_info().reason_text.find("model missing") != std::string::npos);
  }

  TEST_CASE("Opcode 0x4E derives the transform flag from operand 1") {
    Buffer bytes;
    bytes.u8(0x4E).u16(12).u16(0).u8(0x4E).u16(13).u16(7);
    AreaScriptRuntime runtime{bytes.data()};
    std::vector<AreaCharacterActivationRequest> captured;
    runtime.set_character_activation_sink(
        [&captured](const AreaCharacterActivationRequest& request)
            -> std::expected<void, std::string> {
          captured.push_back(request);
          return {};
        });

    runtime.queue_event(1);
    runtime.activate();
    CHECK(runtime.run() == AreaScriptState::k_completed);
    REQUIRE_EQ(captured.size(), 2U);
    CHECK_FALSE(captured.at(0).apply_area_transform);
    CHECK(captured.at(1).apply_area_transform);
  }

  TEST_CASE("Scripts remain inactive until an event is queued") {
    const Buffer bytes{make_startup_prefix()};
    AreaScriptRuntime runtime{bytes.data()};

    CHECK(runtime.state() == AreaScriptState::k_ready);
    static_cast<void>(runtime.run());
    CHECK(runtime.state() == AreaScriptState::k_ready);
    CHECK_EQ(runtime.instruction_pointer(), 0U);

    // Activation alone must not execute anything.
    runtime.activate();
    static_cast<void>(runtime.run());
    CHECK(runtime.state() == AreaScriptState::k_ready);
    CHECK_EQ(runtime.instruction_pointer(), 0U);
  }

  TEST_CASE("Queueing event 1 runs the prefix, plays 109 and opens interface 29") {
    const Buffer bytes{make_startup_prefix()};
    AreaScriptRuntime runtime{bytes.data()};

    std::optional<InterfaceOpenRequest> opened;
    std::optional<MusicTrackRequest> music;
    const InterfaceHandle menu_handle{.interface_id = 29, .generation = 7};
    runtime.set_interface_sink(recording_interface_sink(opened, menu_handle));
    runtime.set_music_sink([&music](const MusicTrackRequest& request) {
      music = request;
    });

    runtime.queue_event(1);
    runtime.activate();
    const AreaScriptState state{runtime.run()};

    CHECK(state == AreaScriptState::k_waiting);
    CHECK_EQ(runtime.wait_state(), 6U);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_interface);
    REQUIRE(runtime.wait_info().interface.has_value());
    CHECK(runtime.wait_info().interface.value() == menu_handle);

    REQUIRE(opened.has_value());
    CHECK_EQ(opened->interface_id, 29U);
    CHECK_EQ(opened->operand_b, -1);
    CHECK_EQ(opened->operand_c, 19);

    REQUIRE(music.has_value());
    CHECK_EQ(music->track_id, 109);
    CHECK(music->loop);
    CHECK_EQ(music->mode_flag, 1);

    CHECK_EQ(runtime.variable(175), std::optional<std::int32_t>{1});
    CHECK_EQ(runtime.variable(170), std::optional<std::int32_t>{50});
    CHECK_EQ(runtime.instruction_pointer(), 45U);
  }

  TEST_CASE("The startup prefix advances using the exact instruction boundaries") {
    const Buffer bytes{make_startup_prefix()};
    AreaScriptRuntime runtime{bytes.data()};
    runtime.queue_event(1);
    runtime.activate();
    static_cast<void>(runtime.run());

    const std::array<std::size_t, 10> expected{0, 3, 7, 10, 13, 14, 17, 22, 29, 38};
    REQUIRE_EQ(runtime.trace().size(), 10U);
    for (std::size_t index{0}; index < expected.size(); ++index) {
      CHECK_EQ(runtime.trace().at(index).offset, expected.at(index));
    }
  }

  TEST_CASE("0x67 consumes six operand bytes and emits a music request") {
    Buffer bytes;
    bytes.u8(0x67).u16(109).u16(1).u16(1).u8(0x00);  // unknown opcode next.
    AreaScriptRuntime runtime{bytes.data()};

    std::optional<MusicTrackRequest> music;
    runtime.set_music_sink([&music](const MusicTrackRequest& request) {
      music = request;
    });

    runtime.queue_event(1);
    runtime.activate();
    const AreaScriptState state{runtime.run()};

    CHECK(state == AreaScriptState::k_paused_unsupported);
    CHECK_EQ(runtime.pause_info().offset, 7U);
    CHECK_EQ(runtime.pause_info().opcode, 0x00U);
    REQUIRE(music.has_value());
    CHECK_EQ(music->track_id, 109);
  }

  TEST_CASE("0x76 consumes eight operand bytes") {
    Buffer bytes;
    bytes.u8(0x76).u32(0).u16(0).u16(0).u8(0x00);  // unknown opcode next.
    AreaScriptRuntime runtime{bytes.data()};
    runtime.queue_event(1);
    runtime.activate();
    const AreaScriptState state{runtime.run()};

    CHECK(state == AreaScriptState::k_paused_unsupported);
    CHECK_EQ(runtime.pause_info().offset, 9U);
    CHECK_EQ(runtime.pause_info().opcode, 0x00U);
  }

  TEST_CASE("A genuinely unhandled opcode produces PausedUnsupported") {
    Buffer bytes;
    bytes.u8(0x99).u8(0x00);
    AreaScriptRuntime runtime{bytes.data()};
    runtime.queue_event(1);
    runtime.activate();
    const AreaScriptState state{runtime.run()};

    CHECK(state == AreaScriptState::k_paused_unsupported);
    CHECK_EQ(runtime.pause_info().offset, 0U);
    CHECK_EQ(runtime.pause_info().opcode, 0x99U);
    CHECK_FALSE(runtime.pause_info().nearby_bytes.empty());
    CHECK(runtime.pause_info().reason_text.find("unhandled") != std::string::npos);
  }

  TEST_CASE("the menu sequence waits, then resumes and plays 87 after completion") {
    const Buffer bytes{make_menu_music_script()};
    AreaScriptRuntime runtime{bytes.data()};

    std::optional<InterfaceOpenRequest> opened;
    std::vector<MusicTrackRequest> music;
    const InterfaceHandle menu_handle{.interface_id = 29, .generation = 1};
    runtime.set_interface_sink(recording_interface_sink(opened, menu_handle));
    runtime.set_music_sink([&music](const MusicTrackRequest& request) {
      music.push_back(request);
    });

    runtime.queue_event(1);
    runtime.activate();

    // First run: 109 emitted, interface opened, script waiting; no 87 yet.
    CHECK(runtime.run() == AreaScriptState::k_waiting);
    REQUIRE_EQ(music.size(), 1U);
    CHECK_EQ(music.at(0).track_id, 109);
    REQUIRE(opened.has_value());
    CHECK_EQ(opened->interface_id, 29U);

    // A second run while waiting must not emit 87.
    CHECK(runtime.run() == AreaScriptState::k_waiting);
    CHECK_EQ(music.size(), 1U);

    // Deliver the matching completion: script resumes at the instruction
    // after opcode 0x46 (the request for 87).
    const InterfaceCompletion completion{.handle = menu_handle, .result = 0};
    REQUIRE(runtime.complete_interface_wait(completion).has_value());
    CHECK(runtime.state() == AreaScriptState::k_running);
    CHECK_EQ(runtime.completion_result(), std::optional<std::int16_t>{0});
    // Runtime writes interface results through operand 2 of opcode 0x46.
    CHECK_EQ(runtime.variable(19), std::optional<std::int32_t>{0});

    // The next run executes opcode 0x67 (87, 1, 1) then runs past the end.
    static_cast<void>(runtime.run());
    REQUIRE_EQ(music.size(), 2U);
    CHECK_EQ(music.at(1).track_id, 87);
    CHECK(music.at(1).loop);
    CHECK_EQ(music.at(1).mode_flag, 1);
    // The script consumed the whole 3-instruction stream.
    CHECK(runtime.state() == AreaScriptState::k_completed);
  }

  TEST_CASE("a mismatched or duplicate completion does not resume the script") {
    const Buffer bytes{make_menu_music_script()};
    AreaScriptRuntime runtime{bytes.data()};

    std::optional<InterfaceOpenRequest> opened;
    const InterfaceHandle menu_handle{.interface_id = 29, .generation = 1};
    runtime.set_interface_sink(recording_interface_sink(opened, menu_handle));

    runtime.queue_event(1);
    runtime.activate();
    CHECK(runtime.run() == AreaScriptState::k_waiting);

    // Stale generation: rejected, still waiting.
    const InterfaceCompletion stale{
        .handle = InterfaceHandle{.interface_id = 29, .generation = 99}, .result = 0};
    REQUIRE_FALSE(runtime.complete_interface_wait(stale).has_value());
    CHECK(runtime.state() == AreaScriptState::k_waiting);

    // Matching completion resumes exactly once.
    REQUIRE(runtime.complete_interface_wait(InterfaceCompletion{.handle = menu_handle, .result = 0})
            .has_value());
    CHECK(runtime.state() == AreaScriptState::k_running);

    // A duplicate matching completion is rejected (not waiting anymore).
    REQUIRE_FALSE(
        runtime.complete_interface_wait(InterfaceCompletion{.handle = menu_handle, .result = 0})
            .has_value());
  }

  TEST_CASE("0x3B requests an explicit-character script without blocking") {
    Buffer bytes;
    bytes.u8(0x3B).u16(310).u16(1).u16(0);

    AreaScriptRuntime runtime{bytes.data()};
    std::optional<AreaCharacterScriptRequest> captured;
    runtime.set_character_script_sink(
        [&captured](
            const AreaCharacterScriptRequest& request) -> std::expected<std::size_t, std::string> {
          captured = request;
          return 42U;
        });

    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_completed);
    CHECK_EQ(runtime.instruction_pointer(), 7U);
    REQUIRE(captured.has_value());
    CHECK_EQ(captured->character_id, 310);
    CHECK_EQ(captured->script_id, 1U);
    CHECK_EQ(captured->parameter, 0);
    CHECK(captured->mode == AreaCharacterScriptLaunchMode::k_fire_and_forget);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
  }

  TEST_CASE("0x3C blocks in Runtime state 4 on the explicit character-script request") {
    Buffer bytes;
    bytes.u8(0x3C).u16(310).u16(1).u16(0);

    AreaScriptRuntime runtime{bytes.data()};
    std::optional<AreaCharacterScriptRequest> captured;
    runtime.set_character_script_sink(
        [&captured](
            const AreaCharacterScriptRequest& request) -> std::expected<std::size_t, std::string> {
          captured = request;
          return 77U;
        });

    runtime.queue_event(1);
    runtime.activate();

    REQUIRE(runtime.run() == AreaScriptState::k_waiting);
    CHECK_EQ(runtime.instruction_pointer(), 7U);
    CHECK_EQ(runtime.wait_state(), 4U);
    CHECK_EQ(runtime.runtime_state(), 4U);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_character_script);

    REQUIRE(captured.has_value());
    CHECK_EQ(captured->character_id, 310);
    CHECK_EQ(captured->script_id, 1U);
    CHECK_EQ(captured->parameter, 0);
    CHECK(captured->mode == AreaCharacterScriptLaunchMode::k_tracked);

    REQUIRE(runtime.wait_info().character_script.has_value());
    CHECK_EQ(runtime.wait_info().character_script->character_id, 310);
    CHECK_EQ(runtime.wait_info().character_script->script_id, 1U);
    CHECK_EQ(runtime.wait_info().character_script_instance, std::optional<std::size_t>{77U});

    // A stale/mismatched completion must not release state 4.
    REQUIRE_FALSE(runtime.complete_character_script_wait(76U).has_value());
    CHECK(runtime.state() == AreaScriptState::k_waiting);
    CHECK_EQ(runtime.runtime_state(), 4U);

    // The exact concrete child instance drives the recovered 4 -> 1
    // control-flow transition.
    REQUIRE(runtime.complete_character_script_wait(77U).has_value());
    CHECK(runtime.state() == AreaScriptState::k_running);
    CHECK_EQ(runtime.runtime_state(), 1U);

    // Duplicate completion is rejected.
    REQUIRE_FALSE(runtime.complete_character_script_wait(77U).has_value());
  }

  TEST_CASE("0x2F waits in Runtime state 10 and resumes only for its exact transition") {
    const App::Script::AreaOpcodeInfo* info{App::Script::area_opcode_info(0x2F)};
    REQUIRE(info != nullptr);
    CHECK(info->support == App::Script::OpcodeSupport::k_supported);
    CHECK_FALSE(info->provisional);
    CHECK_EQ(info->operand_count, 3U);

    Buffer bytes;
    bytes.u8(0x2F).u16(222).u16(0xFFFF).u16(0xFFFF).u8(0x03);

    AreaScriptRuntime runtime{bytes.data()};
    std::vector<AreaTransitionRequest> requests;
    runtime.set_area_transition_sink(
        [&requests](const AreaTransitionRequest& request)
            -> std::expected<AreaTransitionHandle, std::string> {
          requests.push_back(request);
          return AreaTransitionHandle{.generation = 42};
        });
    runtime.queue_event(1);
    runtime.activate();

    REQUIRE(runtime.run() == AreaScriptState::k_waiting);
    REQUIRE_EQ(requests.size(), 1U);
    CHECK_EQ(requests.front().target_area_id, 222);
    CHECK_EQ(requests.front().operand_b, -1);
    CHECK_EQ(requests.front().operand_c, -1);
    CHECK_EQ(runtime.instruction_pointer(), 7U);
    CHECK_EQ(runtime.runtime_state(), 10U);
    CHECK_EQ(runtime.wait_state(), 10U);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_area_transition);
    CHECK_EQ(runtime.wait_info().area_transition_handle,
        std::optional<AreaTransitionHandle>{AreaTransitionHandle{.generation = 42}});
    REQUIRE(runtime.last_area_transition_request().has_value());
    CHECK_EQ(runtime.last_area_transition_request()->target_area_id, 222);

    // Waiting ticks do not redispatch the accepted request or execute 0x03.
    CHECK(runtime.run() == AreaScriptState::k_waiting);
    CHECK_EQ(requests.size(), 1U);
    REQUIRE_EQ(runtime.trace().size(), 1U);

    REQUIRE_FALSE(runtime.complete_area_transition(AreaTransitionHandle{.generation = 41})
                      .has_value());
    CHECK(runtime.state() == AreaScriptState::k_waiting);
    REQUIRE(runtime.complete_area_transition(AreaTransitionHandle{.generation = 42}).has_value());
    CHECK(runtime.state() == AreaScriptState::k_running);
    CHECK_EQ(runtime.instruction_pointer(), 7U);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);

    CHECK(runtime.run() == AreaScriptState::k_ready);
    CHECK_EQ(runtime.instruction_pointer(), 8U);
    CHECK_EQ(requests.size(), 1U);
  }

  TEST_CASE("0x2F without a transition bridge fails without advancing") {
    Buffer bytes;
    bytes.u8(0x2F).u16(222).u16(0xFFFF).u16(0xFFFF).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_failed);
    CHECK_EQ(runtime.instruction_pointer(), 0U);
    CHECK(runtime.pause_info().reason_text.find("AREA transition bridge is not wired") !=
          std::string::npos);
  }

  TEST_CASE("0x2F propagates coordinator rejection without executing its successor") {
    Buffer bytes;
    bytes.u8(0x2F).u16(222).u16(0xFFFF).u16(0xFFFF).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    std::size_t calls{0};
    runtime.set_area_transition_sink(
        [&calls](const AreaTransitionRequest&)
            -> std::expected<AreaTransitionHandle, std::string> {
          ++calls;
          return std::expected<AreaTransitionHandle, std::string>{
              std::unexpect, "coordinator is busy"};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_failed);
    CHECK_EQ(calls, 1U);
    CHECK_EQ(runtime.instruction_pointer(), 0U);
    CHECK(runtime.pause_info().reason_text.find("failed to begin AREA transition to 222") !=
          std::string::npos);
    CHECK(runtime.pause_info().reason_text.find("coordinator is busy") != std::string::npos);
    CHECK(runtime.trace().empty());
  }

  TEST_CASE("0x2F rejects unresolved transition variants and parameter references") {
    SUBCASE("non-default variant") {
      Buffer bytes;
      bytes.u8(0x2F).u16(222).u16(0).u16(0xFFFF);
      AreaScriptRuntime runtime{bytes.data()};
      runtime.queue_event(1);
      runtime.activate();
      CHECK(runtime.run() == AreaScriptState::k_failed);
      CHECK(runtime.pause_info().reason_text.find("transition variant (0, -1)") !=
            std::string::npos);
    }
    SUBCASE("parameter-indirected target") {
      Buffer bytes;
      bytes.u8(0x2F).u16(0x4002).u16(0xFFFF).u16(0xFFFF);
      AreaScriptRuntime runtime{bytes.data()};
      runtime.queue_event(1);
      runtime.activate();
      CHECK(runtime.run() == AreaScriptState::k_failed);
      CHECK(runtime.pause_info().reason_text.find("parameter-indirected Scalar16") !=
            std::string::npos);
    }
  }

  TEST_CASE("0x2F reports truncated three-Scalar16 operands") {
    Buffer bytes;
    bytes.u8(0x2F).u16(222).u16(0xFFFF);
    AreaScriptRuntime runtime{bytes.data()};
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_failed);
    CHECK_EQ(runtime.instruction_pointer(), 0U);
    CHECK(runtime.pause_info().reason_text.find("truncated operands") != std::string::npos);
  }

  TEST_CASE("0x3D starts one dialog, yields while running, and resumes at the advanced IP") {
    const App::Script::AreaOpcodeInfo* info{App::Script::area_opcode_info(0x3D)};
    REQUIRE(info != nullptr);
    CHECK(info->support == App::Script::OpcodeSupport::k_supported);
    CHECK_FALSE(info->provisional);
    CHECK_EQ(info->operand_count, 1U);
    const std::span<const App::Script::AreaOperandWidth> widths{
        info->operands, info->operand_count};
    CHECK(widths.front() == App::Script::AreaOperandWidth::k_int16);

    Buffer bytes;
    bytes.u8(0x3D).u16(272).u8(0x68).u8(0x03);

    AreaScriptRuntime runtime{bytes.data()};
    std::vector<AreaDialogRequest> requests;
    runtime.set_dialog_sink(
        [&requests](const AreaDialogRequest& request) -> std::expected<void, std::string> {
          requests.push_back(request);
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    REQUIRE(runtime.run() == AreaScriptState::k_running);
    REQUIRE_EQ(requests.size(), 1U);
    CHECK_EQ(requests.front().dialog_id, 272);
    CHECK_EQ(runtime.instruction_pointer(), 3U);
    CHECK_EQ(runtime.runtime_state(), 1U);
    CHECK(runtime.last_run_yielded());
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
    CHECK_EQ(runtime.wait_state(), 0U);
    REQUIRE_EQ(runtime.trace().size(), 1U);
    CHECK_EQ(runtime.trace().back().effect, "start dialog 272 and yield");

    // No VM wait completion is needed: the next invocation naturally starts
    // at +3, executes 0x68 exactly once, then terminates the event at +4.
    REQUIRE(runtime.run() == AreaScriptState::k_ready);
    CHECK_EQ(runtime.instruction_pointer(), 5U);
    CHECK_FALSE(runtime.last_run_yielded());
    CHECK_EQ(requests.size(), 1U);
    REQUIRE_EQ(runtime.trace().size(), 3U);
    CHECK_EQ(runtime.trace().at(1).offset, 3U);
    CHECK_EQ(runtime.trace().at(1).opcode, 0x68U);
  }

  TEST_CASE("0x3D without a dialog bridge is a structured execution failure") {
    Buffer bytes;
    bytes.u8(0x3D).u16(272).u8(0x68);

    AreaScriptRuntime runtime{bytes.data()};
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_failed);
    CHECK_EQ(runtime.instruction_pointer(), 0U);
    CHECK_EQ(runtime.pause_info().opcode, 0x3DU);
    CHECK(runtime.pause_info().reason_text.find("dialog bridge is not wired") != std::string::npos);
    CHECK(runtime.pause_info().reason_text.find("unsupported opcode") == std::string::npos);
  }

  TEST_CASE("0x3D propagates dialog-start failure without executing the next instruction") {
    Buffer bytes;
    bytes.u8(0x3D).u16(272).u8(0x68);

    AreaScriptRuntime runtime{bytes.data()};
    std::size_t calls{0};
    runtime.set_dialog_sink([&calls](const AreaDialogRequest&) -> std::expected<void, std::string> {
      ++calls;
      return std::expected<void, std::string>{std::unexpect, "IAM/DIALOG record 272 is corrupt"};
    });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_failed);
    CHECK_EQ(calls, 1U);
    CHECK_EQ(runtime.instruction_pointer(), 0U);
    CHECK(runtime.pause_info().reason_text.find("failed to start dialog 272") != std::string::npos);
    CHECK(runtime.pause_info().reason_text.find("IAM/DIALOG record 272 is corrupt") !=
          std::string::npos);
    CHECK(runtime.trace().empty());
  }

  TEST_CASE("0x3D rejects an unresolved parameter-indirected Scalar16") {
    Buffer bytes;
    bytes.u8(0x3D).u16(0x4002).u8(0x68);

    AreaScriptRuntime runtime{bytes.data()};
    std::size_t calls{0};
    runtime.set_dialog_sink([&calls](const AreaDialogRequest&) -> std::expected<void, std::string> {
      ++calls;
      return {};
    });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_failed);
    CHECK_EQ(calls, 0U);
    CHECK(runtime.pause_info().reason_text.find("parameter-indirected Scalar16") !=
          std::string::npos);
    CHECK(runtime.pause_info().reason_text.find("parameter block is not modeled") !=
          std::string::npos);
  }

  TEST_CASE("The result-zero AREA branch executes through its first event terminator") {
    const Buffer bytes{make_new_game_event_script()};
    AreaScriptRuntime runtime{bytes.data()};

    const InterfaceHandle menu_handle{.interface_id = 29, .generation = 1};
    std::vector<AreaScxScriptRequest> scripts;
    runtime.set_interface_sink(
        [menu_handle](const InterfaceOpenRequest&) -> std::expected<InterfaceHandle, std::string> {
          return menu_handle;
        });
    runtime.set_scx_script_sink(
        [&scripts](const AreaScxScriptRequest& request) -> std::expected<std::size_t, std::string> {
          scripts.push_back(request);
          return 42U;
        });

    runtime.queue_event(1);
    runtime.activate();
    REQUIRE(runtime.run() == AreaScriptState::k_waiting);
    REQUIRE(runtime.complete_interface_wait(InterfaceCompletion{.handle = menu_handle, .result = 0})
            .has_value());

    // Result 0 keeps execution on the fall-through branch. Runtime's
    // presentation helpers set the context-yield flag.
    REQUIRE(runtime.run() == AreaScriptState::k_running);
    CHECK(runtime.cinematic_letterbox_requested());
    REQUIRE(runtime.last_presentation_request().has_value());
    CHECK_EQ(runtime.last_presentation_request()->mode, 2U);

    // Camera 2152 similarly yields one AREA tick.
    REQUIRE(runtime.run() == AreaScriptState::k_running);
    REQUIRE(runtime.last_camera_request().has_value());
    CHECK_EQ(runtime.last_camera_request()->camera_id, 2152U);

    // The next tick launches Wait5sec independently, continues through camera
    // 2153 in the same invocation, and yields only for that camera command.
    REQUIRE(runtime.run() == AreaScriptState::k_running);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
    REQUIRE_EQ(scripts.size(), 1U);
    CHECK_EQ(scripts.at(0).script_id, 20U);
    REQUIRE(runtime.last_camera_request().has_value());
    CHECK_EQ(runtime.last_camera_request()->camera_id, 2153U);

    REQUIRE(runtime.run() == AreaScriptState::k_waiting);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_camera);
    REQUIRE(runtime.last_camera_request().has_value());
    CHECK_EQ(runtime.last_camera_request()->camera_id, 2154U);
    CHECK_EQ(runtime.wait_state(), 7U);

    // Camera 2154 waits 100 scenario frames. The resumed tick reaches 0x76,
    // records presentation mode 1, and yields before camera 2158.
    REQUIRE(runtime.run(100.0F / 30.0F) == AreaScriptState::k_running);
    REQUIRE(runtime.last_presentation_request().has_value());
    CHECK_EQ(runtime.last_presentation_request()->mode, 1U);

    REQUIRE(runtime.run() == AreaScriptState::k_waiting);
    REQUIRE(runtime.last_camera_request().has_value());
    CHECK_EQ(runtime.last_camera_request()->camera_id, 2158U);

    // The final timed wait resumes into 0x04, which lands exactly on +0x11F
    // and executes the event terminator 0x03.
    CHECK(runtime.run(25.0F / 30.0F) == AreaScriptState::k_ready);
    CHECK_EQ(runtime.instruction_pointer(), 0x120U);
    CHECK_EQ(runtime.evaluation_stack_depth(), 0U);
  }

  TEST_CASE("Retail New Game result 3 reaches tracked character script 310/1 and blocks") {
    const Buffer bytes{make_new_game_event_script()};
    AreaScriptRuntime runtime{bytes.data()};

    const InterfaceHandle menu_handle{.interface_id = 29, .generation = 1};
    runtime.set_interface_sink(
        [menu_handle](const InterfaceOpenRequest&) -> std::expected<InterfaceHandle, std::string> {
          return menu_handle;
        });

    std::optional<AreaCharacterScriptRequest> character_script;
    runtime.set_character_script_sink(
        [&character_script](
            const AreaCharacterScriptRequest& request) -> std::expected<std::size_t, std::string> {
          character_script = request;
          return 77U;
        });

    runtime.queue_event(1);
    runtime.activate();
    REQUIRE(runtime.run() == AreaScriptState::k_waiting);

    REQUIRE(runtime.complete_interface_wait(InterfaceCompletion{.handle = menu_handle, .result = 3})
            .has_value());
    CHECK_EQ(runtime.variable(19), std::optional<std::int32_t>{3});

    // global[19] != 0 makes opcode 0x06 branch to +0x79.
    // 0x77 and both 0x5F operations each yield one AREA tick.
    REQUIRE(runtime.run() == AreaScriptState::k_running);
    REQUIRE(runtime.last_presentation_request().has_value());
    CHECK_EQ(runtime.last_presentation_request()->mode, 2U);

    REQUIRE(runtime.run() == AreaScriptState::k_running);
    REQUIRE(runtime.last_camera_request().has_value());
    CHECK_EQ(runtime.last_camera_request()->camera_id, 2172U);

    REQUIRE(runtime.run() == AreaScriptState::k_running);
    REQUIRE(runtime.last_camera_request().has_value());
    CHECK_EQ(runtime.last_camera_request()->camera_id, 2148U);

    // 0x4E does not yield, so the same AREA tick continues into 0x3C and
    // stores the concrete child before stopping in recovered Runtime state 4.
    REQUIRE(runtime.run() == AreaScriptState::k_waiting);
    CHECK_EQ(runtime.instruction_pointer(), 0x9CU);
    CHECK_EQ(runtime.wait_state(), 4U);
    CHECK_EQ(runtime.runtime_state(), 4U);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_character_script);

    REQUIRE(runtime.last_character_activation_request().has_value());
    CHECK_EQ(runtime.last_character_activation_request()->character_id, 310);
    CHECK(runtime.last_character_activation_request()->apply_area_transform);

    REQUIRE(character_script.has_value());
    CHECK_EQ(character_script->character_id, 310);
    CHECK_EQ(character_script->script_id, 1U);
    CHECK_EQ(character_script->parameter, 0);
    CHECK(character_script->mode == AreaCharacterScriptLaunchMode::k_tracked);
    CHECK_EQ(runtime.wait_info().character_script_instance, std::optional<std::size_t>{77U});
  }

  TEST_CASE("A failed character-script launch enters structured failure once") {
    Buffer bytes;
    bytes.u8(0x3C).u16(310).u16(1).u16(0);

    AreaScriptRuntime runtime{bytes.data()};
    std::size_t calls{0};
    runtime.set_character_script_sink(
        [&calls](const AreaCharacterScriptRequest&) -> std::expected<std::size_t, std::string> {
          ++calls;
          return std::expected<std::size_t, std::string>{
              std::unexpect, "runtime character 310 does not exist"};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_failed);
    CHECK_EQ(calls, 1U);
    CHECK(runtime.pause_info().reason_text.find("runtime character 310 does not exist") !=
          std::string::npos);
    CHECK_FALSE(runtime.wait_info().character_script_instance.has_value());
    CHECK(runtime.run() == AreaScriptState::k_failed);
    CHECK_EQ(calls, 1U);
  }

  TEST_CASE("The opcode registry knows the recovered New Game VM primitives") {
    CHECK(App::Script::area_opcode_name(0x03) != nullptr);
    CHECK(App::Script::area_opcode_name(0x04) != nullptr);
    CHECK(App::Script::area_opcode_name(0x06) != nullptr);
    CHECK(App::Script::area_opcode_name(0x07) != nullptr);
    CHECK(App::Script::area_opcode_name(0x0A) != nullptr);
    CHECK(App::Script::area_opcode_name(0x0D) != nullptr);
    CHECK(App::Script::area_opcode_name(0x19) != nullptr);
    CHECK(App::Script::area_opcode_name(0x2F) != nullptr);
    CHECK(App::Script::area_opcode_name(0x39) != nullptr);
    CHECK(App::Script::area_opcode_name(0x3A) != nullptr);
    CHECK(App::Script::area_opcode_name(0x3B) != nullptr);
    CHECK(App::Script::area_opcode_name(0x3C) != nullptr);
    CHECK(App::Script::area_opcode_name(0x3D) != nullptr);
    CHECK(App::Script::area_opcode_name(0x4E) != nullptr);
    CHECK(App::Script::area_opcode_name(0x46) != nullptr);
    CHECK(App::Script::area_opcode_name(0x5F) != nullptr);
    CHECK(App::Script::area_opcode_name(0x60) != nullptr);
    CHECK(App::Script::area_opcode_name(0x67) != nullptr);
    CHECK(App::Script::area_opcode_name(0x77) != nullptr);
    CHECK(App::Script::area_opcode_name(0x84) != nullptr);
    CHECK(App::Script::area_opcode_name(0x85) != nullptr);
    CHECK(App::Script::area_opcode_name(0x99) == nullptr);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// bugprone-unchecked-optional-access)

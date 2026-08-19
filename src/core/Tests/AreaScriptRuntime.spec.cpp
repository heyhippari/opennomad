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

using App::Script::AreaScriptRuntime;
using App::Script::AreaScriptState;
using App::Script::AreaWaitKind;
using App::Audio::MusicTrackRequest;
using App::InterfaceHandle;
using App::InterfaceOpenRequest;
using App::InterfaceCompletion;

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

/// An interface sink that records the request and returns a fixed handle.
auto recording_interface_sink(std::optional<InterfaceOpenRequest>& captured,
    const InterfaceHandle handle) {
  return [&captured, handle](const InterfaceOpenRequest& request)
             -> std::expected<InterfaceHandle, std::string> {
    captured = request;
    return handle;
  };
}

}  // namespace

TEST_SUITE("Core::Script::AreaScriptRuntime") {
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
    runtime.set_music_sink(
        [&music](const MusicTrackRequest& request) { music = request; });

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
    runtime.set_music_sink(
        [&music](const MusicTrackRequest& request) { music = request; });

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
    runtime.set_music_sink(
        [&music](const MusicTrackRequest& request) { music.push_back(request); });

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
    const InterfaceCompletion stale{.handle = InterfaceHandle{.interface_id = 29, .generation = 99},
        .result = 0};
    REQUIRE_FALSE(runtime.complete_interface_wait(stale).has_value());
    CHECK(runtime.state() == AreaScriptState::k_waiting);

    // Matching completion resumes exactly once.
    REQUIRE(runtime.complete_interface_wait(
                     InterfaceCompletion{.handle = menu_handle, .result = 0})
                .has_value());
    CHECK(runtime.state() == AreaScriptState::k_running);

    // A duplicate matching completion is rejected (not waiting anymore).
    REQUIRE_FALSE(runtime.complete_interface_wait(
                          InterfaceCompletion{.handle = menu_handle, .result = 0})
                      .has_value());
  }

  TEST_CASE("The opcode registry knows the confirmed opcodes only") {
    CHECK(App::Script::area_opcode_name(0x0D) != nullptr);
    CHECK(App::Script::area_opcode_name(0x46) != nullptr);
    CHECK(App::Script::area_opcode_name(0x67) != nullptr);
    CHECK(App::Script::area_opcode_name(0x99) == nullptr);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// bugprone-unchecked-optional-access)

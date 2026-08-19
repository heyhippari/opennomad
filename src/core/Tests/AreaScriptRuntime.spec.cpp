#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// bugprone-unchecked-optional-access)

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "Core/Script/AreaScriptOpcode.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "OmikronTestBuffer.hpp"

namespace {

using App::Script::AreaScriptRuntime;
using App::Script::AreaScriptState;

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

  TEST_CASE("Queueing event 1 runs the prefix and opens interface 29") {
    const Buffer bytes{make_startup_prefix()};
    AreaScriptRuntime runtime{bytes.data()};

    std::optional<std::uint16_t> opened_id;
    std::optional<std::int16_t> operand_b;
    std::optional<std::int16_t> operand_c;
    runtime.set_interface_sink(
        [&](const std::uint16_t id, const std::int16_t b_value, const std::int16_t c_value) {
          opened_id = id;
          operand_b = b_value;
          operand_c = c_value;
        });

    runtime.queue_event(1);
    runtime.activate();
    const AreaScriptState state{runtime.run()};

    CHECK(state == AreaScriptState::k_waiting);
    CHECK_EQ(runtime.wait_state(), 6U);
    REQUIRE(opened_id.has_value());
    CHECK_EQ(opened_id.value(), 29U);
    CHECK_EQ(operand_b.value(), -1);
    CHECK_EQ(operand_c.value(), 19);

    CHECK_EQ(runtime.variable(175), std::optional<std::int32_t>{1});
    CHECK_EQ(runtime.variable(170), std::optional<std::int32_t>{50});
    CHECK_EQ(runtime.state_value(), 109);
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

  TEST_CASE("0x67 consumes six operand bytes") {
    Buffer bytes;
    bytes.u8(0x67).u16(109).u16(1).u16(1).u8(0x00);  // unknown opcode next.
    AreaScriptRuntime runtime{bytes.data()};
    runtime.queue_event(1);
    runtime.activate();
    const AreaScriptState state{runtime.run()};

    CHECK(state == AreaScriptState::k_paused_unsupported);
    CHECK_EQ(runtime.pause_info().offset, 7U);
    CHECK_EQ(runtime.pause_info().opcode, 0x00U);
    CHECK_EQ(runtime.state_value(), 109);
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

  TEST_CASE("The opcode registry knows the confirmed opcodes only") {
    CHECK(App::Script::area_opcode_name(0x0D) != nullptr);
    CHECK(App::Script::area_opcode_name(0x46) != nullptr);
    CHECK(App::Script::area_opcode_name(0x99) == nullptr);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// bugprone-unchecked-optional-access)

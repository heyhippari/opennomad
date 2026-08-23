#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// bugprone-unchecked-optional-access)

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "Core/Debug/AreaVmDebugState.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "OmikronTestBuffer.hpp"

namespace {

App::Debug::AreaVmContextSourceDebugState test_source() {
  return App::Debug::AreaVmContextSourceDebugState{.identity = 118,
      .open_nomad_context_index = 0,
      .owner_area_slot = std::uint8_t{0},
      .retail_registry_slot = std::nullopt,
      .area_id = 118,
      .source_primary_event_offset = 0x3FC,
      .source_event_entry_offsets = {std::uint32_t{0x3FC}, std::nullopt, std::nullopt},
      .open_nomad_execution_base_offset = 0x3FC};
}

}  // namespace

TEST_SUITE("Core::Debug::AreaVmDebugState") {
  TEST_CASE("Snapshot exposes real queue order, active event, stack values, and safe bytes") {
    Buffer bytes;
    bytes.u8(0x07).u8(42).u8(0x99);  // push i8, then unsupported
    App::Script::AreaScriptRuntime runtime{bytes.data()};
    runtime.queue_event(1);
    runtime.queue_event(2);
    runtime.activate();
    REQUIRE(runtime.run() == App::Script::AreaScriptState::k_paused_unsupported);

    const App::Debug::AreaVmContextDebugState snapshot{
        App::Debug::build_area_vm_context_debug_state(runtime, test_source())};
    REQUIRE(snapshot.active_event.has_value());
    CHECK_EQ(snapshot.active_event.value(), 1U);
    REQUIRE_EQ(snapshot.queued_events.size(), 1U);
    CHECK_EQ(snapshot.queued_events.at(0), 2U);
    REQUIRE_EQ(snapshot.evaluation_stack.size(), 1U);
    CHECK_EQ(snapshot.evaluation_stack.at(0), 42);
    CHECK_EQ(snapshot.bytecode_size, 3U);
    CHECK_EQ(snapshot.instruction_pointer, 2U);
    REQUIRE(snapshot.current_instruction.has_value());
    CHECK_EQ(snapshot.current_instruction->opcode, 0x99U);
    REQUIRE_EQ(snapshot.current_instruction->nearby_bytes.size(), 1U);
    CHECK_EQ(snapshot.current_instruction->nearby_bytes.at(0), 0x99U);
    CHECK_FALSE(snapshot.recovered_runtime_state.has_value());
  }

  TEST_CASE("Active event remains visible through a typed interface wait") {
    Buffer bytes;
    bytes.u8(0x46).u16(29).u16(0xFFFF).u16(19);
    App::Script::AreaScriptRuntime runtime{bytes.data()};
    const App::InterfaceHandle handle{.interface_id = 29, .generation = 7};
    runtime.set_interface_sink(
        [handle](
            const App::InterfaceOpenRequest&) -> std::expected<App::InterfaceHandle, std::string> {
          return handle;
        });
    runtime.queue_event(1);
    runtime.activate();
    REQUIRE(runtime.run() == App::Script::AreaScriptState::k_waiting);

    const App::Debug::AreaVmContextDebugState snapshot{
        App::Debug::build_area_vm_context_debug_state(runtime, test_source())};
    REQUIRE(snapshot.active_event.has_value());
    CHECK_EQ(snapshot.active_event.value(), 1U);
    REQUIRE(snapshot.recovered_runtime_state.has_value());
    CHECK_EQ(snapshot.recovered_runtime_state.value(), 6U);
    CHECK(snapshot.wait.kind == App::Script::AreaWaitKind::k_interface);
    REQUIRE(snapshot.wait.interface.has_value());
    CHECK(snapshot.wait.interface.value() == handle);
  }

  TEST_CASE("Event termination clears active-event instrumentation") {
    Buffer bytes;
    bytes.u8(0x03);
    App::Script::AreaScriptRuntime runtime{bytes.data()};
    runtime.queue_event(1);
    runtime.activate();
    REQUIRE(runtime.run() == App::Script::AreaScriptState::k_ready);
    CHECK_FALSE(runtime.active_event().has_value());
  }

  TEST_CASE("Snapshot reports an explicit dispatcher yield") {
    Buffer bytes;
    bytes.u8(0x77).u32(0x00FFFFFFU).u16(30).u16(20).u8(0x03);
    App::Script::AreaScriptRuntime runtime{bytes.data()};
    runtime.queue_event(1);
    runtime.activate();
    REQUIRE(runtime.run() == App::Script::AreaScriptState::k_running);

    const App::Debug::AreaVmContextDebugState snapshot{
        App::Debug::build_area_vm_context_debug_state(runtime, test_source())};
    CHECK(snapshot.last_run_yielded);
    REQUIRE(snapshot.active_event.has_value());
    CHECK_EQ(snapshot.active_event.value(), 1U);
  }

  TEST_CASE("Registry architecture reports 32 without manufacturing contexts") {
    const App::Debug::AreaVmRegistryDebugState registry;
    CHECK_EQ(registry.retail_capacity, 32U);
    CHECK(registry.contexts.empty());
  }

  TEST_CASE("Unresolved wait mappings remain unnamed") {
    App::Script::AreaWaitState wait;
    wait.kind = App::Script::AreaWaitKind::k_scx_script;
    wait.runtime_state = 5;
    CHECK_FALSE(
        App::Debug::recovered_area_runtime_state(App::Script::AreaScriptState::k_waiting, wait)
            .has_value());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// bugprone-unchecked-optional-access)

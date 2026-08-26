#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// bugprone-unchecked-optional-access)

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
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
using App::Script::AreaAddressFlagRequest;
using App::Script::AreaAddressPlacementRequest;
using App::Script::AreaCameraOperationHandle;
using App::Script::AreaCameraRequest;
using App::Script::AreaCharacterActivationRequest;
using App::Script::AreaCharacterDeactivationRequest;
using App::Script::AreaCharacterScriptLaunchMode;
using App::Script::AreaCharacterScriptRequest;
using App::Script::AreaCharacterScriptTarget;
using App::Script::AreaCharacterSelectionRequest;
using App::Script::AreaCharacterValueRequest;
using App::Script::AreaCinematicLetterboxRequest;
using App::Script::AreaDialogRequest;
using App::Script::AreaObjectActivationRequest;
using App::Script::AreaObjectPlacementStateRequest;
using App::Script::AreaPersistentObjectCollectionRequest;
using App::Script::AreaPresentationRequest;
using App::Script::AreaReleaseRequest;
using App::Script::AreaSceneAttachRequest;
using App::Script::AreaScriptRuntime;
using App::Script::AreaScriptState;
using App::Script::AreaScxScriptRequest;
using App::Script::AreaTransitionHandle;
using App::Script::AreaTransitionRequest;
using App::Script::AreaWaitKind;
using App::Script::AreaZoneActivationRequest;

struct SharedGlobalStore {
  explicit SharedGlobalStore(const std::size_t size = 1024U) : values(size, 0) {}

  void bind(AreaScriptRuntime& runtime) {
    runtime.set_global_variable_read_sink(
        [this](const std::uint16_t id) -> std::expected<std::int32_t, std::string> {
          if (id >= values.size()) {
            return std::expected<std::int32_t, std::string>{
                std::unexpect, "global variable is out of range"};
          }
          return values.at(id);
        });
    runtime.set_global_variable_write_sink([this](const std::uint16_t id, const std::int32_t value)
                                               -> std::expected<void, std::string> {
      if (id >= values.size()) {
        return std::expected<void, std::string>{std::unexpect, "global variable is out of range"};
      }
      values.at(id) = value;
      return {};
    });
    runtime.set_global_variable_snapshot_sink([this]() -> std::span<const std::int32_t> {
      return values;
    });
  }

  std::vector<std::int32_t> values;
};

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
  bytes.u8(0x77).u32(0).u16(30).u16(0);            // +0x79
  bytes.u8(0x5F).u16(2172).u16(0).u16(2);          // +0x82
  bytes.u8(0x5F).u16(2148).u16(130).u16(2);        // +0x89
  bytes.u8(0x4E).u16(310).u16(1);                  // +0x90
  bytes.u8(0x3C).u16(310).u16(1).u16(0);           // +0x95
  bytes.u8(0x3B).u16(310).u16(6).u16(0);           // +0x9C
  bytes.u8(0x77).u32(0xFFFFFFFFU).u16(45).u16(0);  // +0xA3
  bytes.u8(0x3D).u16(272);                         // +0xAC

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

void wire_startup_character_sinks(AreaScriptRuntime& runtime) {
  runtime.set_character_selection_sink(
      [](const AreaCharacterSelectionRequest&) -> std::expected<void, std::string> {
        return {};
      });
  runtime.set_character_deactivation_sink(
      [](const AreaCharacterDeactivationRequest&) -> std::expected<void, std::string> {
        return {};
      });
  runtime.set_object_activation_sink(
      [](const AreaObjectActivationRequest&) -> std::expected<void, std::string> {
        return {};
      });
}

}  // namespace

TEST_SUITE("Core::Script::AreaScriptRuntime") {
  TEST_CASE("AREA and SCENE compact contexts share one global-variable store") {
    Buffer area_bytes;
    area_bytes.u8(0x0D).u16(123).u8(0x03);
    Buffer scene_bytes;
    scene_bytes.u8(0x0A).u16(123);
    AreaScriptRuntime area_runtime{area_bytes.data()};
    AreaScriptRuntime scene_runtime{scene_bytes.data()};
    SharedGlobalStore globals;
    globals.bind(area_runtime);
    globals.bind(scene_runtime);
    area_runtime.queue_event(1);
    area_runtime.activate();
    scene_runtime.queue_event(1);
    scene_runtime.activate();

    CHECK(area_runtime.run() == AreaScriptState::k_ready);
    CHECK(scene_runtime.run() == AreaScriptState::k_completed);
    REQUIRE_EQ(scene_runtime.evaluation_stack().size(), 1U);
    CHECK_EQ(scene_runtime.evaluation_stack().back(), 1);
    CHECK_EQ(globals.values.at(123), 1);
  }

  TEST_CASE("0x0C zeroes a shared global through direct and parameter Scalar16 operands") {
    SUBCASE("direct") {
      Buffer bytes;
      bytes.u8(0x0C).u16(60).u8(0x03);
      AreaScriptRuntime runtime{bytes.data()};
      SharedGlobalStore globals;
      globals.values.at(60) = 1234;
      globals.bind(runtime);
      runtime.queue_event(1);
      runtime.activate();

      CHECK(runtime.run() == AreaScriptState::k_ready);
      CHECK_EQ(globals.values.at(60), 0);
      CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
      CHECK_EQ(runtime.instruction_pointer(), bytes.data().size());
    }

    SUBCASE("parameter remapping") {
      Buffer bytes;
      bytes.u8(0x0C).u16(0x4001).u8(0x03);
      AreaScriptRuntime runtime{bytes.data()};
      SharedGlobalStore globals;
      globals.values.at(60) = -44;
      globals.bind(runtime);
      const std::array<std::int16_t, 2> parameters{7, 60};
      runtime.set_scalar16_parameters(parameters);
      runtime.queue_event(1);
      runtime.activate();

      CHECK(runtime.run() == AreaScriptState::k_ready);
      CHECK_EQ(globals.values.at(60), 0);
    }
  }

  TEST_CASE("0x4C consumes one Scalar16 and continues exactly into the following 0x47") {
    Buffer bytes;
    bytes.u8(0x4C).u16(162);
    bytes.u8(0x47).u16(237).u16(57);
    bytes.u8(0x03);

    AreaScriptRuntime runtime{bytes.data()};
    std::optional<AreaObjectPlacementStateRequest> placement_request;
    std::optional<AreaSceneAttachRequest> attach_request;
    runtime.set_object_placement_state_sink(
        [&placement_request](const AreaObjectPlacementStateRequest& request)
            -> std::expected<void, std::string> {
          placement_request = request;
          return {};
        });
    runtime.set_area_scene_attach_sink(
        [&attach_request](const AreaSceneAttachRequest& request)
            -> std::expected<void, std::string> {
          attach_request = request;
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    REQUIRE(placement_request.has_value());
    CHECK_EQ(placement_request->object_id, 162);
    CHECK(placement_request->enabled);
    REQUIRE(attach_request.has_value());
    CHECK_EQ(attach_request->area_id, 237);
    CHECK_EQ(attach_request->scene_id, 57);
    CHECK_EQ(runtime.instruction_pointer(), bytes.data().size());
  }

  TEST_CASE("0x4D resolves parameter-indirected Scalar16 object IDs without waiting") {
    Buffer bytes;
    bytes.u8(0x4D).u16(0x4000).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    const std::array<std::int16_t, 1> parameters{162};
    runtime.set_scalar16_parameters(parameters);

    std::optional<AreaObjectPlacementStateRequest> request;
    runtime.set_object_placement_state_sink(
        [&request](const AreaObjectPlacementStateRequest& value)
            -> std::expected<void, std::string> {
          request = value;
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    REQUIRE(request.has_value());
    CHECK_EQ(request->object_id, 162);
    CHECK_FALSE(request->enabled);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
  }

  TEST_CASE("0x5D writes the selected current-character value from a shared global") {
    Buffer bytes;
    bytes.u8(0x5D).u16(0xFFFF).u16(5).u16(60).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    SharedGlobalStore globals;
    globals.values.at(60) = 80;
    globals.bind(runtime);
    constexpr std::int16_t k_current_character{88};
    std::optional<std::int16_t> resolved_character;
    std::int32_t kind5{0};
    runtime.set_character_value_write_sink(
        [&resolved_character, &kind5, k_current_character](const AreaCharacterValueRequest& request,
            const std::int32_t value) -> std::expected<void, std::string> {
          resolved_character =
              request.character_id == -1 ? k_current_character : request.character_id;
          if (request.value_kind != 5) {
            return std::expected<void, std::string>{std::unexpect, "unexpected character kind"};
          }
          kind5 = value;
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    CHECK_EQ(resolved_character, std::optional<std::int16_t>{k_current_character});
    CHECK_EQ(kind5, 80);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
  }

  TEST_CASE("SCENE 55 zeroes globals 60/37 and current-character kinds 5/4 exactly") {
    Buffer bytes;
    bytes.u8(0x0C).u16(60);
    bytes.u8(0x5D).u16(0xFFFF).u16(5).u16(60);
    bytes.u8(0x0C).u16(37);
    bytes.u8(0x5D).u16(0xFFFF).u16(4).u16(37);
    bytes.u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    SharedGlobalStore globals;
    globals.values.at(60) = 91;
    globals.values.at(37) = 876;
    globals.bind(runtime);
    std::array<std::int32_t, 6> character_values{};
    runtime.set_character_value_write_sink(
        [&character_values](const AreaCharacterValueRequest& request,
            const std::int32_t value) -> std::expected<void, std::string> {
          if (request.character_id != -1 || request.value_kind < 0 ||
              static_cast<std::size_t>(request.value_kind) >= character_values.size()) {
            return std::expected<void, std::string>{
                std::unexpect, "unexpected SCENE 55 character value request"};
          }
          character_values.at(static_cast<std::size_t>(request.value_kind)) = value;
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    CHECK_EQ(globals.values.at(60), 0);
    CHECK_EQ(globals.values.at(37), 0);
    CHECK_EQ(character_values.at(5), 0);
    CHECK_EQ(character_values.at(4), 0);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
    CHECK_EQ(runtime.instruction_pointer(), bytes.data().size());
  }

  TEST_CASE("SCENE 55 retail slice activates its authored zone and initializes global 629") {
    Buffer bytes;
    bytes.u8(0x0C).u16(60);
    bytes.u8(0x5D).u16(0xFFFF).u16(5).u16(60);
    bytes.u8(0x0C).u16(37);
    bytes.u8(0x5D).u16(0xFFFF).u16(4).u16(37);
    bytes.u8(0x0C).u16(629);
    bytes.u8(0x07).u8(0);
    bytes.u8(0x0A).u16(629);
    bytes.u8(0x19);
    bytes.u8(0x06).u16(0x006C);
    bytes.u8(0x40).u16(0x0ED3);
    bytes.u8(0x07).u8(10);
    bytes.u8(0x13).u16(629);
    bytes.u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    SharedGlobalStore globals;
    globals.values.at(37) = 2;
    globals.values.at(60) = 3;
    globals.values.at(629) = 4;
    globals.bind(runtime);
    std::optional<AreaZoneActivationRequest> zone_request;
    runtime.set_zone_activation_sink(
        [&zone_request](
            const AreaZoneActivationRequest& request) -> std::expected<void, std::string> {
          zone_request = request;
          return {};
        });
    runtime.set_character_value_write_sink(
        [](const AreaCharacterValueRequest& request,
            const std::int32_t value) -> std::expected<void, std::string> {
          if (request.character_id != -1 || (request.value_kind != 4 && request.value_kind != 5) ||
              value != 0) {
            return std::expected<void, std::string>{
                std::unexpect, "unexpected SCENE 55 character value request"};
          }
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    REQUIRE(zone_request.has_value());
    CHECK_EQ(zone_request->zone_id, 3795);
    CHECK(zone_request->enabled);
    CHECK_EQ(globals.values.at(37), 0);
    CHECK_EQ(globals.values.at(60), 0);
    CHECK_EQ(globals.values.at(629), 10);
  }

  TEST_CASE("0x13 through 0x18 mutate shared globals with x86 dword semantics") {
    const auto execute = [](const std::uint8_t opcode,
                             const std::uint16_t target,
                             const std::uint16_t stack_source,
                             const std::int32_t initial_target,
                             const std::int32_t stack_value) {
      Buffer bytes;
      bytes.u8(0x0A).u16(stack_source).u8(opcode).u16(target).u8(0x03);
      AreaScriptRuntime runtime{bytes.data()};
      SharedGlobalStore globals;
      globals.values.at(target) = initial_target;
      globals.values.at(stack_source) = stack_value;
      globals.bind(runtime);
      runtime.queue_event(1);
      runtime.activate();
      CHECK(runtime.run() == AreaScriptState::k_ready);
      CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
      return globals.values.at(target);
    };

    CHECK_EQ(execute(0x13, 60, 61, 7, 10), 17);
    CHECK_EQ(execute(0x13, 60, 61, std::numeric_limits<std::int32_t>::max(), 1),
        std::numeric_limits<std::int32_t>::min());
    CHECK_EQ(execute(0x14, 60, 61, 10, 3), 7);
    CHECK_EQ(execute(0x14, 60, 61, std::numeric_limits<std::int32_t>::min(), 1),
        std::numeric_limits<std::int32_t>::max());
    CHECK_EQ(execute(0x15, 60, 61, 7, 6), 42);
    CHECK_EQ(execute(0x15, 60, 61, 0x40000000, 4), 0);
    CHECK_EQ(execute(0x16, 60, 61, 10, 3), 3);
    CHECK_EQ(execute(0x16, 60, 61, -10, 3), -3);
    CHECK_EQ(execute(0x16, 60, 61, 10, -3), -3);
    CHECK_EQ(execute(0x17,
                 60,
                 61,
                 std::bit_cast<std::int32_t>(std::uint32_t{0xF0F00FF0U}),
                 std::bit_cast<std::int32_t>(std::uint32_t{0x0FF0F00FU})),
        std::bit_cast<std::int32_t>(std::uint32_t{0x00F00000U}));
    CHECK_EQ(execute(0x18,
                 60,
                 61,
                 std::bit_cast<std::int32_t>(std::uint32_t{0xF0F00FF0U}),
                 std::bit_cast<std::int32_t>(std::uint32_t{0x0FF0F00FU})),
        std::bit_cast<std::int32_t>(std::uint32_t{0xFFF0FFFFU}));
  }

  TEST_CASE("global-stack arithmetic resolves Scalar16 parameters and fails atomically") {
    SUBCASE("parameter remapping") {
      Buffer bytes;
      bytes.u8(0x07).u8(10).u8(0x13).u16(0x4001).u8(0x03);
      AreaScriptRuntime runtime{bytes.data()};
      SharedGlobalStore globals;
      globals.values.at(60) = 7;
      globals.bind(runtime);
      const std::array<std::int16_t, 2> parameters{0, 60};
      runtime.set_scalar16_parameters(parameters);
      runtime.queue_event(1);
      runtime.activate();

      CHECK(runtime.run() == AreaScriptState::k_ready);
      CHECK_EQ(globals.values.at(60), 17);
    }

    SUBCASE("division faults preserve the destination") {
      Buffer bytes;
      bytes.u8(0x0A).u16(61).u8(0x16).u16(60);
      AreaScriptRuntime runtime{bytes.data()};
      SharedGlobalStore globals;
      globals.values.at(60) = 10;
      globals.values.at(61) = 0;
      globals.bind(runtime);
      runtime.queue_event(1);
      runtime.activate();

      CHECK(runtime.run() == AreaScriptState::k_failed);
      CHECK_EQ(globals.values.at(60), 10);
      CHECK(runtime.pause_info().reason_text.find("division by zero") != std::string::npos);

      Buffer overflow_bytes;
      overflow_bytes.u8(0x0A).u16(61).u8(0x16).u16(60);
      AreaScriptRuntime overflow{overflow_bytes.data()};
      globals.values.at(60) = std::numeric_limits<std::int32_t>::min();
      globals.values.at(61) = -1;
      globals.bind(overflow);
      overflow.queue_event(1);
      overflow.activate();
      CHECK(overflow.run() == AreaScriptState::k_failed);
      CHECK_EQ(globals.values.at(60), std::numeric_limits<std::int32_t>::min());
      CHECK(
          overflow.pause_info().reason_text.find("signed division overflow") != std::string::npos);
    }

    SUBCASE("underflow and invalid global leave the destination unchanged") {
      Buffer underflow_bytes;
      underflow_bytes.u8(0x13).u16(60);
      AreaScriptRuntime underflow{underflow_bytes.data()};
      SharedGlobalStore globals;
      globals.values.at(60) = 7;
      globals.bind(underflow);
      underflow.queue_event(1);
      underflow.activate();
      CHECK(underflow.run() == AreaScriptState::k_failed);
      CHECK_EQ(globals.values.at(60), 7);
      CHECK(underflow.pause_info().reason_text.find("underflow") != std::string::npos);

      Buffer invalid_bytes;
      invalid_bytes.u8(0x07).u8(1).u8(0x13).u16(1024);
      AreaScriptRuntime invalid{invalid_bytes.data()};
      globals.bind(invalid);
      invalid.queue_event(1);
      invalid.activate();
      CHECK(invalid.run() == AreaScriptState::k_failed);
      CHECK(invalid.pause_info().reason_text.find("out of range") != std::string::npos);
    }
  }

  TEST_CASE("0x40 and 0x41 emit immediate typed ZONE requests") {
    Buffer bytes;
    bytes.u8(0x40).u16(3795).u8(0x41).u16(0x4001).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    const std::array<std::int16_t, 2> parameters{0, static_cast<std::int16_t>(0x8005U)};
    runtime.set_scalar16_parameters(parameters);
    std::vector<AreaZoneActivationRequest> requests;
    runtime.set_zone_activation_sink(
        [&requests](const AreaZoneActivationRequest& request) -> std::expected<void, std::string> {
          requests.push_back(request);
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    REQUIRE_EQ(requests.size(), 2U);
    CHECK_EQ(requests.at(0).zone_id, 3795);
    CHECK(requests.at(0).enabled);
    CHECK_EQ(static_cast<std::uint16_t>(requests.at(1).zone_id), 0x8005U);
    CHECK_FALSE(requests.at(1).enabled);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);

    Buffer missing_sink_bytes;
    missing_sink_bytes.u8(0x40).u16(5);
    AreaScriptRuntime missing_sink{missing_sink_bytes.data()};
    missing_sink.queue_event(1);
    missing_sink.activate();
    CHECK(missing_sink.run() == AreaScriptState::k_failed);
    CHECK(
        missing_sink.pause_info().reason_text.find("zone activation bridge") != std::string::npos);
  }

  TEST_CASE("0x56 reads a current-character value into a shared global") {
    Buffer bytes;
    bytes.u8(0x56).u16(0xFFFF).u16(4).u16(37).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    SharedGlobalStore globals;
    globals.bind(runtime);
    runtime.set_character_value_read_sink(
        [](const AreaCharacterValueRequest& request) -> std::expected<std::int32_t, std::string> {
          if (request.character_id != -1 || request.value_kind != 4) {
            return std::expected<std::int32_t, std::string>{
                std::unexpect, "unexpected character value request"};
          }
          return 321;
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    CHECK_EQ(globals.values.at(37), 321);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
  }

  TEST_CASE("0x57 and 0x58 mutate ADDRESS state without waiting") {
    Buffer bytes;
    bytes.u8(0x57).u16(5).u8(0x58).u16(6).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    std::vector<AreaAddressFlagRequest> requests;
    runtime.set_address_flag_sink(
        [&requests](const AreaAddressFlagRequest& request) -> std::expected<void, std::string> {
          requests.push_back(request);
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    REQUIRE_EQ(requests.size(), 2U);
    CHECK_EQ(requests.at(0).address_id, 5);
    CHECK(requests.at(0).enabled);
    CHECK_EQ(requests.at(1).address_id, 6);
    CHECK_FALSE(requests.at(1).enabled);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
    CHECK_EQ(runtime.instruction_pointer(), bytes.data().size());
  }

  TEST_CASE("0x57 rejects parameter-indirected ADDRESS Scalar16 values") {
    Buffer bytes;
    bytes.u8(0x57).u16(0x4002);
    AreaScriptRuntime runtime{bytes.data()};
    std::size_t calls{0};
    runtime.set_address_flag_sink(
        [&calls](const AreaAddressFlagRequest&) -> std::expected<void, std::string> {
          ++calls;
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_failed);
    CHECK_EQ(calls, 0U);
    CHECK(runtime.pause_info().reason_text.find("parameter-indirected Scalar16") !=
          std::string::npos);
  }

  TEST_CASE("0x32 requests a persistent object insertion without waiting") {
    Buffer bytes;
    bytes.u8(0x32).u16(2).u16(314).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    std::optional<AreaPersistentObjectCollectionRequest> request;
    runtime.set_persistent_object_collection_sink(
        [&request](const AreaPersistentObjectCollectionRequest& value)
            -> std::expected<void, std::string> {
          request = value;
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    REQUIRE(request.has_value());
    CHECK_EQ(request->collection_kind, 2);
    CHECK_EQ(request->object_id, 314);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
    CHECK_EQ(runtime.instruction_pointer(), bytes.data().size());
  }

  TEST_CASE("0x5C resolves Scalar16, skips -1 loading, and yields without a typed wait") {
    Buffer bytes;
    bytes.u8(0x5C).u16(0x4000).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    const std::array<std::int16_t, 1> parameters{141};
    runtime.set_scalar16_parameters(parameters);
    std::vector<AreaObjectActivationRequest> requests;
    runtime.set_object_activation_sink(
        [&requests](
            const AreaObjectActivationRequest& request) -> std::expected<void, std::string> {
          requests.push_back(request);
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_running);
    CHECK(runtime.last_run_yielded());
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
    REQUIRE_EQ(requests.size(), 1U);
    CHECK_EQ(requests.front().object_id, 141);
    CHECK_EQ(runtime.instruction_pointer(), 3U);
    CHECK(runtime.run() == AreaScriptState::k_ready);

    Buffer minus_one;
    minus_one.u8(0x5C).u16(0xFFFF).u8(0x03);
    AreaScriptRuntime skipped{minus_one.data()};
    std::optional<AreaObjectActivationRequest> skipped_request;
    skipped.set_object_activation_sink(
        [&skipped_request](
            const AreaObjectActivationRequest& request) -> std::expected<void, std::string> {
          skipped_request = request;
          return {};
        });
    skipped.queue_event(1);
    skipped.activate();
    CHECK(skipped.run() == AreaScriptState::k_running);
    REQUIRE(skipped_request.has_value());
    CHECK_EQ(skipped_request->object_id, -1);
    CHECK(skipped.wait_info().kind == AreaWaitKind::k_none);
  }

  TEST_CASE("OBJECTS presentation is submitted before a following tracked character launch") {
    Buffer bytes;
    bytes.u8(0x5C).u16(141);
    bytes.u8(0x3C).u16(58).u16(267).u16(0);
    AreaScriptRuntime runtime{bytes.data()};
    std::vector<std::string> order;
    runtime.set_object_activation_sink(
        [&order](const AreaObjectActivationRequest&) -> std::expected<void, std::string> {
          order.emplace_back("object");
          return {};
        });
    runtime.set_character_script_sink(
        [&order](
            const AreaCharacterScriptRequest& request) -> std::expected<std::size_t, std::string> {
          order.emplace_back("tracked");
          CHECK_EQ(request.character_id, std::optional<std::int16_t>{58});
          CHECK_EQ(request.script_id, 267U);
          CHECK(request.mode == AreaCharacterScriptLaunchMode::k_tracked);
          return 44U;
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_running);
    REQUIRE_EQ(order.size(), 1U);
    CHECK_EQ(order.front(), "object");
    CHECK(runtime.last_run_yielded());
    CHECK(runtime.run() == AreaScriptState::k_waiting);
    REQUIRE_EQ(order.size(), 2U);
    CHECK_EQ(order.back(), "tracked");
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_character_script);
  }

  TEST_CASE("0x47 attaches a SCENE without waiting and continues to EndEvent") {
    Buffer bytes;
    bytes.u8(0x47).u16(222).u16(55).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    std::optional<AreaSceneAttachRequest> captured;
    std::size_t calls{0};
    runtime.set_area_scene_attach_sink(
        [&captured, &calls](
            const AreaSceneAttachRequest& request) -> std::expected<void, std::string> {
          captured = request;
          ++calls;
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    REQUIRE(captured.has_value());
    CHECK_EQ(captured->area_id, 222);
    CHECK_EQ(captured->scene_id, 55);
    CHECK_EQ(calls, 1U);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
    CHECK_EQ(runtime.instruction_pointer(), bytes.data().size());
  }

  TEST_CASE("0x49 requests current-character address placement without waiting") {
    Buffer bytes;
    bytes.u8(0x49).u16(654).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    std::optional<AreaAddressPlacementRequest> captured;
    runtime.set_area_address_placement_sink(
        [&captured](
            const AreaAddressPlacementRequest& request) -> std::expected<void, std::string> {
          captured = request;
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    REQUIRE(captured.has_value());
    CHECK_EQ(captured->address_id, 654);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
  }

  TEST_CASE("0x38 selects the current character once and continues through EndEvent") {
    Buffer bytes;
    bytes.u8(0x38).u16(73).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    std::optional<AreaCharacterSelectionRequest> captured;
    std::size_t calls{0};
    runtime.set_character_selection_sink(
        [&captured, &calls](
            const AreaCharacterSelectionRequest& request) -> std::expected<void, std::string> {
          captured = request;
          ++calls;
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    REQUIRE(captured.has_value());
    CHECK_EQ(captured->character_id, 73);
    CHECK_EQ(calls, 1U);
    CHECK(runtime.last_character_selection_request().has_value());
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
    CHECK_EQ(runtime.instruction_pointer(), bytes.data().size());
  }

  TEST_CASE("0x38 rejects a parameter-indirected Scalar16 before calling its sink") {
    Buffer bytes;
    bytes.u8(0x38).u16(0x4002);
    AreaScriptRuntime runtime{bytes.data()};
    std::size_t calls{0};
    runtime.set_character_selection_sink(
        [&calls](const AreaCharacterSelectionRequest&) -> std::expected<void, std::string> {
          ++calls;
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_failed);
    CHECK_EQ(calls, 0U);
    CHECK(runtime.pause_info().reason_text.find("parameter-indirected Scalar16") !=
          std::string::npos);
  }

  TEST_CASE("0x38 reports a selection sink failure without advancing") {
    Buffer bytes;
    bytes.u8(0x38).u16(73);
    AreaScriptRuntime runtime{bytes.data()};
    runtime.set_character_selection_sink(
        [](const AreaCharacterSelectionRequest&) -> std::expected<void, std::string> {
          return std::expected<void, std::string>{std::unexpect, "selection unavailable"};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_failed);
    CHECK_EQ(runtime.instruction_pointer(), 0U);
    CHECK(runtime.pause_info().reason_text.find("selection unavailable") != std::string::npos);
  }

  TEST_CASE("0x30 releases its requested AREA without waiting") {
    Buffer bytes;
    bytes.u8(0x30).u16(118).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    std::optional<AreaReleaseRequest> captured;
    runtime.set_area_release_sink(
        [&captured](const AreaReleaseRequest& request) -> std::expected<void, std::string> {
          captured = request;
          return {};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_ready);
    REQUIRE(captured.has_value());
    CHECK_EQ(captured->area_id, 118);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
  }

  TEST_CASE("0x47 sink failure preserves instruction boundaries as a structured failure") {
    Buffer bytes;
    bytes.u8(0x47).u16(222).u16(55).u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    runtime.set_area_scene_attach_sink(
        [](const AreaSceneAttachRequest&) -> std::expected<void, std::string> {
          return std::expected<void, std::string>{std::unexpect, "scene archive unavailable"};
        });
    runtime.queue_event(1);
    runtime.activate();

    CHECK(runtime.run() == AreaScriptState::k_failed);
    CHECK_EQ(runtime.pause_info().opcode, 0x47U);
    CHECK_EQ(runtime.instruction_pointer(), 0U);
    CHECK(runtime.pause_info().reason_text.find("scene archive unavailable") != std::string::npos);
  }

  TEST_CASE("0x39 launches SCX fire-and-forget and executes the following instruction") {
    Buffer bytes;
    bytes.u8(0x39).u16(20).u16(7).u16(9);
    bytes.u8(0x0D).u16(175);
    bytes.u8(0x03);

    AreaScriptRuntime runtime{bytes.data()};
    SharedGlobalStore globals;
    globals.bind(runtime);
    std::vector<AreaScxScriptRequest> requests;
    std::vector<std::uint32_t> instructions;
    runtime.set_scx_script_sink(
        [&requests](
            const AreaScxScriptRequest& request) -> std::expected<std::size_t, std::string> {
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
    SharedGlobalStore globals;
    globals.bind(runtime);
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
    runtime.set_cinematic_letterbox_sink([&requests](const AreaCinematicLetterboxRequest& request) {
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
        [&captured, &calls](
            const AreaCharacterActivationRequest& request) -> std::expected<void, std::string> {
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
        [&captured](
            const AreaCharacterActivationRequest& request) -> std::expected<void, std::string> {
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
    SharedGlobalStore globals;
    globals.bind(runtime);

    std::optional<InterfaceOpenRequest> opened;
    std::optional<MusicTrackRequest> music;
    const InterfaceHandle menu_handle{.interface_id = 29, .generation = 7};
    runtime.set_interface_sink(recording_interface_sink(opened, menu_handle));
    wire_startup_character_sinks(runtime);
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
    SharedGlobalStore globals;
    globals.bind(runtime);
    wire_startup_character_sinks(runtime);
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

  TEST_CASE("0x76 consumes eight operand bytes and decodes duration and delay") {
    Buffer bytes;
    bytes.u8(0x76).u32(0x00123456U).u16(30).u16(2).u8(0x00);  // unknown opcode next.
    AreaScriptRuntime runtime{bytes.data()};
    std::optional<AreaPresentationRequest> presentation;
    runtime.set_presentation_sink(
        [&presentation](const AreaPresentationRequest& request) { presentation = request; });
    runtime.queue_event(1);
    runtime.activate();
    const AreaScriptState yielded{runtime.run()};

    CHECK(yielded == AreaScriptState::k_running);
    CHECK_EQ(runtime.instruction_pointer(), 9U);
    REQUIRE(presentation.has_value());
    CHECK_EQ(presentation->mode, 1U);
    CHECK_EQ(presentation->color, 0x00123456U);
    CHECK_EQ(presentation->duration_units, 30);
    CHECK_EQ(presentation->delay_units, 2);

    CHECK(runtime.run() == AreaScriptState::k_paused_unsupported);
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
    SharedGlobalStore globals;
    globals.bind(runtime);

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
    SharedGlobalStore globals;
    globals.bind(runtime);

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
    bytes.u8(0x3B).u16(310).u16(1).u16(123);

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
    CHECK(captured->target == AreaCharacterScriptTarget::k_explicit);
    CHECK_EQ(captured->character_id, std::optional<std::int16_t>{310});
    CHECK_EQ(captured->script_id, 1U);
    CHECK_EQ(captured->camera_duration_units, 123);
    CHECK(captured->mode == AreaCharacterScriptLaunchMode::k_fire_and_forget);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);
  }

  TEST_CASE("0x3C blocks in Runtime state 4 on the explicit character-script request") {
    Buffer bytes;
    bytes.u8(0x3C).u16(310).u16(1).u16(123);

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
    CHECK(captured->target == AreaCharacterScriptTarget::k_explicit);
    CHECK_EQ(captured->character_id, std::optional<std::int16_t>{310});
    CHECK_EQ(captured->script_id, 1U);
    CHECK_EQ(captured->camera_duration_units, 123);
    CHECK(captured->mode == AreaCharacterScriptLaunchMode::k_tracked);

    REQUIRE(runtime.wait_info().character_script.has_value());
    CHECK(runtime.wait_info().character_script->target == AreaCharacterScriptTarget::k_explicit);
    CHECK_EQ(runtime.wait_info().character_script->character_id, std::optional<std::int16_t>{310});
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

  TEST_CASE("0x2E starts the current character script and waits for its exact instance") {
    Buffer bytes;
    bytes.u8(0x2E).u16(221).u16(0).u8(0x03);

    AreaScriptRuntime runtime{bytes.data()};
    std::optional<AreaCharacterScriptRequest> captured;
    runtime.set_character_script_sink(
        [&captured](
            const AreaCharacterScriptRequest& request) -> std::expected<std::size_t, std::string> {
          captured = request;
          return 42U;
        });

    const App::Script::AreaOpcodeInfo* info{App::Script::area_opcode_info(0x2E)};
    REQUIRE(info != nullptr);
    CHECK_EQ(info->name, "StartCurrentCharacterScriptTracked");
    CHECK(info->support == App::Script::OpcodeSupport::k_supported);
    CHECK_FALSE(info->provisional);
    CHECK_EQ(info->operand_count, 2U);

    runtime.queue_event(1);
    runtime.activate();
    REQUIRE(runtime.run() == AreaScriptState::k_waiting);
    CHECK_EQ(runtime.instruction_pointer(), 5U);
    CHECK_EQ(runtime.runtime_state(), 4U);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_character_script);
    CHECK_EQ(runtime.wait_info().character_script_instance, std::optional<std::size_t>{42U});

    REQUIRE(captured.has_value());
    CHECK(captured->target == AreaCharacterScriptTarget::k_current);
    CHECK_FALSE(captured->character_id.has_value());
    CHECK_EQ(captured->script_id, 221U);
    CHECK_EQ(captured->camera_duration_units, 0);
    CHECK(captured->mode == AreaCharacterScriptLaunchMode::k_tracked);

    REQUIRE_FALSE(runtime.complete_character_script_wait(41U).has_value());
    CHECK(runtime.state() == AreaScriptState::k_waiting);
    REQUIRE(runtime.complete_character_script_wait(42U).has_value());
    CHECK(runtime.state() == AreaScriptState::k_running);
    CHECK(runtime.run() == AreaScriptState::k_ready);
  }

  TEST_CASE("0x5A starts the current character script without waiting") {
    Buffer bytes;
    bytes.u8(0x5A).u16(229).u16(0).u8(0x03);

    AreaScriptRuntime runtime{bytes.data()};
    std::optional<AreaCharacterScriptRequest> captured;
    std::size_t calls{0};
    runtime.set_character_script_sink(
        [&captured, &calls](
            const AreaCharacterScriptRequest& request) -> std::expected<std::size_t, std::string> {
          captured = request;
          ++calls;
          return 43U;
        });

    const App::Script::AreaOpcodeInfo* info{App::Script::area_opcode_info(0x5A)};
    REQUIRE(info != nullptr);
    CHECK_EQ(info->name, "StartCurrentCharacterScript");
    CHECK(info->support == App::Script::OpcodeSupport::k_supported);
    CHECK_FALSE(info->provisional);
    CHECK_EQ(info->operand_count, 2U);

    runtime.queue_event(1);
    runtime.activate();
    CHECK(runtime.run() == AreaScriptState::k_ready);
    CHECK_EQ(calls, 1U);
    CHECK(runtime.wait_info().kind == AreaWaitKind::k_none);

    REQUIRE(captured.has_value());
    CHECK(captured->target == AreaCharacterScriptTarget::k_current);
    CHECK_FALSE(captured->character_id.has_value());
    CHECK_EQ(captured->script_id, 229U);
    CHECK_EQ(captured->camera_duration_units, 0);
    CHECK(captured->mode == AreaCharacterScriptLaunchMode::k_fire_and_forget);
  }

  TEST_CASE("current character script IDs remain raw while camera duration is Scalar16") {
    SUBCASE("script ID retains the parameter-reference bit") {
      Buffer bytes;
      bytes.u8(0x5A).u16(0x4001).u16(0).u8(0x03);

      AreaScriptRuntime runtime{bytes.data()};
      std::optional<AreaCharacterScriptRequest> captured;
      runtime.set_character_script_sink([&captured](const AreaCharacterScriptRequest& request)
                                            -> std::expected<std::size_t, std::string> {
        captured = request;
        return 44U;
      });
      runtime.queue_event(1);
      runtime.activate();

      CHECK(runtime.run() == AreaScriptState::k_ready);
      REQUIRE(captured.has_value());
      CHECK_EQ(captured->script_id, 0x4001U);
    }

    SUBCASE("camera duration rejects an unresolved parameter reference") {
      Buffer bytes;
      bytes.u8(0x2E).u16(221).u16(0x4000).u8(0x03);

      AreaScriptRuntime runtime{bytes.data()};
      runtime.set_character_script_sink(
          [](const AreaCharacterScriptRequest&) -> std::expected<std::size_t, std::string> {
            return 45U;
          });
      runtime.queue_event(1);
      runtime.activate();

      CHECK(runtime.run() == AreaScriptState::k_failed);
      CHECK(runtime.pause_info().reason_text.find("camera duration") != std::string::npos);
      CHECK(runtime.pause_info().reason_text.find("parameter-indirected Scalar16") !=
            std::string::npos);
    }
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
    runtime.set_area_transition_sink([&requests](const AreaTransitionRequest& request)
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

    REQUIRE_FALSE(
        runtime.complete_area_transition(AreaTransitionHandle{.generation = 41}).has_value());
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
        [&calls](const AreaTransitionRequest&) -> std::expected<AreaTransitionHandle, std::string> {
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
      CHECK(
          runtime.pause_info().reason_text.find("transition variant (0, -1)") != std::string::npos);
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
    SharedGlobalStore globals;
    globals.bind(runtime);

    const InterfaceHandle menu_handle{.interface_id = 29, .generation = 1};
    std::vector<AreaScxScriptRequest> scripts;
    std::uint64_t next_camera_operation{1};
    runtime.set_interface_sink(
        [menu_handle](const InterfaceOpenRequest&) -> std::expected<InterfaceHandle, std::string> {
          return menu_handle;
        });
    runtime.set_scx_script_sink(
        [&scripts](const AreaScxScriptRequest& request) -> std::expected<std::size_t, std::string> {
          scripts.push_back(request);
          return 42U;
        });
    runtime.set_camera_sink(
        [&next_camera_operation](const AreaCameraRequest& request)
            -> std::expected<AreaCameraOperationHandle, std::string> {
          return AreaCameraOperationHandle{
              .generation = request.wait_for_completion ? next_camera_operation++ : 0U};
        });
    wire_startup_character_sinks(runtime);

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
    REQUIRE(runtime.wait_info().camera_operation.has_value());
    const AreaCameraOperationHandle camera_2154{runtime.wait_info().camera_operation.value()};

    // A large VM delta cannot expire state 7: WorldCamera owns the only clock.
    CHECK(runtime.run(100.0F / 30.0F) == AreaScriptState::k_waiting);
    REQUIRE_FALSE(runtime.complete_camera_wait(
        AreaCameraOperationHandle{.generation = camera_2154.generation + 100U})
                      .has_value());
    CHECK(runtime.state() == AreaScriptState::k_waiting);
    REQUIRE(runtime.complete_camera_wait(camera_2154).has_value());
 
    // The presentation-owned completion resumes into 0x76, which records
    // presentation mode 1 and yields before camera 2158.
    REQUIRE(runtime.run() == AreaScriptState::k_running);
    REQUIRE(runtime.last_presentation_request().has_value());
    CHECK_EQ(runtime.last_presentation_request()->mode, 1U);

    REQUIRE(runtime.run() == AreaScriptState::k_waiting);
    REQUIRE(runtime.last_camera_request().has_value());
    CHECK_EQ(runtime.last_camera_request()->camera_id, 2158U);
    REQUIRE(runtime.wait_info().camera_operation.has_value());
    const AreaCameraOperationHandle camera_2158{runtime.wait_info().camera_operation.value()};
 
    // The exact second camera completion resumes into 0x04, which lands
    // exactly on +0x11F and executes the event terminator 0x03.
    REQUIRE(runtime.complete_camera_wait(camera_2158).has_value());
    CHECK(runtime.run(25.0F / 30.0F) == AreaScriptState::k_ready);
    CHECK_EQ(runtime.instruction_pointer(), 0x120U);
    CHECK_EQ(runtime.evaluation_stack_depth(), 0U);
  }

  TEST_CASE("Retail New Game result 3 resumes after tracked 310/1 and starts dialog 272") {
    const Buffer bytes{make_new_game_event_script()};
    AreaScriptRuntime runtime{bytes.data()};
    SharedGlobalStore globals;
    globals.bind(runtime);

    const InterfaceHandle menu_handle{.interface_id = 29, .generation = 1};
    runtime.set_interface_sink(
        [menu_handle](const InterfaceOpenRequest&) -> std::expected<InterfaceHandle, std::string> {
          return menu_handle;
        });

    std::vector<AreaCharacterScriptRequest> character_scripts;
    runtime.set_character_script_sink(
        [&character_scripts](
            const AreaCharacterScriptRequest& request) -> std::expected<std::size_t, std::string> {
          character_scripts.push_back(request);
          return request.script_id == 1U ? 77U : 78U;
        });
    std::vector<AreaDialogRequest> dialogs;
    runtime.set_dialog_sink([&dialogs](const AreaDialogRequest& request) {
      dialogs.push_back(request);
      return std::expected<void, std::string>{};
    });
    wire_startup_character_sinks(runtime);

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

    REQUIRE_EQ(character_scripts.size(), 1U);
    CHECK(character_scripts.front().target == AreaCharacterScriptTarget::k_explicit);
    CHECK_EQ(character_scripts.front().character_id, std::optional<std::int16_t>{310});
    CHECK_EQ(character_scripts.front().script_id, 1U);
    CHECK_EQ(character_scripts.front().camera_duration_units, 0);
    CHECK(character_scripts.front().mode == AreaCharacterScriptLaunchMode::k_tracked);
    CHECK_EQ(runtime.wait_info().character_script_instance, std::optional<std::size_t>{77U});

    // The exact child completion resumes at the post-launch compact IP, not
    // at event start. It launches script 310/6 once, yields for 0x77, then
    // reaches the documented DIALOG 272 0x3D.
    REQUIRE(runtime.complete_character_script_wait(77U).has_value());
    CHECK_EQ(runtime.instruction_pointer(), 0x9CU);
    REQUIRE(runtime.run() == AreaScriptState::k_running);
    REQUIRE_EQ(character_scripts.size(), 2U);
    CHECK_EQ(character_scripts.at(1).script_id, 6U);
    CHECK(character_scripts.at(1).mode == AreaCharacterScriptLaunchMode::k_fire_and_forget);
    REQUIRE(runtime.last_presentation_request().has_value());
    CHECK_EQ(runtime.last_presentation_request()->mode, 2U);
    CHECK_EQ(runtime.last_presentation_request()->color, 0xFFFFFFFFU);

    REQUIRE(runtime.run() == AreaScriptState::k_running);
    REQUIRE_EQ(dialogs.size(), 1U);
    CHECK_EQ(dialogs.front().dialog_id, 272);
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
    CHECK_EQ(std::string{App::Script::area_opcode_name(0x0C)}, "SetGlobalVariableZero");
    CHECK(App::Script::area_opcode_name(0x0D) != nullptr);
    CHECK_EQ(std::string{App::Script::area_opcode_name(0x13)}, "AddStackToGlobalVariable");
    CHECK_EQ(std::string{App::Script::area_opcode_name(0x18)}, "OrGlobalVariableWithStack");
    CHECK(App::Script::area_opcode_name(0x19) != nullptr);
    CHECK(App::Script::area_opcode_name(0x2F) != nullptr);
    CHECK_EQ(std::string{App::Script::area_opcode_name(0x32)}, "AddObjectToPersistentCollection");
    CHECK(App::Script::area_opcode_name(0x39) != nullptr);
    CHECK(App::Script::area_opcode_name(0x3A) != nullptr);
    CHECK(App::Script::area_opcode_name(0x3B) != nullptr);
    CHECK(App::Script::area_opcode_name(0x3C) != nullptr);
    CHECK(App::Script::area_opcode_name(0x3D) != nullptr);
    CHECK_EQ(std::string{App::Script::area_opcode_name(0x40)}, "ActivateZone");
    CHECK_EQ(std::string{App::Script::area_opcode_name(0x41)}, "DeactivateZone");
    CHECK(App::Script::area_opcode_name(0x4E) != nullptr);
    CHECK(App::Script::area_opcode_name(0x46) != nullptr);
    CHECK_EQ(std::string{App::Script::area_opcode_name(0x56)}, "GetCharacterValueToVariable");
    CHECK_EQ(std::string{App::Script::area_opcode_name(0x5D)}, "SetCharacterValueFromVariable");
    CHECK(App::Script::area_opcode_name(0x5F) != nullptr);
    CHECK(App::Script::area_opcode_name(0x60) != nullptr);
    CHECK_EQ(std::string{App::Script::area_opcode_name(0x57)}, "SetAddressFlag");
    CHECK_EQ(std::string{App::Script::area_opcode_name(0x58)}, "ClearAddressFlag");
    CHECK(App::Script::area_opcode_name(0x67) != nullptr);
    CHECK(App::Script::area_opcode_name(0x77) != nullptr);
    CHECK(App::Script::area_opcode_name(0x84) != nullptr);
    CHECK(App::Script::area_opcode_name(0x85) != nullptr);
    CHECK(App::Script::area_opcode_name(0x99) == nullptr);
  }

  TEST_CASE("Explicit event entries select record-relative bytecode safely") {
    Buffer top_level_bytes;
    top_level_bytes.u8(0x03);
    AreaScriptRuntime top_level{top_level_bytes.data()};
    top_level.queue_event(1);
    top_level.activate();
    CHECK(top_level.run() == AreaScriptState::k_ready);

    Buffer bytes;
    bytes.u8(0x99);  // record prefix must never run for the configured events.
    bytes.u8(0x03);  // event 1
    bytes.u8(0x03);  // event 2
    bytes.u8(0x03);  // event 3

    AreaScriptRuntime runtime{bytes.data()};
    REQUIRE(runtime
            .set_event_entries(
                App::Script::AreaScriptEventEntries{.event1 = 1U, .event2 = 2U, .event3 = 3U})
            .has_value());
    runtime.queue_event(1);
    runtime.activate();
    CHECK(runtime.run() == AreaScriptState::k_ready);
    CHECK_FALSE(runtime.active_event().has_value());

    runtime.queue_event(2);
    runtime.queue_event(2);
    CHECK_EQ(runtime.queued_events().size(), 1U);
    CHECK(runtime.run() == AreaScriptState::k_ready);
    runtime.queue_event(3);
    CHECK(runtime.run() == AreaScriptState::k_ready);

    AreaScriptRuntime missing{bytes.data()};
    REQUIRE(missing
            .set_event_entries(App::Script::AreaScriptEventEntries{
                .event1 = std::nullopt, .event2 = std::nullopt, .event3 = std::nullopt})
            .has_value());
    missing.queue_event(1);
    missing.activate();
    CHECK(missing.run() == AreaScriptState::k_ready);
  }

  TEST_CASE("Current-character control records are nonblocking typed requests") {
    Buffer bytes;
    bytes.u8(0x3F).u16(100);
    bytes.u8(0x68);
    bytes.u8(0x69);
    bytes.u8(0x03);
    AreaScriptRuntime runtime{bytes.data()};
    std::optional<std::int16_t> move;
    std::vector<bool> controller;
    runtime.set_current_character_move_sink(
        [&move](const App::Script::AreaCurrentCharacterMoveRequest& request) {
          move = request.move_id;
          return std::expected<void, std::string>{};
        });
    runtime.set_current_character_controller_sink(
        [&controller](const App::Script::AreaCurrentCharacterControllerRequest& request) {
          controller.push_back(request.enabled);
          return std::expected<void, std::string>{};
        });
    runtime.queue_event(1);
    runtime.activate();
    CHECK(runtime.run() == AreaScriptState::k_ready);
    CHECK_EQ(move, std::optional<std::int16_t>{100});
    REQUIRE_EQ(controller.size(), 2U);
    CHECK(controller.at(0));
    CHECK_FALSE(controller.at(1));
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// bugprone-unchecked-optional-access)

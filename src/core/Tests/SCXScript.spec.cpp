#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Omikron/SCX.hpp"
#include "OmikronTestBuffer.hpp"

namespace {

constexpr std::uint32_t K_MAGIC{0x00DEAD00U};
constexpr std::uint32_t K_SCRIPTS_TAG{0xDEAD0002U};
constexpr std::uint32_t K_SPRITES_TAG{0xDEAD0004U};
constexpr std::uint32_t K_END_TAG{0xDEADFFFFU};
constexpr std::uint32_t K_SET_FRAME{0x04000029U};

/// Appends one 0x18-byte serialized command record.
Buffer& command(Buffer& buffer,
    const std::uint32_t opcode,
    const std::uint32_t value_count,
    const std::uint32_t first_value_index,
    const std::int32_t next_command_index,
    const std::uint32_t execution_limit = 1) {
  buffer.u32(opcode).u32(value_count).u32(first_value_index).i32(next_command_index)
      .u32(execution_limit).u32(0);
  return buffer;
}

/// Appends one fixed 0x64-byte script record.
Buffer& script_record(Buffer& buffer,
    const std::string_view name,
    const std::uint16_t script_id,
    const std::uint32_t root_command_count,
    const std::uint32_t linked_command_count) {
  buffer.u32(0x00596C60U);
  buffer.chars(name, 22);
  buffer.u16(script_id).u16(1).u16(0);
  buffer.u32(root_command_count).u32(0).u32(0);
  buffer.u32(linked_command_count).u32(0);
  buffer.i32(1).u32(0);
  buffer.u32(0).u32(0).u32(0);
  buffer.u32(0).u32(0).u32(0);
  buffer.u32(0).u32(0);
  buffer.zeros(8);
  return buffer;
}

/// Appends the two empty trailing binding tables.
Buffer& empty_binding_tables(Buffer& buffer) {
  buffer.u32(0);  // binding table A count 0.
  buffer.u32(0);  // binding table B count 0.
  return buffer;
}

/// Wraps a descriptor block (DEAD0002 + DEAD0004 + DEADFFFF) in a full SCX.
Buffer make_scx(const Buffer& descriptor) {
  Buffer bytes;
  bytes.u32(K_MAGIC).u32(5).u32(8).u32(static_cast<std::uint32_t>(descriptor.data().size()));
  for (const std::byte byte : descriptor.data()) {
    bytes.u8(std::to_integer<std::uint8_t>(byte));
  }
  return bytes;
}

/// Builds a descriptor with a single script whose root command selects a
/// frame, two shared values, no linked commands and empty binding tables.
Buffer make_single_script_descriptor() {
  Buffer descriptor;
  descriptor.u32(K_SCRIPTS_TAG).u32(1);
  script_record(descriptor, "effects2_smoke2", 1, 1, 0);
  descriptor.u32(2);                 // sharedValueCount.
  descriptor.u32(0);                 // shared value 0: sprite index.
  descriptor.f32(1.5F);              // shared value 1: frame (float bits).
  descriptor.u8(0);                  // related block absent.
  command(descriptor, K_SET_FRAME, 2, 0, -1);
  empty_binding_tables(descriptor);
  descriptor.u32(K_SPRITES_TAG).u32(0);
  descriptor.u32(K_END_TAG);
  return descriptor;
}

bool error_contains(const std::expected<App::Omikron::ScxData, std::string>& result,
    const std::string_view text) {
  return result.error().find(text) != std::string::npos;
}

}  // namespace

TEST_SUITE("Core::Omikron::SCXScript") {
  TEST_CASE("Parses a valid script section and preserves raw value bits") {
    const Buffer file{make_scx(make_single_script_descriptor())};

    const auto scx{App::Omikron::SCX::load(file.data())};
    REQUIRE(scx.has_value());

    REQUIRE_EQ(scx->scripts.size(), 1U);
    CHECK_EQ(scx->scripts.at(0).name, "effects2_smoke2");
    CHECK_EQ(scx->scripts.at(0).script_id, 1U);
    CHECK_EQ(scx->scripts.at(0).root_command_count, 1U);
    CHECK_EQ(scx->scripts.at(0).linked_command_count, 0U);
    CHECK_EQ(scx->scripts.at(0).repeat_limit, 1);
    CHECK_EQ(scx->scripts.at(0).initial_repeat_index, 0U);
    CHECK_EQ(scx->scripts.at(0).file_offset, 24U);  // 16 header + 8 tag/count.

    REQUIRE_EQ(scx->shared_values.size(), 2U);
    CHECK_EQ(scx->shared_values.at(0).raw, 0U);
    CHECK_EQ(scx->shared_values.at(1).as_float(), doctest::Approx(1.5F));  // float bits.

    REQUIRE_EQ(scx->scripts.at(0).root_commands.size(), 1U);
    const App::Omikron::ScxScriptCommand& root{scx->scripts.at(0).root_commands.at(0)};
    CHECK_EQ(root.opcode, K_SET_FRAME);
    CHECK_EQ(root.value_count, 2U);
    CHECK_EQ(root.first_value_index, 0U);
    CHECK_FALSE(root.next_linked_command_index.has_value());  // -1 sentinel.
    CHECK_EQ(root.execution_limit, 1U);
  }

  TEST_CASE("Rejects a truncated command record") {
    Buffer descriptor;
    descriptor.u32(K_SCRIPTS_TAG).u32(1);
    script_record(descriptor, "s", 1, 1, 0);
    descriptor.u32(2).u32(0).u32(0);
    descriptor.u8(0);
    // A command record truncated to half its size.
    descriptor.u32(K_SET_FRAME).u32(2).u32(0);
    empty_binding_tables(descriptor);
    descriptor.u32(K_SPRITES_TAG).u32(0);
    descriptor.u32(K_END_TAG);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "script 0"));
  }

  TEST_CASE("Rejects a first-value slice beyond the shared pool") {
    Buffer descriptor;
    descriptor.u32(K_SCRIPTS_TAG).u32(1);
    script_record(descriptor, "s", 1, 1, 0);
    descriptor.u32(1).u32(0);  // one shared value.
    descriptor.u8(0);
    command(descriptor, K_SET_FRAME, 2, 1, -1);  // slice [1..3) exceeds pool of 1.
    empty_binding_tables(descriptor);
    descriptor.u32(K_SPRITES_TAG).u32(0);
    descriptor.u32(K_END_TAG);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "value slice"));
  }

  TEST_CASE("Rejects an out-of-range next-command index") {
    Buffer descriptor;
    descriptor.u32(K_SCRIPTS_TAG).u32(1);
    script_record(descriptor, "s", 1, 1, 1);
    descriptor.u32(2).u32(0).u32(0);
    descriptor.u8(0);
    command(descriptor, K_SET_FRAME, 2, 0, 0);
    command(descriptor, K_SET_FRAME, 2, 0, 5);  // next 5 exceeds 1 linked command.
    empty_binding_tables(descriptor);
    descriptor.u32(K_SPRITES_TAG).u32(0);
    descriptor.u32(K_END_TAG);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "next-command"));
  }

  TEST_CASE("Rejects an implausible script count") {
    Buffer descriptor;
    descriptor.u32(K_SCRIPTS_TAG).u32(0xFFFFFFF0U);
    descriptor.u32(K_SPRITES_TAG).u32(0);
    descriptor.u32(K_END_TAG);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "implausible count"));
  }

  TEST_CASE("Succeeds with no scripts when the DEAD0002 section is absent") {
    Buffer descriptor;
    descriptor.u32(K_SPRITES_TAG).u32(0);
    descriptor.u32(K_END_TAG);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor).data())};
    REQUIRE(scx.has_value());
    CHECK(scx->scripts.empty());
    CHECK(scx->shared_values.empty());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

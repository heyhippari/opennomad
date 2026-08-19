#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "Core/Omikron/SCX.hpp"
#include "OmikronTestBuffer.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

namespace {

constexpr std::size_t K_HEADER_SIZE{16};
constexpr std::uint32_t K_MAGIC{0x00DEAD00U};
constexpr std::uint32_t K_SOUNDS_TAG{0xDEAD0003U};
constexpr std::uint32_t K_SPRITES_TAG{0xDEAD0004U};
constexpr std::uint32_t K_END_TAG{0xDEADFFFFU};
constexpr std::uint32_t K_RIFF{0x46464952U};  // "RIFF"
constexpr std::uint32_t K_WAVE{0x45564157U};  // "WAVE"
constexpr std::uint32_t K_OD3X{0x5833444FU};  // "OD3X"

/// Appends one 0x24-byte DEAD0004 sprite/effect descriptor.
Buffer& append_sprite_record(Buffer& buffer, const std::string_view name) {
  buffer.chars(name, 24).u32(0x11111111U).u32(0x22222222U).u32(7);
  return buffer;
}

/// Appends one 0x1A-byte DEAD0003 sound descriptor.
Buffer& append_sound_record(Buffer& buffer, const std::string_view name,
    const std::uint16_t runtime_id, const std::uint16_t h_id) {
  buffer.chars(name, 22).u16(runtime_id).u16(h_id);
  return buffer;
}

/// Wraps a descriptor block and appended resource stream in a full container.
Buffer make_scx(const Buffer& descriptor, const Buffer& stream) {
  Buffer bytes;
  bytes.u32(K_MAGIC).u32(5).u32(8).u32(static_cast<std::uint32_t>(descriptor.data().size()));
  for (const std::byte byte : descriptor.data()) {
    bytes.u8(std::to_integer<std::uint8_t>(byte));
  }
  for (const std::byte byte : stream.data()) {
    bytes.u8(std::to_integer<std::uint8_t>(byte));
  }
  return bytes;
}

/// Builds a minimal valid container: one sprite plus its embedded OD3X model.
Buffer make_valid_scx(const std::string_view name) {
  Buffer descriptor;
  descriptor.u32(K_SPRITES_TAG).u32(1);
  append_sprite_record(descriptor, name);
  descriptor.u32(K_END_TAG);

  Buffer stream;
  stream.u32(K_HEADER_SIZE + descriptor.data().size()).u32(8).u32(0).u32(K_OD3X).u32(4);
  return make_scx(descriptor, stream);
}

/// Builds a container with a sound table and one RIFF/WAVE payload per sound.
Buffer make_sound_scx(const std::size_t count) {
  Buffer descriptor;
  descriptor.u32(K_SOUNDS_TAG).u32(static_cast<std::uint32_t>(count));
  for (std::size_t index{0}; index < count; ++index) {
    append_sound_record(descriptor, "SOUND", 0xFFFF, 0x0011);
  }
  descriptor.u32(K_END_TAG);

  const std::uint32_t stream_offset{
      static_cast<std::uint32_t>(K_HEADER_SIZE + descriptor.data().size())};
  Buffer stream;
  for (std::size_t index{0}; index < count; ++index) {
    const std::uint32_t position{stream_offset + static_cast<std::uint32_t>(index) * 20U};
    stream.u32(position).u32(12).u32(K_RIFF).u32(0).u32(K_WAVE);
  }
  return make_scx(descriptor, stream);
}

/// Returns true when an error result's message contains the given text.
bool error_contains(const std::expected<App::Omikron::ScxData, std::string>& result,
    const std::string_view text) {
  return result.error().find(text) != std::string::npos;
}

}  // namespace

TEST_SUITE("Core::Omikron::SCX") {
  TEST_CASE("Parses a valid minimal container") {
    const Buffer file{make_valid_scx("EFFECTS2_SMOKE2.3DO")};

    const auto scx{App::Omikron::SCX::load(file.data())};
    REQUIRE(scx.has_value());

    CHECK_EQ(scx->header.magic, K_MAGIC);
    CHECK_EQ(scx->header.version, 5U);
    CHECK_EQ(scx->header.header_word_2, 8U);
    CHECK_EQ(scx->resource_stream_offset, 64U);
    REQUIRE_EQ(scx->sprites.size(), 1U);
    CHECK_EQ(scx->sprites.at(0).name, "EFFECTS2_SMOKE2.3DO");
    CHECK_EQ(scx->sprites.at(0).sprite_id, 7U);
    CHECK(scx->waves.empty());
    REQUIRE_EQ(scx->models.size(), 1U);
    CHECK_EQ(scx->models.at(0).header_offset, 64U);
    CHECK_EQ(scx->models.at(0).core_offset, 76U);
    CHECK_EQ(scx->models.at(0).core_size, 8U);
    CHECK_EQ(scx->models.at(0).auxiliary_size, 0U);
  }

  TEST_CASE("Rejects a file shorter than the 16-byte header") {
    Buffer file;
    file.u32(K_MAGIC);

    const auto scx{App::Omikron::SCX::load(file.data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "too small"));
  }

  TEST_CASE("Rejects an invalid magic") {
    Buffer file;
    file.u32(0x12345678U).u32(5).u32(8).u32(0);

    const auto scx{App::Omikron::SCX::load(file.data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "magic"));
  }

  TEST_CASE("Rejects an unsupported version") {
    Buffer file;
    file.u32(K_MAGIC).u32(4).u32(8).u32(0);

    const auto scx{App::Omikron::SCX::load(file.data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "version"));
  }

  TEST_CASE("Rejects a descriptor size beyond the end of the file") {
    Buffer file;
    file.u32(K_MAGIC).u32(5).u32(8).u32(0x1000).zeros(32);

    const auto scx{App::Omikron::SCX::load(file.data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "descriptor"));
  }

  TEST_CASE("Rejects a descriptor that ends before DEADFFFF") {
    Buffer descriptor;
    descriptor.u32(K_SPRITES_TAG).u32(1);
    append_sprite_record(descriptor, "EFFECTS2_SMOKE2.3DO");
    // No DEADFFFF.

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, {}).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "DEADFFFF"));
  }

  TEST_CASE("Rejects trailing bytes after DEADFFFF") {
    Buffer descriptor;
    descriptor.u32(K_SPRITES_TAG).u32(0);
    descriptor.u32(K_END_TAG).u32(0x12345678U);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, {}).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "trailing bytes"));
  }

  TEST_CASE("Rejects duplicate descriptor tags") {
    Buffer descriptor;
    descriptor.u32(K_SPRITES_TAG).u32(0);
    descriptor.u32(K_SPRITES_TAG).u32(0);
    descriptor.u32(K_END_TAG);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, {}).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "duplicate"));
  }

  TEST_CASE("Records unmatched descriptor words as opaque gaps") {
    Buffer descriptor;
    descriptor.u32(0x12345678U);
    descriptor.u32(K_SPRITES_TAG).u32(0);
    descriptor.u32(K_END_TAG);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, {}).data())};
    REQUIRE(scx.has_value());
    REQUIRE_EQ(scx->descriptor_gaps.size(), 1U);
    CHECK_EQ(scx->descriptor_gaps.at(0).file_offset, K_HEADER_SIZE);
    CHECK_EQ(scx->descriptor_gaps.at(0).serialized_size, 4U);
  }

  TEST_CASE("Accepts a descriptor without a sprite table") {
    Buffer descriptor;
    descriptor.u32(K_SOUNDS_TAG).u32(0);
    descriptor.u32(K_END_TAG);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, {}).data())};
    REQUIRE(scx.has_value());
    CHECK(scx->sprites.empty());
    CHECK(scx->models.empty());
  }

  TEST_CASE("Indexes WAV resources parallel to the sound table") {
    const Buffer file{make_sound_scx(2)};

    const auto scx{App::Omikron::SCX::load(file.data())};
    REQUIRE(scx.has_value());
    REQUIRE_EQ(scx->sounds.size(), 2U);
    REQUIRE_EQ(scx->waves.size(), 2U);
    CHECK_EQ(scx->waves.at(0).header_offset, 80U);
    CHECK_EQ(scx->waves.at(0).payload_offset, 88U);
    CHECK_EQ(scx->waves.at(0).payload_size, 12U);
    CHECK_EQ(scx->waves.at(1).header_offset, 100U);
    CHECK(scx->models.empty());
  }

  TEST_CASE("Rejects a sound payload that is not RIFF/WAVE") {
    Buffer descriptor;
    descriptor.u32(K_SOUNDS_TAG).u32(1);
    append_sound_record(descriptor, "SOUND", 0xFFFF, 0);
    descriptor.u32(K_END_TAG);

    Buffer stream;
    stream.u32(K_HEADER_SIZE + descriptor.data().size()).u32(12)
        .u32(K_RIFF).u32(0).u32(0x12345678U);  // Not WAVE.

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, stream).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "RIFF/WAVE"));
  }

  TEST_CASE("Rejects a sound payload that runs past the end of the file") {
    Buffer descriptor;
    descriptor.u32(K_SOUNDS_TAG).u32(1);
    append_sound_record(descriptor, "SOUND", 0xFFFF, 0);
    descriptor.u32(K_END_TAG);

    Buffer stream;
    stream.u32(K_HEADER_SIZE + descriptor.data().size()).u32(100).u32(K_RIFF);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, stream).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "exceeds"));
  }

  TEST_CASE("Rejects a resource header whose self-offset does not match") {
    Buffer descriptor;
    descriptor.u32(K_SOUNDS_TAG).u32(1);
    append_sound_record(descriptor, "SOUND", 0xFFFF, 0);
    descriptor.u32(K_END_TAG);

    Buffer stream;
    stream.u32(K_HEADER_SIZE + descriptor.data().size() + 4U).u32(12)
        .u32(K_RIFF).u32(0).u32(K_WAVE);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, stream).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "self-offset"));
  }

  TEST_CASE("Rejects a truncated 8-byte resource header") {
    Buffer descriptor;
    descriptor.u32(K_SOUNDS_TAG).u32(1);
    append_sound_record(descriptor, "SOUND", 0xFFFF, 0);
    descriptor.u32(K_END_TAG);

    Buffer stream;
    stream.u32(K_HEADER_SIZE + descriptor.data().size());  // Only four bytes.

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, stream).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "truncated 8-byte"));
  }

  TEST_CASE("Rejects an embedded model with a missing OD3X core magic") {
    Buffer descriptor;
    descriptor.u32(K_SPRITES_TAG).u32(1);
    append_sprite_record(descriptor, "EFFECTS2_SMOKE2.3DO");
    descriptor.u32(K_END_TAG);

    Buffer stream;
    stream.u32(K_HEADER_SIZE + descriptor.data().size()).u32(8).u32(0)
        .u32(0x12345678U).u32(4);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, stream).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "OD3X"));
  }

  TEST_CASE("Rejects a truncated model core") {
    Buffer descriptor;
    descriptor.u32(K_SPRITES_TAG).u32(1);
    append_sprite_record(descriptor, "EFFECTS2_SMOKE2.3DO");
    descriptor.u32(K_END_TAG);

    Buffer stream;
    stream.u32(K_HEADER_SIZE + descriptor.data().size()).u32(8).u32(0).u32(K_OD3X);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, stream).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "exceeds"));
  }

  TEST_CASE("Rejects a truncated model auxiliary block") {
    Buffer descriptor;
    descriptor.u32(K_SPRITES_TAG).u32(1);
    append_sprite_record(descriptor, "EFFECTS2_SMOKE2.3DO");
    descriptor.u32(K_END_TAG);

    Buffer stream;
    stream.u32(K_HEADER_SIZE + descriptor.data().size()).u32(8).u32(8)
        .u32(K_OD3X).u32(4).zeros(4);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, stream).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "exceeds"));
  }

  TEST_CASE("Rejects an embedded model with an unsupported core version") {
    Buffer descriptor;
    descriptor.u32(K_SPRITES_TAG).u32(1);
    append_sprite_record(descriptor, "EFFECTS2_SMOKE2.3DO");
    descriptor.u32(K_END_TAG);

    Buffer stream;
    stream.u32(K_HEADER_SIZE + descriptor.data().size()).u32(8).u32(0)
        .u32(K_OD3X).u32(3);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, stream).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "version"));
  }

  TEST_CASE("Rejects a model core too small to hold its version header") {
    Buffer descriptor;
    descriptor.u32(K_SPRITES_TAG).u32(1);
    append_sprite_record(descriptor, "EFFECTS2_SMOKE2.3DO");
    descriptor.u32(K_END_TAG);

    Buffer stream;
    stream.u32(K_HEADER_SIZE + descriptor.data().size()).u32(4).u32(0).u32(K_OD3X);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, stream).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "too small"));
  }

  TEST_CASE("Rejects sprite records that overrun the descriptor") {
    Buffer descriptor;
    descriptor.u32(K_SPRITES_TAG).u32(2);
    append_sprite_record(descriptor, "EFFECTS2_SMOKE2.3DO");
    descriptor.u32(K_END_TAG);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, {}).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "sprites"));
  }

  TEST_CASE("Rejects unclaimed bytes after the resource manifest") {
    Buffer descriptor;
    descriptor.u32(K_SPRITES_TAG).u32(1);
    append_sprite_record(descriptor, "EFFECTS2_SMOKE2.3DO");
    descriptor.u32(K_END_TAG);

    Buffer stream;
    stream.u32(K_HEADER_SIZE + descriptor.data().size()).u32(8).u32(0)
        .u32(K_OD3X).u32(4).zeros(4);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, stream).data())};
    REQUIRE_FALSE(scx.has_value());
    CHECK(error_contains(scx, "unclaimed bytes"));
  }

  TEST_CASE("Decodes a non-NUL-terminated sprite filename as its full width") {
    Buffer descriptor;
    descriptor.u32(K_SPRITES_TAG).u32(1).chars("ABCDEFGHIJKLMNOPQRSTUVWX", 24)
        .u32(0).u32(0).u32(7);
    descriptor.u32(K_END_TAG);

    Buffer stream;
    stream.u32(K_HEADER_SIZE + descriptor.data().size()).u32(8).u32(0).u32(K_OD3X).u32(4);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, stream).data())};
    REQUIRE(scx.has_value());
    REQUIRE_EQ(scx->sprites.size(), 1U);
    CHECK_EQ(scx->sprites.at(0).name, "ABCDEFGHIJKLMNOPQRSTUVWX");
  }

  TEST_CASE("Decodes a non-NUL-terminated sound name safely") {
    Buffer descriptor;
    descriptor.u32(K_SOUNDS_TAG).u32(1).chars("ABCDEFGHIJKLMNOPQRSTUV", 22)
        .u16(0xFFFF).u16(0);
    descriptor.u32(K_END_TAG);

    Buffer stream;
    stream.u32(K_HEADER_SIZE + descriptor.data().size()).u32(12).u32(K_RIFF).u32(0).u32(K_WAVE);

    const auto scx{App::Omikron::SCX::load(make_scx(descriptor, stream).data())};
    REQUIRE(scx.has_value());
    REQUIRE_EQ(scx->sounds.size(), 1U);
    CHECK_EQ(scx->sounds.at(0).name, "ABCDEFGHIJKLMNOPQRSTUV");
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

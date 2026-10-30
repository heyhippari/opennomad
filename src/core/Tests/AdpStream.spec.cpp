#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// bugprone-unchecked-optional-access)

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_iostream.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Core/Audio/AdpStream.hpp"
#include "Core/Omikron/QdAdp.hpp"

namespace {

/// Builds an ADP file whose payload spans several decode windows.
std::vector<std::byte> make_long_adp(const std::size_t payload_size, const bool stereo = true) {
  std::vector<std::byte> data;
  data.push_back(static_cast<std::byte>(payload_size & 0xFFU));
  data.push_back(static_cast<std::byte>((payload_size >> 8U) & 0xFFU));
  data.push_back(static_cast<std::byte>((payload_size >> 16U) & 0xFFU));
  data.push_back(stereo ? std::byte{1} : std::byte{0});
  for (std::size_t index{0}; index < 12U; ++index) {
    data.push_back(std::byte{0});
  }
  for (std::size_t index{0}; index < payload_size; ++index) {
    data.push_back(static_cast<std::byte>(((index * 37U) + (index >> 5U)) & 0xFFU));
  }
  return data;
}

std::vector<std::byte> decode_all(const std::vector<std::byte>& file) {
  auto decoder{App::Omikron::QdAdpDecoder::create(file)};
  REQUIRE(decoder.has_value());
  std::vector<std::int16_t> pcm(
      decoder->total_frames() * static_cast<std::size_t>(decoder->channels()));
  REQUIRE_EQ(decoder->decode_frames(pcm), decoder->total_frames());
  const std::span<const std::byte> bytes{std::as_bytes(std::span{pcm})};
  return {bytes.begin(), bytes.end()};
}

std::vector<std::byte> read_bytes(SDL_IOStream* const io, const std::size_t size) {
  std::vector<std::byte> bytes(size);
  bytes.resize(SDL_ReadIO(io, bytes.data(), bytes.size()));
  return bytes;
}

}  // namespace

TEST_SUITE("Core::Audio::AdpStream") {
  TEST_CASE("streams the same PCM as a full decode, including across seeks") {
    const std::vector<std::byte> file{make_long_adp(30000U)};
    const std::vector<std::byte> expected{decode_all(file)};

    auto stream{App::Audio::open_adp_stream(file)};
    REQUIRE(stream.has_value());
    CHECK_EQ(stream->spec.format, SDL_AUDIO_S16LE);
    CHECK_EQ(stream->spec.channels, 2);
    CHECK_EQ(stream->total_frames * 4U, expected.size());
    CHECK_EQ(SDL_GetIOSize(stream->io), static_cast<Sint64>(expected.size()));

    // Odd-sized sequential reads cross decode-window and frame boundaries.
    std::vector<std::byte> streamed;
    while (true) {
      const std::vector<std::byte> chunk{read_bytes(stream->io, 3001U)};
      if (chunk.empty()) {
        break;
      }
      streamed.insert(streamed.end(), chunk.begin(), chunk.end());
    }
    CHECK(streamed == expected);
    CHECK_EQ(SDL_GetIOStatus(stream->io), SDL_IO_STATUS_EOF);

    // A loop restart rewinds the decoder; a forward seek skips ahead.
    REQUIRE_EQ(SDL_SeekIO(stream->io, 0, SDL_IO_SEEK_SET), 0);
    CHECK(read_bytes(stream->io, 512U) ==
          std::vector<std::byte>(expected.begin(), expected.begin() + 512));
    const Sint64 middle{40001};
    REQUIRE_EQ(SDL_SeekIO(stream->io, middle, SDL_IO_SEEK_SET), middle);
    CHECK(read_bytes(stream->io, 777U) ==
          std::vector<std::byte>(expected.begin() + middle, expected.begin() + middle + 777));
    CHECK_EQ(
        SDL_SeekIO(stream->io, -10, SDL_IO_SEEK_END), static_cast<Sint64>(expected.size()) - 10);
    CHECK_EQ(read_bytes(stream->io, 64U).size(), 10U);

    CHECK(SDL_CloseIO(stream->io));
  }

  TEST_CASE("streams mono voice-over clips, including seeks to odd frames") {
    const std::vector<std::byte> file{make_long_adp(9001U, /*stereo=*/false)};
    const std::vector<std::byte> expected{decode_all(file)};

    auto stream{App::Audio::open_adp_stream(file)};
    REQUIRE(stream.has_value());
    CHECK_EQ(stream->spec.channels, 1);
    CHECK_EQ(stream->total_frames * 2U, expected.size());

    // Mono packs two frames per byte, so an odd frame splits a byte.
    const Sint64 odd_frame_offset{(4097 * 2)};
    REQUIRE_EQ(SDL_SeekIO(stream->io, odd_frame_offset, SDL_IO_SEEK_SET), odd_frame_offset);
    CHECK(
        read_bytes(stream->io, 1001U) == std::vector<std::byte>(expected.begin() + odd_frame_offset,
                                             expected.begin() + odd_frame_offset + 1001));
    REQUIRE_EQ(SDL_SeekIO(stream->io, 6, SDL_IO_SEEK_SET), 6);
    CHECK(read_bytes(stream->io, 9000U) ==
          std::vector<std::byte>(expected.begin() + 6, expected.begin() + 9006));

    CHECK(SDL_CloseIO(stream->io));
  }

  TEST_CASE("rejects a truncated ADP file") {
    const std::vector<std::byte> short_file(15, std::byte{0});
    CHECK_FALSE(App::Audio::open_adp_stream(short_file).has_value());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// bugprone-unchecked-optional-access)

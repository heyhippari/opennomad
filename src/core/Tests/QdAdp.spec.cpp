#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// bugprone-unchecked-optional-access, cppcoreguidelines-pro-bounds-constant-array-index)

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Core/Omikron/QdAdp.hpp"

namespace {

using App::Omikron::QdAdpDecoder;
using App::Omikron::QdAdpFile;

/// Builds an ADP file: 3-byte LE payload size, stereo flag, 12 zero bytes,
/// then the payload.
std::vector<std::byte> make_adp(const std::vector<std::byte>& payload, const std::uint8_t stereo) {
  std::vector<std::byte> data;
  data.reserve(0x10 + payload.size());
  const std::uint32_t payload_size{static_cast<std::uint32_t>(payload.size())};
  data.push_back(static_cast<std::byte>(payload_size & 0xFFU));
  data.push_back(static_cast<std::byte>((payload_size >> 8U) & 0xFFU));
  data.push_back(static_cast<std::byte>((payload_size >> 16U) & 0xFFU));
  data.push_back(static_cast<std::byte>(stereo));
  for (std::size_t index{0}; index < 12U; ++index) {
    data.push_back(std::byte{0});
  }
  data.insert(data.end(), payload.begin(), payload.end());
  return data;
}

std::vector<std::byte> make_payload(const std::initializer_list<std::uint8_t> bytes) {
  std::vector<std::byte> payload;
  payload.reserve(bytes.size());
  for (const std::uint8_t value : bytes) {
    payload.push_back(static_cast<std::byte>(value));
  }
  return payload;
}

}  // namespace

TEST_SUITE("Core::Omikron::QdAdp") {
  TEST_CASE("valid stereo header") {
    const std::vector<std::byte> payload{make_payload({0x00, 0x01, 0x02, 0x03})};
    const std::vector<std::byte> file{make_adp(payload, 1)};

    auto parsed{QdAdpFile::load(std::span<const std::byte>{file})};
    REQUIRE(parsed.has_value());
    CHECK_EQ(parsed->stereo_flag(), 1U);
    CHECK_EQ(parsed->channels(), 2);
    CHECK_EQ(parsed->sample_rate(), 22050);
    CHECK_EQ(parsed->total_frames(), 4U);  // payload 4 bytes * 2 / 2 channels.
    CHECK_EQ(parsed->payload().size(), 4U);
  }

  TEST_CASE("valid mono header") {
    const std::vector<std::byte> payload{make_payload({0x00, 0x01})};
    const std::vector<std::byte> file{make_adp(payload, 0)};

    auto parsed{QdAdpFile::load(std::span<const std::byte>{file})};
    REQUIRE(parsed.has_value());
    CHECK_EQ(parsed->stereo_flag(), 0U);
    CHECK_EQ(parsed->channels(), 1);
    CHECK_EQ(parsed->total_frames(), 4U);  // payload 2 bytes * 2 / 1 channel.
  }

  TEST_CASE("invalid headers are rejected") {
    // File shorter than the 16-byte header.
    const std::vector<std::byte> short_file(15, std::byte{0});
    CHECK_FALSE(QdAdpFile::load(std::span<const std::byte>{short_file}).has_value());

    // Payload size mismatch.
    const std::vector<std::byte> payload{make_payload({0x00, 0x01})};
    const std::vector<std::byte> good{make_adp(payload, 0)};
    std::vector<std::byte> mismatched{good};
    mismatched.at(0) = std::byte{0x7F};  // Corrupt payload size low byte.
    CHECK_FALSE(QdAdpFile::load(std::span<const std::byte>{mismatched}).has_value());

    // Invalid stereo flag.
    const std::vector<std::byte> bad_flag{make_adp(payload, 2)};
    CHECK_FALSE(QdAdpFile::load(std::span<const std::byte>{bad_flag}).has_value());

    // Nonzero reserved header byte.
    std::vector<std::byte> bad_reserved{make_adp(payload, 0)};
    bad_reserved.at(0x05) = std::byte{1};
    CHECK_FALSE(QdAdpFile::load(std::span<const std::byte>{bad_reserved}).has_value());
  }

  TEST_CASE("mono high-nibble-first order and initial zero state") {
    const std::vector<std::byte> file{make_adp(make_payload({0x00}), 0)};
    auto decoder{QdAdpDecoder::create(std::span<const std::byte>{file})};
    REQUIRE(decoder.has_value());

    CHECK_EQ(decoder->predictor(0), 0);
    CHECK_EQ(decoder->step_index(0), 0);

    std::vector<std::int16_t> pcm(2);
    CHECK_EQ(decoder->decode_frames(std::span<std::int16_t>{pcm}), 2U);
    // Both nibbles of 0x00 produce zero with a zero predictor.
    CHECK_EQ(pcm.at(0), 0);
    CHECK_EQ(pcm.at(1), 0);
  }

  TEST_CASE("positive and negative nibbles") {
    const std::vector<std::byte> file{make_adp(make_payload({0x22, 0x2A}), 0)};
    auto decoder{QdAdpDecoder::create(std::span<const std::byte>{file})};
    REQUIRE(decoder.has_value());

    std::vector<std::int16_t> pcm(4);
    CHECK_EQ(decoder->decode_frames(std::span<std::int16_t>{pcm}), 4U);
    // Byte 0x22: high 0x2 -> +3, low 0x2 -> +3 more (predictor 3 -> 6).
    CHECK_EQ(pcm.at(0), 3);
    CHECK_EQ(pcm.at(1), 6);
    // Byte 0x2A: high 0x2 -> +3 (predictor 6 -> 9); low 0xA (sign) -> -3
    // (predictor 9 -> 6).
    CHECK_EQ(pcm.at(2), 9);
    CHECK_EQ(pcm.at(3), 6);
  }

  TEST_CASE("stereo high=channel0 low=channel1") {
    const std::vector<std::byte> file{make_adp(make_payload({0x21}), 1)};
    auto decoder{QdAdpDecoder::create(std::span<const std::byte>{file})};
    REQUIRE(decoder.has_value());

    std::vector<std::int16_t> pcm(2);
    CHECK_EQ(decoder->decode_frames(std::span<std::int16_t>{pcm}), 1U);
    CHECK_EQ(pcm.at(0), 3);  // channel 0 from high nibble 0x2.
    CHECK_EQ(pcm.at(1), 1);  // channel 1 from low nibble 0x1.
  }

  TEST_CASE("step index clamps at 0") {
    const std::vector<std::byte> file{make_adp(make_payload({0x00, 0x00, 0x00}), 0)};
    auto decoder{QdAdpDecoder::create(std::span<const std::byte>{file})};
    REQUIRE(decoder.has_value());

    std::vector<std::int16_t> pcm(6);
    static_cast<void>(decoder->decode_frames(std::span<std::int16_t>{pcm}));
    CHECK_EQ(decoder->step_index(0), 0);
  }

  TEST_CASE("step index clamps at 88") {
    // Nibble 0x4 has index adjustment +2 and magnitude 4. Enough repetitions
    // drive the step index to its 88 ceiling even as the predictor clamps.
    std::vector<std::byte> payload;
    for (std::size_t index{0}; index < 64U; ++index) {
      payload.push_back(std::byte{0x44});
    }
    const std::vector<std::byte> file{make_adp(payload, 0)};
    auto decoder{QdAdpDecoder::create(std::span<const std::byte>{file})};
    REQUIRE(decoder.has_value());

    std::vector<std::int16_t> pcm(128);
    static_cast<void>(decoder->decode_frames(std::span<std::int16_t>{pcm}));
    CHECK_EQ(decoder->step_index(0), 88);
  }

  TEST_CASE("predictor clamps to signed 16-bit") {
    // Nibble 0x4 repeatedly adds a large positive delta once the step index
    // is large; the predictor must never exceed the signed 16-bit maximum.
    std::vector<std::byte> payload;
    for (std::size_t index{0}; index < 200U; ++index) {
      payload.push_back(std::byte{0x44});
    }
    const std::vector<std::byte> file{make_adp(payload, 0)};
    auto decoder{QdAdpDecoder::create(std::span<const std::byte>{file})};
    REQUIRE(decoder.has_value());

    std::vector<std::int16_t> pcm(400);
    static_cast<void>(decoder->decode_frames(std::span<std::int16_t>{pcm}));
    CHECK_LE(decoder->predictor(0), 32767);
    CHECK_GE(decoder->predictor(0), -32768);
  }

  TEST_CASE("rewind returns output to the beginning") {
    const std::vector<std::byte> payload{make_payload({0x21, 0x43})};
    const std::vector<std::byte> file{make_adp(payload, 1)};
    auto decoder{QdAdpDecoder::create(std::span<const std::byte>{file})};
    REQUIRE(decoder.has_value());

    std::vector<std::int16_t> first(4, 0);
    CHECK_EQ(decoder->decode_frames(std::span<std::int16_t>{first}), 2U);

    decoder->rewind();
    CHECK_EQ(decoder->decoded_frames(), 0U);
    CHECK_FALSE(decoder->finished());
    CHECK_EQ(decoder->predictor(0), 0);
    CHECK_EQ(decoder->predictor(1), 0);
    CHECK_EQ(decoder->step_index(0), 0);
    CHECK_EQ(decoder->step_index(1), 0);

    std::vector<std::int16_t> second(4, 0);
    CHECK_EQ(decoder->decode_frames(std::span<std::int16_t>{second}), 2U);
    CHECK(first == second);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// bugprone-unchecked-optional-access, cppcoreguidelines-pro-bounds-constant-array-index)

#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-magic-numbers)

#include <fmt/format.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/QdAdp.hpp"

namespace {

constexpr std::string_view K_ADP_RELATIVE_PATH{"TRACKS/109.ADP"};
constexpr std::uint64_t K_PAYLOAD_SIZE{3419352};
constexpr std::uint64_t K_TOTAL_FRAMES{3419352};
constexpr std::uint64_t K_SAMPLE_RATE{22050};

constexpr std::array<std::uint32_t, 64> K_SHA256_K{0x428a2f98,
    0x71374491,
    0xb5c0fbcf,
    0xe9b5dba5,
    0x3956c25b,
    0x59f111f1,
    0x923f82a4,
    0xab1c5ed5,
    0xd807aa98,
    0x12835b01,
    0x243185be,
    0x550c7dc3,
    0x72be5d74,
    0x80deb1fe,
    0x9bdc06a7,
    0xc19bf174,
    0xe49b69c1,
    0xefbe4786,
    0x0fc19dc6,
    0x240ca1cc,
    0x2de92c6f,
    0x4a7484aa,
    0x5cb0a9dc,
    0x76f988da,
    0x983e5152,
    0xa831c66d,
    0xb00327c8,
    0xbf597fc7,
    0xc6e00bf3,
    0xd5a79147,
    0x06ca6351,
    0x14292967,
    0x27b70a85,
    0x2e1b2138,
    0x4d2c6dfc,
    0x53380d13,
    0x650a7354,
    0x766a0abb,
    0x81c2c92e,
    0x92722c85,
    0xa2bfe8a1,
    0xa81a664b,
    0xc24b8b70,
    0xc76c51a3,
    0xd192e819,
    0xd6990624,
    0xf40e3585,
    0x106aa070,
    0x19a4c116,
    0x1e376c08,
    0x2748774c,
    0x34b0bcb5,
    0x391c0cb3,
    0x4ed8aa4a,
    0x5b9cca4f,
    0x682e6ff3,
    0x748f82ee,
    0x78a5636f,
    0x84c87814,
    0x8cc70208,
    0x90befffa,
    0xa4506ceb,
    0xbef9a3f7,
    0xc67178f2};

[[nodiscard]] constexpr std::uint32_t rotr(const std::uint32_t value, const std::uint32_t bits) {
  return (value >> bits) | (value << (32U - bits));
}

/// Compact SHA-256 over little-endian signed 16-bit PCM serialized as-is.
[[nodiscard]] std::string sha256_of_pcm(const std::span<const std::int16_t> pcm) {
  std::array<std::uint32_t, 8> state{0x6a09e667,
      0xbb67ae85,
      0x3c6ef372,
      0xa54ff53a,
      0x510e527f,
      0x9b05688c,
      0x1f83d9ab,
      0x5be0cd19};

  const std::size_t byte_count{pcm.size() * sizeof(std::int16_t)};
  std::vector<std::byte> message(byte_count);
  std::memcpy(message.data(), pcm.data(), byte_count);

  const std::uint64_t bit_length{static_cast<std::uint64_t>(byte_count) * 8U};
  message.push_back(std::byte{0x80});
  while ((message.size() % 64U) != 56U) {
    message.push_back(std::byte{0});
  }
  for (int shift{56}; shift >= 0; shift -= 8) {
    message.push_back(static_cast<std::byte>((bit_length >> shift) & 0xFFU));
  }

  std::array<std::uint32_t, 64> w{};
  for (std::size_t chunk{0}; chunk < message.size(); chunk += 64U) {
    for (std::size_t index{0}; index < 16U; ++index) {
      const std::size_t base{chunk + index * 4U};
      w.at(index) = (static_cast<std::uint32_t>(message.at(base)) << 24U) |
                    (static_cast<std::uint32_t>(message.at(base + 1U)) << 16U) |
                    (static_cast<std::uint32_t>(message.at(base + 2U)) << 8U) |
                    static_cast<std::uint32_t>(message.at(base + 3U));
    }
    for (std::size_t index{16}; index < 64U; ++index) {
      const std::uint32_t s0{
          rotr(w.at(index - 15U), 7U) ^ rotr(w.at(index - 15U), 18U) ^ (w.at(index - 15U) >> 3U)};
      const std::uint32_t s1{
          rotr(w.at(index - 2U), 17U) ^ rotr(w.at(index - 2U), 19U) ^ (w.at(index - 2U) >> 10U)};
      w.at(index) = w.at(index - 16U) + s0 + w.at(index - 7U) + s1;
    }

    std::uint32_t a{state.at(0)};
    std::uint32_t b{state.at(1)};
    std::uint32_t c{state.at(2)};
    std::uint32_t d{state.at(3)};
    std::uint32_t e{state.at(4)};
    std::uint32_t f{state.at(5)};
    std::uint32_t g{state.at(6)};
    std::uint32_t h{state.at(7)};

    for (std::size_t index{0}; index < 64U; ++index) {
      const std::uint32_t s1{rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U)};
      const std::uint32_t choose{(e & f) ^ (~e & g)};
      const std::uint32_t temp1{h + s1 + choose + K_SHA256_K.at(index) + w.at(index)};
      const std::uint32_t s0{rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U)};
      const std::uint32_t majority{(a & b) ^ (a & c) ^ (b & c)};
      const std::uint32_t temp2{s0 + majority};
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state.at(0) += a;
    state.at(1) += b;
    state.at(2) += c;
    state.at(3) += d;
    state.at(4) += e;
    state.at(5) += f;
    state.at(6) += g;
    state.at(7) += h;
  }

  std::string hex;
  hex.reserve(64);
  for (const std::uint32_t word : state) {
    hex += fmt::format("{:08x}", word);
  }
  return hex;
}

}  // namespace

TEST_SUITE("Core::Omikron::QdAdpIntegration") {
  TEST_CASE("[RETAIL] 109.ADP matches verified facts and PCM oracle") {
    const auto file{App::load_game_file(K_ADP_RELATIVE_PATH)};
    REQUIRE_MESSAGE(file.has_value(), file.error());

    CHECK_EQ(file->bytes.size(), K_PAYLOAD_SIZE + 0x10U);

    auto decoder{App::Omikron::QdAdpDecoder::create(file->bytes)};
    REQUIRE_MESSAGE(decoder.has_value(), decoder.error());
    CHECK_EQ(decoder->channels(), 2);
    CHECK_EQ(decoder->sample_rate(), static_cast<int>(K_SAMPLE_RATE));
    CHECK_EQ(decoder->total_frames(), K_TOTAL_FRAMES);

    // First sixteen decoded stereo frames.
    const std::array<std::int16_t, 16> expected_first{
        12, 12, 40, 40, 40, 40, 40, 40, 12, 12, 29, 29, 36, 36, 36, 36};
    std::vector<std::int16_t> first(32);
    REQUIRE_EQ(decoder->decode_frames(std::span<std::int16_t>{first}), 16U);
    for (std::size_t index{0}; index < expected_first.size(); ++index) {
      CHECK_EQ(first.at(index), expected_first.at(index));
    }

    // First 4096 decoded stereo frames, serialized as little-endian S16 PCM.
    decoder->rewind();
    std::vector<std::int16_t> pcm(4096U * 2U);
    REQUIRE_EQ(decoder->decode_frames(std::span<std::int16_t>{pcm}), 4096U);
    CHECK_EQ(sha256_of_pcm(std::span<const std::int16_t>{pcm}),
        "c5ffefdea4ac90a6125130e78399b8d451f11d011e27e7241861ddd69caeb23b");
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-magic-numbers)

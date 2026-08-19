#include "Core/Omikron/QdAdp.hpp"

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// The decoder writes into caller-provided spans, which have no bounds-checked
// element accessor; the write index is kept within the computed frame count.

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace App::Omikron {

namespace {

constexpr std::size_t K_HEADER_SIZE{0x10};
constexpr std::uint32_t K_PAYLOAD_OFFSET{0x04};

/// The standard 89-entry IMA step-size table.
constexpr std::array<std::int32_t, 89> K_IMA_STEP_TABLE{
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

/// The standard 16-entry IMA index-adjustment table.
constexpr std::array<int, 16> K_IMA_INDEX_TABLE{
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

/// Clamps a decoded predictor sum into signed 16-bit range.
[[nodiscard]] std::int16_t clamp_to_int16(const std::int32_t value) {
  return static_cast<std::int16_t>(
      std::clamp(value,
          static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
          static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())));
}

/// Reads a little-endian 24-bit unsigned integer from `data` at `offset`.
[[nodiscard]] std::uint32_t read_u24_le(const std::span<const std::byte> data,
    const std::size_t offset) {
  const std::uint32_t byte0{static_cast<std::uint32_t>(data[offset])};
  const std::uint32_t byte1{static_cast<std::uint32_t>(data[offset + 1U])};
  const std::uint32_t byte2{static_cast<std::uint32_t>(data[offset + 2U])};
  return byte0 | (byte1 << 8U) | (byte2 << 16U);
}

}  // namespace

std::expected<QdAdpFile, std::string> QdAdpFile::load(const std::span<const std::byte> data) {
  if (data.size() < K_HEADER_SIZE) {
    return std::expected<QdAdpFile, std::string>{std::unexpect,
        fmt::format("ADP file is only {} bytes; expected at least 16-byte header", data.size())};
  }

  const std::uint32_t payload_size{read_u24_le(data, 0x00)};
  const std::uint8_t stereo_flag{static_cast<std::uint8_t>(data[0x03])};
  if (stereo_flag > 1U) {
    return std::expected<QdAdpFile, std::string>{std::unexpect,
        fmt::format("ADP stereo flag {} is invalid (expected 0 or 1)", stereo_flag)};
  }

  for (std::size_t offset{K_PAYLOAD_OFFSET}; offset < K_HEADER_SIZE; ++offset) {
    if (data[offset] != std::byte{0}) {
      return std::expected<QdAdpFile, std::string>{std::unexpect,
          fmt::format("ADP reserved header byte at offset {:#x} is nonzero", offset)};
    }
  }

  const std::uint64_t expected_size{
      static_cast<std::uint64_t>(payload_size) + K_HEADER_SIZE};
  if (expected_size != data.size()) {
    return std::expected<QdAdpFile, std::string>{std::unexpect,
        fmt::format("ADP payload size {} (+16 = {}) does not match file size {}",
            payload_size,
            expected_size,
            data.size())};
  }

  QdAdpFile file;
  file.m_payload_size = payload_size;
  file.m_stereo_flag = stereo_flag;
  file.m_payload = data.subspan(K_HEADER_SIZE, payload_size);
  return file;
}

std::expected<QdAdpDecoder, std::string> QdAdpDecoder::create(
    const std::span<const std::byte> file) {
  auto parsed{QdAdpFile::load(file)};
  if (!parsed) {
    return std::expected<QdAdpDecoder, std::string>{std::unexpect, parsed.error()};
  }

  QdAdpDecoder decoder;
  decoder.m_file = std::move(parsed).value();
  decoder.m_channels.fill(QdImaChannelState{});
  return decoder;
}

void QdAdpDecoder::rewind() {
  m_read_position = 0;
  m_decoded_frames = 0;
  m_channels.fill(QdImaChannelState{});
}

std::int16_t QdAdpDecoder::decode_nibble(const std::uint8_t nibble, const int channel) {
  QdImaChannelState& state{m_channels.at(static_cast<std::size_t>(channel))};

  const int magnitude{static_cast<int>(nibble & 0x07U)};
  const std::int32_t step{K_IMA_STEP_TABLE.at(static_cast<std::size_t>(state.step_index))};

  std::int32_t delta{0};
  if ((magnitude & 0x04) != 0) {
    delta += step * 4;
  }
  if ((magnitude & 0x02) != 0) {
    delta += step * 2;
  }
  if ((magnitude & 0x01) != 0) {
    delta += step;
  }
  delta >>= 2;
  if ((nibble & 0x08U) != 0U) {
    delta = -delta;
  }

  state.predictor = clamp_to_int16(state.predictor + delta);

  state.step_index += K_IMA_INDEX_TABLE.at(nibble);
  state.step_index = std::clamp(state.step_index, 0, 88);

  return static_cast<std::int16_t>(state.predictor);
}

std::size_t QdAdpDecoder::decode_frames(std::span<std::int16_t> interleaved_pcm) {
  const std::size_t channel_count{static_cast<std::size_t>(channels())};
  if (channel_count == 0) {
    return 0;
  }
  const std::size_t capacity_frames{interleaved_pcm.size() / channel_count};
  const std::uint64_t remaining{total_frames() - m_decoded_frames};
  const std::size_t frames_to_decode{static_cast<std::size_t>(
      std::min<std::uint64_t>(remaining, capacity_frames))};

  std::size_t out_sample{0};
  for (std::size_t frame{0}; frame < frames_to_decode; ++frame) {
    if (m_read_position >= m_file.payload().size()) {
      break;  // Truncated payload: stop rather than read past the end.
    }
    const std::uint8_t byte{
        static_cast<std::uint8_t>(m_file.payload()[m_read_position])};
    ++m_read_position;

    if (channel_count == 1U) {
      interleaved_pcm[out_sample++] = decode_nibble(byte >> 4U, 0);
      interleaved_pcm[out_sample++] = decode_nibble(byte & 0x0FU, 0);
    } else {
      interleaved_pcm[out_sample++] = decode_nibble(byte >> 4U, 0);
      interleaved_pcm[out_sample++] = decode_nibble(byte & 0x0FU, 1);
    }
  }

  m_decoded_frames += frames_to_decode;
  return frames_to_decode;
}

}  // namespace App::Omikron

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

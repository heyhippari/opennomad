#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace App::Omikron {

/// Per-channel QD IMA decoder state. The codec always begins with a zero
/// predictor and a zero step index, and these are reset on rewind.
struct QdImaChannelState {
  std::int32_t predictor{0};
  int step_index{0};
};

/// Immutable parsed container metadata and payload view for Omikron's ADP
/// music format. The payload view borrows from the caller's byte buffer, so
/// the buffer must outlive the parser result.
///
/// File layout (16-byte header followed by compressed QD IMA data):
///   0x00  3 bytes  compressed payload size, little-endian 24-bit
///   0x03  1 byte   stereo flag (0 = mono, 1 = stereo)
///   0x04  12 bytes must be zero
///   0x10  ...      compressed QD IMA data
class QdAdpFile {
 public:
  /// Parses and validates the 16-byte header plus payload sizing.
  [[nodiscard]] static std::expected<QdAdpFile, std::string> load(std::span<const std::byte> data);

  /// 0 for mono, 1 for stereo (the raw header byte).
  [[nodiscard]] std::uint8_t stereo_flag() const {
    return m_stereo_flag;
  }

  /// 1 for mono, 2 for stereo.
  [[nodiscard]] int channels() const {
    return m_stereo_flag == 0 ? 1 : 2;
  }

  /// The fixed QD ADP sample rate (22050 Hz).
  [[nodiscard]] int sample_rate() const {
    return 22050;
  }

  /// Frames per channel: payload_size * 2 / channels.
  [[nodiscard]] std::uint64_t total_frames() const {
    return (static_cast<std::uint64_t>(m_payload_size) * 2U) /
           static_cast<std::uint64_t>(channels());
  }

  /// The compressed payload (excludes the 16-byte header).
  [[nodiscard]] std::span<const std::byte> payload() const {
    return m_payload;
  }

 private:
  std::uint32_t m_payload_size{0};
  std::uint8_t m_stereo_flag{0};
  std::span<const std::byte> m_payload;
};

/// Incremental QD IMA decoder over an ADP file. The container metadata is
/// immutable; the decoder keeps the mutable predictor/index/read-position
/// state and can decode arbitrary frame counts, finishing with the payload.
class QdAdpDecoder {
 public:
  /// Parses `file` (header + payload) and initializes the per-channel state.
  [[nodiscard]] static std::expected<QdAdpDecoder, std::string> create(
      std::span<const std::byte> file);

  [[nodiscard]] int sample_rate() const {
    return m_file.sample_rate();
  }
  [[nodiscard]] int channels() const {
    return m_file.channels();
  }
  [[nodiscard]] std::uint64_t total_frames() const {
    return m_file.total_frames();
  }
  /// Frames decoded so far (across all rewind cycles since the last rewind).
  [[nodiscard]] std::uint64_t decoded_frames() const {
    return m_decoded_frames;
  }
  [[nodiscard]] bool finished() const {
    return m_decoded_frames >= m_file.total_frames();
  }

  /// Decodes up to `interleaved_pcm.size() / channels()` frames into the
  /// interleaved little-endian signed 16-bit buffer and returns the number of
  /// frames actually decoded. Decoding past the payload stops early.
  std::size_t decode_frames(std::span<std::int16_t> interleaved_pcm);

  /// Resets the payload read position, decoded-frame count, and every
  /// channel predictor/step index so decoding restarts from the beginning.
  void rewind();

  /// Per-channel decoder-state accessors (diagnostics and tests).
  [[nodiscard]] std::int32_t predictor(int channel) const {
    return m_channels.at(static_cast<std::size_t>(channel)).predictor;
  }
  [[nodiscard]] int step_index(int channel) const {
    return m_channels.at(static_cast<std::size_t>(channel)).step_index;
  }

 private:
  /// Decodes one 4-bit nibble for `channel` and returns the resulting sample.
  [[nodiscard]] std::int16_t decode_nibble(std::uint8_t nibble, int channel);

  QdAdpFile m_file;
  std::size_t m_read_position{0};
  std::uint64_t m_decoded_frames{0};
  std::array<QdImaChannelState, 2> m_channels{};
};

}  // namespace App::Omikron

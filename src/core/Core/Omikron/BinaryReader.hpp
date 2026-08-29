#pragma once

#include <fmt/format.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace App::Omikron {

/// Bounds-checked little-endian reader over an in-memory byte buffer.
///
/// Reads that would run past the end of the buffer set an error state and
/// return zero-initialised values; callers check has_error() after parsing.
class BinaryReader {
 public:
  explicit BinaryReader(std::span<const std::byte> data) : m_data(data) {}

  [[nodiscard]] std::uint8_t read_u8() {
    return read_scalar<std::uint8_t>();
  }
  [[nodiscard]] std::uint16_t read_u16() {
    return read_scalar<std::uint16_t>();
  }
  [[nodiscard]] std::uint32_t read_u32() {
    return read_scalar<std::uint32_t>();
  }
  [[nodiscard]] std::uint64_t read_u64() {
    return read_scalar<std::uint64_t>();
  }
  [[nodiscard]] std::int32_t read_i32() {
    return read_scalar<std::int32_t>();
  }
  [[nodiscard]] float read_f32() {
    return std::bit_cast<float>(read_scalar<std::uint32_t>());
  }

  /// Returns a view of count bytes at the current position and advances past
  /// them. An empty span plus an error state signals an out-of-range read.
  [[nodiscard]] std::span<const std::byte> read_bytes(std::size_t count);

  void skip(std::size_t count);
  void seek(std::size_t position);

  [[nodiscard]] std::size_t tell() const {
    return m_position;
  }
  [[nodiscard]] std::size_t remaining() const {
    return m_data.size() - m_position;
  }

  [[nodiscard]] bool has_error() const {
    return m_has_error;
  }
  [[nodiscard]] const std::string& error() const {
    return m_error;
  }

 private:
  template <typename T>
  [[nodiscard]] T read_scalar();

  std::span<const std::byte> m_data;
  std::size_t m_position{0};
  bool m_has_error{false};
  std::string m_error;
};

template <typename T>
T BinaryReader::read_scalar() {
  if (m_has_error) {
    return T{};
  }
  if (sizeof(T) > remaining()) {
    m_has_error = true;
    m_error = fmt::format("read of {} bytes at offset {} exceeds buffer size {}",
        sizeof(T),
        m_position,
        m_data.size());
    return T{};
  }

  std::array<std::byte, sizeof(T)> bytes{};
  for (std::size_t index{0}; index < bytes.size(); ++index) {
    bytes.at(index) = m_data[m_position + index];
  }
  m_position += sizeof(T);

  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  return value;
}

inline std::span<const std::byte> BinaryReader::read_bytes(const std::size_t count) {
  if (m_has_error) {
    return {};
  }
  if (count > remaining()) {
    m_has_error = true;
    m_error = fmt::format(
        "read of {} bytes at offset {} exceeds buffer size {}", count, m_position, m_data.size());
    return {};
  }

  const std::span<const std::byte> bytes{m_data.subspan(m_position, count)};
  m_position += count;
  return bytes;
}

inline void BinaryReader::skip(const std::size_t count) {
  if (m_has_error) {
    return;
  }
  if (count > remaining()) {
    m_has_error = true;
    m_error = fmt::format(
        "skip of {} bytes at offset {} exceeds buffer size {}", count, m_position, m_data.size());
    m_position = m_data.size();
    return;
  }
  m_position += count;
}

inline void BinaryReader::seek(const std::size_t position) {
  if (m_has_error) {
    return;
  }
  if (position > m_data.size()) {
    m_has_error = true;
    m_error = fmt::format("seek to offset {} exceeds buffer size {}", position, m_data.size());
    m_position = m_data.size();
    return;
  }
  m_position = position;
}

}  // namespace App::Omikron

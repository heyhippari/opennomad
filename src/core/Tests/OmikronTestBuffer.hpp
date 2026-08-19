#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

/// Minimal little-endian buffer builder for hand-crafted .3DO/.3DT fixtures.
class Buffer {
 public:
  Buffer& u8(const std::uint8_t value) {
    m_data.push_back(static_cast<std::byte>(value));
    return *this;
  }

  Buffer& u16(const std::uint16_t value) {
    m_data.push_back(static_cast<std::byte>(value & 0xFFU));
    m_data.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    return *this;
  }

  Buffer& u32(const std::uint32_t value) {
    for (std::size_t shift{0}; shift < 32U; shift += 8U) {
      m_data.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
    return *this;
  }

  Buffer& u64(const std::uint64_t value) {
    for (std::size_t shift{0}; shift < 64U; shift += 8U) {
      m_data.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
    return *this;
  }

  Buffer& i32(const std::int32_t value) {
    return u32(static_cast<std::uint32_t>(value));
  }

  Buffer& f32(const float value) {
    return u32(std::bit_cast<std::uint32_t>(value));
  }

  /// Appends width bytes: the text followed by NUL padding.
  Buffer& chars(const std::string_view text, const std::size_t width) {
    for (std::size_t index{0}; index < width; ++index) {
      const char character{index < text.size() ? text[index] : '\0'};
      m_data.push_back(static_cast<std::byte>(character));
    }
    return *this;
  }

  Buffer& zeros(const std::size_t count) {
    m_data.insert(m_data.end(), count, std::byte{});
    return *this;
  }

  [[nodiscard]] const std::vector<std::byte>& data() const { return m_data; }

 private:
  std::vector<std::byte> m_data;
};

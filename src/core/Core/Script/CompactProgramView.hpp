#pragma once

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace App::Script {

/// Bounded IAM compact bytecode plus the record-relative offset represented
/// by byte zero. Serialized event offsets are rebased only after validation.
class CompactProgramView {
 public:
  [[nodiscard]] static std::expected<CompactProgramView, std::string> create(
      const std::span<const std::byte> bytes, const std::uint32_t record_origin) {
    constexpr std::uint64_t k_serialized_end_limit{
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U};
    const std::uint64_t record_end{static_cast<std::uint64_t>(record_origin) + bytes.size()};
    if (record_end > k_serialized_end_limit) {
      return std::expected<CompactProgramView, std::string>{std::unexpect,
          fmt::format("compact program [{:#x}, {:#x}) exceeds the uint32 record-offset domain",
              record_origin,
              record_end)};
    }
    return CompactProgramView{bytes, record_origin};
  }

  [[nodiscard]] std::span<const std::byte> bytes() const {
    return m_bytes;
  }

  [[nodiscard]] std::uint32_t record_origin() const {
    return m_record_origin;
  }

  [[nodiscard]] std::expected<std::optional<std::size_t>, std::string> rebase_entry(
      const std::uint32_t serialized_offset, const std::string_view source) const {
    if (serialized_offset == 0U) {
      return std::optional<std::size_t>{};
    }
    const std::uint64_t record_end{static_cast<std::uint64_t>(m_record_origin) + m_bytes.size()};
    if (serialized_offset < m_record_origin || serialized_offset >= record_end) {
      return std::expected<std::optional<std::size_t>, std::string>{std::unexpect,
          fmt::format("{} offset {:#x} is outside compact program [{:#x}, {:#x})",
              source,
              serialized_offset,
              m_record_origin,
              record_end)};
    }
    return std::optional<std::size_t>{serialized_offset - m_record_origin};
  }

 private:
  CompactProgramView(const std::span<const std::byte> bytes, const std::uint32_t record_origin)
      : m_bytes(bytes),
        m_record_origin(record_origin) {}

  std::span<const std::byte> m_bytes;
  std::uint32_t m_record_origin{0};
};

}  // namespace App::Script
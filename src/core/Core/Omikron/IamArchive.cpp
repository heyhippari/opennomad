#include "Core/Omikron/IamArchive.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"

namespace App::Omikron {

namespace {

/// Reads a little-endian u32 from `data` at `offset`. The caller must have
/// already validated that offset + 4 is within the span.
std::uint32_t read_u32_at(const std::span<const std::byte> data, const std::size_t offset) {
  std::uint32_t value{0};
  std::memcpy(&value, data.subspan(offset, 4U).data(), sizeof(value));
  return value;
}

}  // namespace

std::expected<std::size_t, std::string> IamIndexedArchive::index_entry_offset(
    const std::uint32_t id) {
  APP_PROFILE_FUNCTION();

  const std::uint32_t page{id >> 8U};
  const std::uint32_t entry{id & 0xFFU};

  const std::uint64_t page_offset{static_cast<std::uint64_t>(page) * k_index_page_size};
  const std::uint64_t entry_offset{
      page_offset + (static_cast<std::uint64_t>(entry) * k_index_entry_size)};

  if (entry_offset > std::numeric_limits<std::size_t>::max()) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, fmt::format("index entry offset for record {} overflows size_t", id)};
  }
  return static_cast<std::size_t>(entry_offset);
}

std::expected<std::span<const std::byte>, std::string> IamIndexedArchive::read_record(
    const std::uint32_t id) const {
  APP_PROFILE_FUNCTION();

  auto entry_offset{index_entry_offset(id)};
  if (!entry_offset) {
    return std::expected<std::span<const std::byte>, std::string>{
        std::unexpect, std::move(entry_offset).error()};
  }

  const std::size_t entry_position{*entry_offset};
  const std::size_t page_position{
      (entry_position / k_index_page_size) * k_index_page_size};
  if (page_position > m_data.size() ||
      k_index_page_size > (m_data.size() - page_position)) {
    return std::expected<std::span<const std::byte>, std::string>{std::unexpect,
        fmt::format("IAM archive: index page for record {} at {:#x} is truncated in the {} byte "
                    "archive",
            id,
            page_position,
            m_data.size())};
  }
  if ((entry_position + k_index_entry_size) > m_data.size()) {
    return std::expected<std::span<const std::byte>, std::string>{std::unexpect,
        fmt::format("IAM archive: index entry for record {} at {:#x} is outside the {} byte "
                    "archive",
            id,
            entry_position,
            m_data.size())};
  }

  const std::uint32_t offset{read_u32_at(m_data, entry_position)};
  const std::uint32_t size{read_u32_at(m_data, entry_position + 4U)};

  if (size == 0U) {
    return std::expected<std::span<const std::byte>, std::string>{
        std::unexpect, fmt::format("IAM archive: record {} is absent (zero size)", id)};
  }

  const std::uint64_t data_end{static_cast<std::uint64_t>(offset) + size};
  if (offset > m_data.size() || data_end > m_data.size()) {
    return std::expected<std::span<const std::byte>, std::string>{std::unexpect,
        fmt::format("IAM archive: record {} data range [{:#x}, {:#x}) is outside the {} byte "
                    "archive",
            id,
            offset,
            data_end,
            m_data.size())};
  }

  return m_data.subspan(offset, size);
}

std::expected<std::span<const std::byte>, std::string> IamFixedStrideArchive::read_record(
    const std::uint32_t id) const {
  APP_PROFILE_FUNCTION();

  if (m_record_size == 0U || m_stride == 0U) {
    return std::expected<std::span<const std::byte>, std::string>{
        std::unexpect, "IAM fixed-stride archive has a zero record size or stride"};
  }
  if (m_record_size > m_stride) {
    return std::expected<std::span<const std::byte>, std::string>{std::unexpect,
        fmt::format("IAM fixed-stride archive record size {:#x} exceeds stride {:#x}",
            m_record_size,
            m_stride)};
  }

  const std::uint64_t offset{
      static_cast<std::uint64_t>(id) * static_cast<std::uint64_t>(m_stride)};
  const std::uint64_t end{offset + static_cast<std::uint64_t>(m_record_size)};
  if (offset > std::numeric_limits<std::size_t>::max() ||
      end > std::numeric_limits<std::size_t>::max()) {
    return std::expected<std::span<const std::byte>, std::string>{
        std::unexpect, fmt::format("IAM fixed-stride record {} offset overflows size_t", id)};
  }
  if (end > m_data.size()) {
    return std::expected<std::span<const std::byte>, std::string>{std::unexpect,
        fmt::format("IAM fixed-stride record {} range [{:#x}, {:#x}) is outside the {} byte archive",
            id,
            offset,
            end,
            m_data.size())};
  }
  return m_data.subspan(static_cast<std::size_t>(offset), m_record_size);
}

}  // namespace App::Omikron

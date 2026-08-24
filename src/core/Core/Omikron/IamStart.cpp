#include "Core/Omikron/IamStart.hpp"

#include <fmt/format.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Omikron/IamCharacterDefinition.hpp"

namespace App::Omikron {

namespace {

template <typename Value>
Value read_at(const std::span<const std::byte> data, const std::size_t offset) {
  Value value{};
  std::memcpy(&value, data.subspan(offset, sizeof(Value)).data(), sizeof(value));
  return value;
}

std::string fixed_string(const std::span<const std::byte> data) {
  const void* raw{data.data()};
  const char* begin{static_cast<const char*>(raw)};
  const void* nul{std::memchr(raw, '\0', data.size())};
  const std::size_t size{nul == nullptr
                             ? data.size()
                             : static_cast<std::size_t>(static_cast<const char*>(nul) - begin)};
  return std::string{begin, size};
}

std::expected<void, std::string> validate_width(
    const std::string& name, const std::size_t byte_count, const std::size_t width) {
  if ((byte_count % width) != 0U) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format(
            "IAM/START: {} region has {} bytes, not a multiple of {}", name, byte_count, width)};
  }
  return {};
}

}  // namespace

std::expected<IamStart, std::string> IamStart::load(const std::span<const std::byte> data) {
  APP_PROFILE_FUNCTION();

  if (data.size() < k_min_size) {
    return std::expected<IamStart, std::string>{std::unexpect,
        fmt::format(
            "IAM/START: file too small ({} bytes, expected at least {})", data.size(), k_min_size)};
  }

  std::array<std::uint32_t, k_region_count> offsets{};
  for (std::size_t index{0}; index < offsets.size(); ++index) {
    offsets.at(index) = read_at<std::uint32_t>(
        data, k_global_variables_begin_offset + (index * sizeof(std::uint32_t)));
  }
  if (offsets.front() < k_min_size) {
    return std::expected<IamStart, std::string>{std::unexpect,
        fmt::format("IAM/START: global-variable region begins at {:#x}, before fixed state ends at "
                    "{:#x}",
            offsets.front(),
            k_min_size)};
  }
  for (std::size_t index{1}; index < offsets.size(); ++index) {
    if (offsets.at(index) < offsets.at(index - 1U)) {
      return std::expected<IamStart, std::string>{std::unexpect,
          fmt::format("IAM/START: region boundary {} at {:#x} precedes boundary {} at {:#x}",
              index,
              offsets.at(index),
              index - 1U,
              offsets.at(index - 1U))};
    }
  }
  if (offsets.back() > data.size()) {
    return std::expected<IamStart, std::string>{std::unexpect,
        fmt::format("IAM/START: ZONE region begins at {:#x}, beyond logical end {:#x}",
            offsets.back(),
            data.size())};
  }

  if (auto width{
          validate_width("global-variable", offsets.at(1) - offsets.at(0), sizeof(std::int32_t))};
      !width) {
    return std::expected<IamStart, std::string>{std::unexpect, width.error()};
  }
  if (auto width{validate_width("area-map", offsets.at(2) - offsets.at(1), sizeof(std::int16_t))};
      !width) {
    return std::expected<IamStart, std::string>{std::unexpect, width.error()};
  }

  IamStart start{data, offsets};
  auto current{start.current_character()};
  if (!current) {
    return std::expected<IamStart, std::string>{
        std::unexpect, fmt::format("IAM/START: current character: {}", current.error())};
  }
  return start;
}

std::uint32_t IamStart::format_revision() const {
  return read_at<std::uint32_t>(m_data, k_format_revision_offset);
}

std::uint32_t IamStart::build_date() const {
  return read_at<std::uint32_t>(m_data, k_build_date_offset);
}

std::array<std::int32_t, 3> IamStart::saved_position() const {
  return {read_at<std::int32_t>(m_data, k_saved_position_offset),
      read_at<std::int32_t>(m_data, k_saved_position_offset + sizeof(std::int32_t)),
      read_at<std::int32_t>(m_data, k_saved_position_offset + (2U * sizeof(std::int32_t)))};
}

std::int32_t IamStart::saved_orientation() const {
  return read_at<std::int32_t>(m_data, k_saved_orientation_offset);
}

std::int16_t IamStart::initial_area_id() const {
  return read_at<std::int16_t>(m_data, k_initial_area_offset);
}

std::int16_t IamStart::linked_area_id() const {
  return read_at<std::int16_t>(m_data, k_linked_area_offset);
}

std::span<const std::byte> IamStart::opaque_header_state() const {
  return m_data.subspan(k_opaque_header_offset, k_opaque_header_size);
}

std::span<const std::byte> IamStart::opaque_area_state() const {
  return m_data.subspan(k_opaque_area_offset, k_opaque_area_size);
}

std::span<const std::byte> IamStart::signs_buffer() const {
  return m_data.subspan(k_signs_offset, k_character_text_size);
}

std::span<const std::byte> IamStart::interests_buffer() const {
  return m_data.subspan(k_interests_offset, k_character_text_size);
}

std::expected<std::optional<IamCharacterDefinition>, std::string> IamStart::current_character()
    const {
  const std::span<const std::byte> record{
      m_data.subspan(k_current_character_offset, k_current_character_size)};
  if (read_at<std::int16_t>(record, 0x110U) == -1) {
    return std::optional<IamCharacterDefinition>{};
  }
  auto parsed{parse_iam_character_definition(record,
      std::optional<std::string>{fixed_string(signs_buffer())},
      std::optional<std::string>{fixed_string(interests_buffer())})};
  if (!parsed) {
    return std::expected<std::optional<IamCharacterDefinition>, std::string>{
        std::unexpect, parsed.error()};
  }
  return std::optional<IamCharacterDefinition>{std::move(parsed).value()};
}

std::span<const std::byte> IamStart::region(
    const std::size_t begin_index, const std::size_t end_index) const {
  const std::size_t begin{m_region_offsets.at(begin_index)};
  const std::size_t end{
      end_index < m_region_offsets.size() ? m_region_offsets.at(end_index) : m_data.size()};
  return m_data.subspan(begin, end - begin);
}

std::expected<std::span<const std::byte>, std::string> IamStart::global_variables() const {
  return region(0, 1);
}

std::expected<std::span<const std::byte>, std::string> IamStart::area_mappings() const {
  return region(1, 2);
}

std::expected<std::span<const std::byte>, std::string> IamStart::packed_state_bytes() const {
  return region(2, 3);
}

std::expected<std::span<const std::byte>, std::string> IamStart::character_flags() const {
  return region(3, 4);
}

std::expected<std::span<const std::byte>, std::string> IamStart::address_flags() const {
  return region(4, 5);
}

std::expected<std::span<const std::byte>, std::string> IamStart::zone_flags() const {
  return region(5, m_region_offsets.size());
}

std::expected<std::span<const std::byte>, std::string> IamStart::persistent_object_collection(
    const std::uint16_t kind) const {
  std::size_t offset{0};
  std::size_t capacity{0};
  switch (kind) {
    case 0:
      offset = k_object_collection_0_offset;
      capacity = k_object_collection_0_capacity;
      break;
    case 1:
      offset = k_object_collection_1_offset;
      capacity = k_object_collection_1_capacity;
      break;
    case 2:
      offset = k_object_collection_2_offset;
      capacity = k_object_collection_2_capacity;
      break;
    default:
      return std::expected<std::span<const std::byte>, std::string>{std::unexpect,
          fmt::format("IAM/START: persistent object collection kind {} is unsupported", kind)};
  }
  return m_data.subspan(offset, capacity * sizeof(std::int16_t));
}

}  // namespace App::Omikron

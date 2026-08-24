#include "Core/Omikron/IamStart.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>

#include "Core/Debug/Instrumentor.hpp"

namespace App::Omikron {

namespace {

std::int16_t read_i16_at(const std::span<const std::byte> data, const std::size_t offset) {
  std::int16_t value{0};
  std::memcpy(&value, data.subspan(offset, 2U).data(), sizeof(value));
  return value;
}

std::uint32_t read_u32_at(const std::span<const std::byte> data, const std::size_t offset) {
  std::uint32_t value{0};
  std::memcpy(&value, data.subspan(offset, 4U).data(), sizeof(value));
  return value;
}

}  // namespace

std::expected<IamStart, std::string> IamStart::load(const std::span<const std::byte> data) {
  APP_PROFILE_FUNCTION();

  if (data.size() < k_min_size) {
    return std::expected<IamStart, std::string>{std::unexpect,
        fmt::format(
            "IAM/START: file too small ({} bytes, expected at least {})", data.size(), k_min_size)};
  }

  return IamStart{data,
      read_i16_at(data, k_initial_area_offset),
      read_i16_at(data, k_linked_area_offset),
      read_u32_at(data, k_area_mapping_offset)};
}

std::expected<std::span<const std::byte>, std::string> IamStart::address_flags() const {
  const std::uint32_t begin{read_u32_at(m_data, k_address_flags_begin_offset)};
  const std::uint32_t end{read_u32_at(m_data, k_address_flags_end_offset)};
  if (begin > end) {
    return std::expected<std::span<const std::byte>, std::string>{std::unexpect,
        fmt::format("IAM/START: ADDRESS bounds are reversed ({:#x} > {:#x})", begin, end)};
  }
  if (end > m_data.size()) {
    return std::expected<std::span<const std::byte>, std::string>{std::unexpect,
        fmt::format("IAM/START: ADDRESS end {:#x} exceeds file size {:#x}", end, m_data.size())};
  }
  return m_data.subspan(begin, end - begin);
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
  constexpr std::size_t k_object_id_size{sizeof(std::int16_t)};
  const std::size_t byte_count{capacity * k_object_id_size};
  if (offset > m_data.size() || byte_count > (m_data.size() - offset)) {
    return std::expected<std::span<const std::byte>, std::string>{std::unexpect,
        fmt::format("IAM/START: persistent object collection {} exceeds file size", kind)};
  }
  return m_data.subspan(offset, byte_count);
}

}  // namespace App::Omikron

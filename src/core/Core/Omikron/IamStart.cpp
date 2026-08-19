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

}  // namespace App::Omikron

#include "Core/Omikron/IamObjectPlacement.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

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

}  // namespace

IamObjectPlacementRecord parse_iam_object_placement(const std::span<const std::byte, 0x18> record) {
  return IamObjectPlacementRecord{.runtime_object_slot_seed = read_at<std::int16_t>(record, 0x00U),
      .object_id = read_at<std::int16_t>(record, 0x02U),
      .serialized_position = {read_at<std::int32_t>(record, 0x04U),
          read_at<std::int32_t>(record, 0x08U),
          read_at<std::int32_t>(record, 0x0CU)},
      .orientation_units = {read_at<std::int16_t>(record, 0x10U),
          read_at<std::int16_t>(record, 0x12U),
          read_at<std::int16_t>(record, 0x14U)},
      .persistent_state_index = read_at<std::int16_t>(record, 0x16U)};
}

IamObjectDefinitionRecord parse_iam_object_definition(
    const std::span<const std::byte, 0x18> record) {
  IamObjectDefinitionRecord definition{.object_id = read_at<std::int16_t>(record, 0x00U),
      .type_or_flags = read_at<std::uint16_t>(record, 0x02U),
      .fields_04_0c = {},
      .model_resource = fixed_string(record.subspan<0x0EU, 10U>())};
  for (std::size_t index{0}; index < definition.fields_04_0c.size(); ++index) {
    definition.fields_04_0c.at(index) =
        read_at<std::uint16_t>(record, 0x04U + (index * sizeof(std::uint16_t)));
  }
  return definition;
}

}  // namespace App::Omikron
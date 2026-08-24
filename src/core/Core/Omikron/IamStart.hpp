#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

#include "Core/Omikron/IamCharacterDefinition.hpp"

namespace App::Omikron {

/// Checked immutable view over the complete recovered IAM/START layout.
/// Serialized offsets remain offsets and are never relocated into pointers.
class IamStart {
 public:
  static constexpr std::size_t k_format_revision_offset{0x00};
  static constexpr std::size_t k_build_date_offset{0x04};
  static constexpr std::size_t k_global_variables_begin_offset{0x08};
  static constexpr std::size_t k_global_variables_end_offset{0x0C};
  static constexpr std::size_t k_area_mapping_offset{k_global_variables_end_offset};
  static constexpr std::size_t k_packed_state_offset{0x10};
  static constexpr std::size_t k_character_flags_offset{0x14};
  static constexpr std::size_t k_address_flags_begin_offset{0x18};
  static constexpr std::size_t k_address_flags_end_offset{0x1C};
  static constexpr std::size_t k_region_count{6};

  static constexpr std::size_t k_opaque_header_offset{0x20};
  static constexpr std::size_t k_opaque_header_size{0x0C};
  static constexpr std::size_t k_saved_position_offset{0x2C};
  static constexpr std::size_t k_saved_orientation_offset{0x38};
  static constexpr std::size_t k_current_character_offset{0x3C};
  static constexpr std::size_t k_current_character_size{0x114};
  static constexpr std::size_t k_signs_offset{0x150};
  static constexpr std::size_t k_interests_offset{0x250};
  static constexpr std::size_t k_character_text_size{0x100};

  static constexpr std::size_t k_object_collection_0_offset{0x350};
  static constexpr std::size_t k_object_collection_1_offset{0x374};
  static constexpr std::size_t k_object_collection_2_offset{0x574};
  static constexpr std::size_t k_object_collection_0_capacity{18};
  static constexpr std::size_t k_object_collection_1_capacity{256};
  static constexpr std::size_t k_object_collection_2_capacity{9};

  static constexpr std::size_t k_initial_area_offset{0x586};
  static constexpr std::size_t k_linked_area_offset{0x588};
  static constexpr std::size_t k_opaque_area_offset{0x58A};
  static constexpr std::size_t k_opaque_area_size{2};
  static constexpr std::size_t k_min_size{0x58C};
  static constexpr std::size_t k_retail_size{0x1636};

  [[nodiscard]] static std::expected<IamStart, std::string> load(std::span<const std::byte> data);

  [[nodiscard]] std::uint32_t format_revision() const;
  [[nodiscard]] std::uint32_t build_date() const;
  [[nodiscard]] std::array<std::int32_t, 3> saved_position() const;
  [[nodiscard]] std::int32_t saved_orientation() const;
  [[nodiscard]] std::int16_t initial_area_id() const;
  [[nodiscard]] std::int16_t current_area_id() const {
    return initial_area_id();
  }
  [[nodiscard]] std::int16_t linked_area_id() const;
  [[nodiscard]] std::uint32_t area_mapping_table_offset() const {
    return m_region_offsets.at(1);
  }

  [[nodiscard]] std::span<const std::byte> opaque_header_state() const;
  [[nodiscard]] std::span<const std::byte> opaque_area_state() const;
  [[nodiscard]] std::span<const std::byte> signs_buffer() const;
  [[nodiscard]] std::span<const std::byte> interests_buffer() const;

  [[nodiscard]] std::expected<std::optional<IamCharacterDefinition>, std::string>
  current_character() const;

  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> global_variables() const;
  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> area_mappings() const;
  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> packed_state_bytes() const;
  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> character_flags() const;
  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> address_flags() const;
  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> zone_flags() const;

  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> persistent_object_collection(
      std::uint16_t kind) const;

 private:
  explicit IamStart(
      std::span<const std::byte> data, std::array<std::uint32_t, k_region_count> region_offsets)
      : m_data(data),
        m_region_offsets(region_offsets) {}

  [[nodiscard]] std::span<const std::byte> region(
      std::size_t begin_index, std::size_t end_index) const;

  std::span<const std::byte> m_data;
  std::array<std::uint32_t, k_region_count> m_region_offsets{};
};

}  // namespace App::Omikron

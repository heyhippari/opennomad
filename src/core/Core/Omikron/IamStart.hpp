#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace App::Omikron {

/// Checked view over IAM/START. START is an immutable byte buffer; this view
/// exposes the startup fields recovered from Runtime.exe without mutating
/// serialized offsets into pointers.
class IamStart {
 public:
  /// Serialized offset of the area-mapping table.
  static constexpr std::size_t k_area_mapping_offset{0x0C};
  /// Signed little-endian initial area ID.
  static constexpr std::size_t k_initial_area_offset{0x586};
  /// Signed little-endian linked/secondary area ID.
  static constexpr std::size_t k_linked_area_offset{0x588};
  /// Minimum size required to read the three fields above (0x588 + 2).
  static constexpr std::size_t k_min_size{0x58A};

  /// Parses a START buffer, validating that it is large enough for the known
  /// startup fields. The returned view borrows `data`.
  [[nodiscard]] static std::expected<IamStart, std::string> load(std::span<const std::byte> data);

  /// Signed initial area ID (+0x586).
  [[nodiscard]] std::int16_t initial_area_id() const {
    return m_initial_area_id;
  }

  /// Signed linked/secondary area ID (+0x588).
  [[nodiscard]] std::int16_t linked_area_id() const {
    return m_linked_area_id;
  }

  /// Raw serialized offset of the area-mapping table (+0x0C). Resolving the
  /// table into elements is deferred until its element size/count are known.
  [[nodiscard]] std::uint32_t area_mapping_table_offset() const {
    return m_area_mapping_table_offset;
  }

 private:
  explicit IamStart(std::span<const std::byte> data,
      std::int16_t initial_area_id,
      std::int16_t linked_area_id,
      std::uint32_t area_mapping_table_offset)
      : m_data(data),
        m_initial_area_id(initial_area_id),
        m_linked_area_id(linked_area_id),
        m_area_mapping_table_offset(area_mapping_table_offset) {}

  std::span<const std::byte> m_data;
  std::int16_t m_initial_area_id{0};
  std::int16_t m_linked_area_id{0};
  std::uint32_t m_area_mapping_table_offset{0};
};

}  // namespace App::Omikron

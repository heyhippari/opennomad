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
  /// Header offsets delimiting the signed 32-bit global-variable region.
  static constexpr std::size_t k_global_variables_begin_offset{0x08};
  static constexpr std::size_t k_global_variables_end_offset{0x0C};
  /// Signed little-endian initial area ID.
  static constexpr std::size_t k_initial_area_offset{0x586};
  /// Signed little-endian linked/secondary area ID.
  static constexpr std::size_t k_linked_area_offset{0x588};
  /// Minimum size required to read the three fields above (0x588 + 2).
  static constexpr std::size_t k_min_size{0x58A};
  /// Header offsets delimiting the relocated persistent ADDRESS bit region.
  static constexpr std::size_t k_address_flags_begin_offset{0x18};
  static constexpr std::size_t k_address_flags_end_offset{0x1C};
  /// Fixed START locations and capacities of the three persistent object-ID
  /// collections. Their names deliberately remain numeric until broader
  /// gameplay semantics are recovered.
  static constexpr std::size_t k_object_collection_0_offset{0x350};
  static constexpr std::size_t k_object_collection_1_offset{0x374};
  static constexpr std::size_t k_object_collection_2_offset{0x574};
  static constexpr std::size_t k_object_collection_0_capacity{18};
  static constexpr std::size_t k_object_collection_1_capacity{256};
  static constexpr std::size_t k_object_collection_2_capacity{9};

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

  /// Checked immutable view of the relocated persistent ADDRESS bit region.
  /// The START header selects its boundaries, so this never assumes retail's
  /// 100-byte size for synthetic or future data.
  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> address_flags() const;

  /// Checked immutable bytes of the header-selected signed 32-bit global
  /// variables. The returned size is always divisible by four.
  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> global_variables() const;

  /// Checked immutable bytes of one fixed-capacity persistent object-ID
  /// collection. Each element is one little-endian signed 16-bit object ID.
  [[nodiscard]] std::expected<std::span<const std::byte>, std::string>
  persistent_object_collection(std::uint16_t kind) const;

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

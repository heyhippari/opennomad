#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace App::Omikron {

/// Checked reader for the paged indexed IAM archives (IAM/AREA and its
/// siblings). The archive is treated as an immutable byte span; individual
/// records are located through the little-endian index and returned as
/// non-owning views into that span.
///
/// Index layout (recovered from Runtime.exe):
/// - each page is 0x800 bytes and holds 256 eight-byte entries;
/// - record `id` lives in page `id >> 8`, entry `id & 0xff`;
/// - an entry is an absolute little-endian `{offset, size}` pair.
class IamIndexedArchive {
 public:
  static constexpr std::size_t k_index_page_size{0x800};
  static constexpr std::size_t k_index_entry_size{8};
  static constexpr std::size_t k_index_entries_per_page{256};

  explicit IamIndexedArchive(std::span<const std::byte> data) : m_data(data) {}

  /// Byte offset of the eight-byte index entry for `id`, with overflow-
  /// checked arithmetic. IDs above 255 spill into the next page.
  [[nodiscard]] static std::expected<std::size_t, std::string> index_entry_offset(std::uint32_t id);

  /// Reads the record with the given ID: locates and decodes its index entry
  /// and returns the record's byte span. Fails cleanly when the entry or the
  /// data range it describes lies outside the archive, when the size is
  /// implausible, or when the record is absent (zero size).
  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> read_record(
      std::uint32_t id) const;

  /// The backing bytes of the archive.
  [[nodiscard]] std::span<const std::byte> data() const {
    return m_data;
  }

 private:
  std::span<const std::byte> m_data;
};

/// Checked reader for Runtime's other IAM file-storage mode: fixed-size
/// records stored at a fixed byte stride, with no {offset,size} index.
///
/// Runtime's generic IAM loader takes both a record size and a final stride
/// argument. When that stride is positive it seeks directly to:
///
///     record_id * stride
///
/// and reads exactly `record_size` bytes. IAM/OBJECT uses this mode with a
/// 0x518-byte record in each 0x800-byte slot.
class IamFixedStrideArchive {
 public:
  IamFixedStrideArchive(
      std::span<const std::byte> data, std::size_t record_size, std::size_t stride)
      : m_data(data),
        m_record_size(record_size),
        m_stride(stride) {}

  /// Returns the fixed-size record for `id`. A zero-filled slot is still a
  /// present record; unlike IamIndexedArchive there is no serialized size
  /// field whose zero value can mean "absent".
  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> read_record(
      std::uint32_t id) const;

 private:
  std::span<const std::byte> m_data;
  std::size_t m_record_size{0};
  std::size_t m_stride{0};
};

}  // namespace App::Omikron

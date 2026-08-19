#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace App::Omikron {

/// Parsed, owning representation of one IAM/AREA record. Serialized offsets
/// remain immutable; runtime values (script span, table views) are computed
/// on demand from the owned bytes, never overwritten into pointers.
class IamAreaRecord {
 public:
  /// Fixed header size of a serialized area record.
  static constexpr std::size_t k_header_size{0xB4};
  /// Width of each fixed dependency-name field.
  static constexpr std::size_t k_name_field_size{9};
  /// Number of offset/count table pairs in the header.
  static constexpr std::size_t k_table_count{8};

  /// Header field offsets (serialized layout only).
  static constexpr std::size_t k_offset_runtime_context{0x00};
  static constexpr std::size_t k_offset_script{0x04};
  static constexpr std::size_t k_offset_related_area_ids{0x08};
  static constexpr std::size_t k_offset_table_offsets{0x28};
  static constexpr std::size_t k_offset_table_counts{0x48};
  static constexpr std::size_t k_offset_model3do_name{0x58};
  static constexpr std::size_t k_offset_scenario_scx_name{0x61};
  static constexpr std::size_t k_offset_map_mpt_name{0x6A};
  static constexpr std::size_t k_offset_options_opt_name{0x73};
  static constexpr std::size_t k_offset_animation_ani_name{0x7C};
  static constexpr std::size_t k_offset_secondary_3do_name{0x85};

  /// Parses an area record, validating the fixed header, the script offset
  /// and every known table span. The record owns its byte copy.
  [[nodiscard]] static std::expected<IamAreaRecord, std::string> load(
      std::span<const std::byte> data);

  /// Total record size in bytes.
  [[nodiscard]] std::size_t record_size() const {
    return m_bytes.size();
  }

  /// Runtime context placeholder (+0x00); zero on disk, kept as a runtime
  /// field rather than a pointer.
  [[nodiscard]] std::uint32_t runtime_context() const;

  /// Byte offset of the startup script within the record (+0x04).
  [[nodiscard]] std::uint32_t script_offset() const;

  /// Dependency names (+0x58..+0x8D), trimmed at the first NUL.
  [[nodiscard]] std::string model3do_name() const;
  [[nodiscard]] std::string scenario_scx_name() const;
  [[nodiscard]] std::string map_mpt_name() const;
  [[nodiscard]] std::string options_opt_name() const;
  [[nodiscard]] std::string animation_ani_name() const;
  [[nodiscard]] std::string secondary_3do_name() const;

  /// The startup script bytes: `[scriptOffset, record end)`.
  [[nodiscard]] std::span<const std::byte> script_bytes() const;

  /// Raw table offset (+0x28 + 4 * index).
  [[nodiscard]] std::uint32_t table_offset(std::size_t index) const;
  /// Raw table count (+0x48 + 2 * index).
  [[nodiscard]] std::uint16_t table_count(std::size_t index) const;

  /// Validated byte view of one table using its known stride. An empty table
  /// resolves to an empty span; a non-empty table whose stride is still
  /// unresolved is a structured error rather than a guessed view.
  [[nodiscard]] std::expected<std::span<const std::byte>, std::string> table_view(
      std::size_t index) const;

  /// Known serialized stride of a table, when established. Tables 0, 1, 2, 4,
  /// 5, 6 and 7 have confirmed strides; table 3's semantics remain unresolved.
  [[nodiscard]] static std::optional<std::size_t> known_table_stride(std::size_t index);

 private:
  explicit IamAreaRecord(std::vector<std::byte> bytes) : m_bytes(std::move(bytes)) {}

  /// Reads a fixed-width name field and trims it at the first NUL.
  [[nodiscard]] std::string name_at(std::size_t offset) const;

  std::vector<std::byte> m_bytes;
};

}  // namespace App::Omikron

#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace App::Omikron {

/// One NUL-separated string table as used by the IAM interface files
/// (IAM/Menu, IAM/Options, IAM/Save, ...). The table is a flat sequence of
/// NUL-terminated UTF-8/ASCII entries; the main-menu labels are recovered
/// from these files rather than hardcoded in any scene.
///
/// The class owns the copied byte storage; the string views returned by
/// at()/try_at() borrow from it, so the table must outlive every view.
class IamStringTable {
 public:
  /// An empty table (zero entries).
  IamStringTable() = default;

  /// Parses a NUL-separated string table. Empty input yields a valid table
  /// with zero entries (an "empty table" is not an error). Entries are the
  /// byte ranges between NULs; consecutive NULs produce empty entries, and a
  /// trailing non-NUL range becomes the final entry.
  [[nodiscard]] static std::expected<IamStringTable, std::string> load(
      std::span<const std::byte> data);

  /// Number of parsed entries.
  [[nodiscard]] std::size_t size() const {
    return m_strings.size();
  }

  /// Bounds-checked lookup: returns an empty view and logs a warning for an
  /// out-of-range index. Prefer try_at() at call sites that need to report
  /// the error themselves.
  [[nodiscard]] std::string_view at(std::size_t index) const;

  /// Bounds-checked lookup returning nullopt for an out-of-range index.
  [[nodiscard]] std::optional<std::string_view> try_at(std::size_t index) const;

 private:
  explicit IamStringTable(std::vector<std::byte> storage, std::vector<std::string_view> strings)
      : m_storage(std::move(storage)),
        m_strings(std::move(strings)) {}

  std::vector<std::byte> m_storage;
  std::vector<std::string_view> m_strings;
};

}  // namespace App::Omikron

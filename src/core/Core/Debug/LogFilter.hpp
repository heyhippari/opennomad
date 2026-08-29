#pragma once

#include <spdlog/common.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "Core/LogCategory.hpp"

namespace App::Debug {

/// Pure filtering logic for the in-app log viewer.
///
/// Kept free of any ImGui dependency so the severity/category/text matching
/// can be unit-tested directly. One instance per viewer; `matches` is const
/// and cheap, so it can be called once per buffered entry per frame.
struct LogFilter {
  spdlog::level::level_enum min_level{spdlog::level::debug};
  /// Category bitmask: bit (1 << category) selects whether that category is
  /// shown. All bits set by default (show everything).
  std::uint32_t category_mask{0xFFFF'FFFFU};
  /// Case-insensitive substring search; an empty filter matches everything.
  std::string text;

  /// True when an entry passes the current severity, category and text
  /// filters.
  [[nodiscard]] bool matches(const spdlog::level::level_enum level,
      const LogCategory category,
      const std::string_view line) const {
    if (level < min_level) {
      return false;
    }
    const std::uint32_t bit{1U << static_cast<std::uint32_t>(category)};
    if ((category_mask & bit) == 0U) {
      return false;
    }
    return contains_ignore_case(line, text);
  }

 private:
  [[nodiscard]] static bool contains_ignore_case(
      const std::string_view haystack, const std::string_view needle) {
    if (needle.empty()) {
      return true;
    }
    if (needle.size() > haystack.size()) {
      return false;
    }
    for (std::size_t start{0}; start + needle.size() <= haystack.size(); ++start) {
      bool equal{true};
      for (std::size_t i{0}; i < needle.size(); ++i) {
        if (ascii_lower(haystack[start + i]) != ascii_lower(needle[i])) {
          equal = false;
          break;
        }
      }
      if (equal) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] static char ascii_lower(const char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }
};

}  // namespace App::Debug

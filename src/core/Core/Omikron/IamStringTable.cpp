#include "Core/Omikron/IamStringTable.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"

namespace App::Omikron {

std::expected<IamStringTable, std::string> IamStringTable::load(
    const std::span<const std::byte> data) {
  APP_PROFILE_FUNCTION();

  std::vector<std::byte> storage(data.begin(), data.end());
  std::vector<std::string_view> strings;

  std::size_t entry_start{0};
  const auto push_entry = [&storage, &strings](const std::size_t begin, const std::size_t length) {
    // The byte storage holds the entry text; viewing it as char is the
    // documented reinterpretation needed to construct the string_view.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* first{reinterpret_cast<const char*>(storage.data() + begin)};
    strings.emplace_back(first, length);
  };

  for (std::size_t index{0}; index < storage.size(); ++index) {
    if (storage.at(index) == std::byte{0x00}) {
      push_entry(entry_start, index - entry_start);
      entry_start = index + 1U;
    }
  }
  // A final non-NUL range is an unterminated trailing entry; a final NUL
  // leaves entry_start == size, which must not emit a trailing empty entry.
  if (entry_start < storage.size()) {
    push_entry(entry_start, storage.size() - entry_start);
  }

  return IamStringTable{std::move(storage), std::move(strings)};
}

std::string_view IamStringTable::at(const std::size_t index) const {
  if (const std::optional<std::string_view> value{try_at(index)}) {
    return *value;
  }
  App::Log::warn("IamStringTable: index {} out of range ({} entries)", index, m_strings.size());
  return {};
}

std::optional<std::string_view> IamStringTable::try_at(const std::size_t index) const {
  if (index >= m_strings.size()) {
    return std::nullopt;
  }
  return m_strings.at(index);
}

}  // namespace App::Omikron

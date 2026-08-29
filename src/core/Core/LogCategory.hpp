#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace App {

/// Engine subsystems that may be filtered independently in the log.
///
/// A category represents an engine subsystem a developer may reasonably want
/// to filter independently; it is deliberately not a per-class or per-file
/// label. The enumeration is kept small on purpose.
enum class LogCategory : std::uint8_t {
  Core,
  Startup,
  Video,
  Input,
  Resource,
  Scenario,
  SCX,
  Script,
  Audio,
  Music,
  Interface,
  I2D,
  Renderer,
  Debug,
};

/// Number of distinct log categories.
inline constexpr std::size_t k_log_category_count{14};

/// Every log category in declaration order (used by the log viewer, the
/// sink's reverse lookup and unit tests).
inline constexpr std::array<LogCategory, k_log_category_count> k_all_log_categories{
    LogCategory::Core,
    LogCategory::Startup,
    LogCategory::Video,
    LogCategory::Input,
    LogCategory::Resource,
    LogCategory::Scenario,
    LogCategory::SCX,
    LogCategory::Script,
    LogCategory::Audio,
    LogCategory::Music,
    LogCategory::Interface,
    LogCategory::I2D,
    LogCategory::Renderer,
    LogCategory::Debug,
};

/// Stable display name of a category. This is also the spdlog logger name,
/// so it is what `%n` renders and what the ring-buffer sink stores.
[[nodiscard]] constexpr std::string_view log_category_name(const LogCategory category) noexcept {
  switch (category) {
    case LogCategory::Core:
      return "Core";
    case LogCategory::Startup:
      return "Startup";
    case LogCategory::Video:
      return "Video";
    case LogCategory::Input:
      return "Input";
    case LogCategory::Resource:
      return "Resource";
    case LogCategory::Scenario:
      return "Scenario";
    case LogCategory::SCX:
      return "SCX";
    case LogCategory::Script:
      return "Script";
    case LogCategory::Audio:
      return "Audio";
    case LogCategory::Music:
      return "Music";
    case LogCategory::Interface:
      return "Interface";
    case LogCategory::I2D:
      return "I2D";
    case LogCategory::Renderer:
      return "Renderer";
    case LogCategory::Debug:
      return "Debug";
  }
  return "Core";
}

/// Resolve a logger name back to its category. Unknown names (for example a
/// third-party logger sharing the sinks) resolve to Core.
[[nodiscard]] constexpr LogCategory log_category_from_name(const std::string_view name) noexcept {
  for (const LogCategory category : k_all_log_categories) {
    if (log_category_name(category) == name) {
      return category;
    }
  }
  return LogCategory::Core;
}

}  // namespace App

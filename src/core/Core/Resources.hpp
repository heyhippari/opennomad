#pragma once

#include <SDL3/SDL_filesystem.h>

#include <cstddef>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "Settings/Project.hpp"

namespace App {

class Resources {
 public:
  [[nodiscard]] static std::filesystem::path get_user_config_path() {
    return SDL_GetPrefPath(COMPANY_NAMESPACE.c_str(), APP_NAME.c_str());
  }

  [[nodiscard]] static std::filesystem::path resource_path(const std::filesystem::path& file_path);
  [[nodiscard]] static std::filesystem::path font_path(const std::string_view& font_file);

  /// Resolves a game data file (e.g. MESHES/PERSOS/KAI_FN.3DO) relative to the
  /// executable, mirroring the original game's file tree.
  [[nodiscard]] static std::filesystem::path game_data_path(const std::filesystem::path& file_path);

  /// Finds the on-disk spelling of `file_path`, matching each path component
  /// case-insensitively when the verbatim path does not exist. On
  /// case-sensitive filesystems this mirrors Windows' case-insensitive
  /// lookups, so files whose stored case differs from the requested case
  /// (e.g. `akg_fnm.3dt` requested as `AKG_FNM.3dt`) still load. Returns
  /// `file_path` unchanged when it exists verbatim or when no
  /// case-insensitive match exists.
  [[nodiscard]] static std::filesystem::path resolve_case_insensitive(
      const std::filesystem::path& file_path) {
    namespace fs = std::filesystem;

    std::error_code error;
    if (fs::exists(file_path, error)) {
      return file_path;
    }
    if (error) {
      return file_path;  // e.g. permission denied: keep the original path.
    }

    const fs::path absolute{file_path.is_absolute() ? file_path : fs::absolute(file_path, error)};
    if (error) {
      return file_path;
    }

    // Walk up to the deepest ancestor that exists; the components below it
    // have to be matched case-insensitively against the directory entries.
    fs::path existing{absolute.lexically_normal()};
    std::vector<fs::path> missing;
    while (!existing.empty()) {
      if (fs::exists(existing, error)) {
        break;
      }
      const fs::path parent{existing.parent_path()};
      if (parent == existing) {
        break;  // Reached the root without a match.
      }
      missing.push_back(existing.filename());
      existing = parent;
    }
    if (error) {
      return file_path;
    }

    for (const fs::path& wanted : missing | std::views::reverse) {
      const fs::path candidate{existing / wanted};
      if (fs::exists(candidate, error)) {
        existing = candidate;
        continue;
      }
      const fs::directory_iterator directory{existing, error};
      if (error) {
        return file_path;
      }
      bool found{false};
      for (const fs::directory_entry& entry : directory) {
        if (ascii_case_insensitive_equal(entry.path().filename().string(), wanted.string())) {
          existing = entry.path();
          found = true;
          break;
        }
      }
      if (!found) {
        return file_path;
      }
    }
    return existing;
  }

 private:
  [[nodiscard]] static bool ascii_case_insensitive_equal(
      const std::string_view left, const std::string_view right) {
    if (left.size() != right.size()) {
      return false;
    }
    for (std::size_t index{0}; index < left.size(); ++index) {
      if (to_ascii_lower(left.at(index)) != to_ascii_lower(right.at(index))) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] static char to_ascii_lower(const char value) {
    if ((value >= 'A') && (value <= 'Z')) {
      return static_cast<char>(value + ('a' - 'A'));
    }
    return value;
  }
};

}  // namespace App

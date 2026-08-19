#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace App {

/// One game-data file loaded from disk through the case-insensitive resolver.
/// `requested` preserves the caller's path (with its original casing and
/// separators), `resolved` is the on-disk spelling that actually matched, and
/// `bytes` owns the file contents.
struct LoadedGameFile {
  std::filesystem::path requested;
  std::filesystem::path resolved;
  std::vector<std::byte> bytes;
};

/// Loads a whole game-data file through Resources::game_data_path() and
/// Resources::resolve_case_insensitive(), mirroring Windows' case-insensitive
/// lookups on case-sensitive filesystems. The error retains both the
/// requested and the resolved path where useful.
[[nodiscard]] std::expected<LoadedGameFile, std::string> load_game_file(
    const std::filesystem::path& relative);

}  // namespace App

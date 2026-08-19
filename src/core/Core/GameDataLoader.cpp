#include "Core/GameDataLoader.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <fmt/format.h>

#include <cstddef>
#include <cstring>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Resources.hpp"

namespace App {

std::expected<LoadedGameFile, std::string> load_game_file(
    const std::filesystem::path& relative) {
  APP_PROFILE_FUNCTION();

  const std::filesystem::path root_relative{Resources::game_data_path(relative)};
  const std::filesystem::path resolved{Resources::resolve_case_insensitive(root_relative)};

  App::Log::trace(LogCategory::Resource,
      "Game-data file resolution: requested='{}' resolved='{}'",
      relative.string(),
      resolved.string());

  std::size_t size{0};
  void* raw{SDL_LoadFile(resolved.string().c_str(), &size)};
  if (raw == nullptr) {
    return std::expected<LoadedGameFile, std::string>{std::unexpect,
        fmt::format("cannot read '{}' (resolved '{}'): {}",
            relative.string(),
            resolved.string(),
            SDL_GetError())};
  }

  std::vector<std::byte> bytes(size);
  if (size > 0) {
    std::memcpy(bytes.data(), raw, size);
  }
  SDL_free(raw);

  return LoadedGameFile{.requested = relative,
      .resolved = resolved,
      .bytes = std::move(bytes)};
}

}  // namespace App

#include "Core/Resources.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "Core/Debug/Instrumentor.hpp"

namespace App {

static const std::string BASE_PATH{SDL_GetBasePath()};

std::filesystem::path Resources::resource_path(const std::filesystem::path& file_path) {
  APP_PROFILE_FUNCTION();

  std::filesystem::path font_path{BASE_PATH};
  font_path /= file_path;
  return font_path;
}

std::filesystem::path Resources::font_path(const std::string_view& font_file) {
  APP_PROFILE_FUNCTION();

  return resource_path(font_file);
}

std::filesystem::path Resources::game_data_path(const std::filesystem::path& file_path) {
  APP_PROFILE_FUNCTION();

  // OPENNOMAD_GAME_DATA_ROOT overrides the game-data root, so an external
  // copy (e.g. a SteamLibrary install) can be used during development without
  // copying the data next to the executable after every clean rebuild.
  if (const char* root{SDL_getenv("OPENNOMAD_GAME_DATA_ROOT")}; root != nullptr) {
    return std::filesystem::path{root} / file_path;
  }

  // Game data sits next to the executable, in the original game's layout.
  std::filesystem::path data_path{BASE_PATH};
  data_path /= file_path;
  return data_path;
}

}  // namespace App

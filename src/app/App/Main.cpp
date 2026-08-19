#define SDL_MAIN_HANDLED

#include <cstdlib>
#include <exception>

#include "Core/Application.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"

int main() {
  try {
    APP_PROFILE_BEGIN_SESSION_WITH_FILE("App", "profile.json");

    int exit_code{EXIT_SUCCESS};
    {
      APP_PROFILE_SCOPE("Test scope");
      auto app{App::Application::create("App")};
      if (!app) {
        App::Log::error("Failed to start: {}", app.error());
        exit_code = EXIT_FAILURE;
      } else {
        app->run();
      }
    }

    APP_PROFILE_END_SESSION();
    return exit_code;
  } catch (std::exception& e) {
    App::Log::error("Main process terminated with: {}", e.what());
  }

  return EXIT_FAILURE;
}

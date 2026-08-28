#include "Core/DPIHandler.hpp"

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Window.hpp"

namespace App {

Window::Settings DPIHandler::get_dpi_aware_window_size(const Window::Settings& settings) {
  APP_PROFILE_FUNCTION();

  return {.title = settings.title,
      .width = settings.width,
      .height = settings.height,
      .display_mode = settings.display_mode,
      .resolution = settings.resolution,
      .hidden = settings.hidden};
}

}  // namespace App

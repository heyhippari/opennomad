#pragma once

namespace App {

/// Cached window dimensions: logical size (window coordinates) and drawable
/// pixel size (backbuffer coordinates). High-DPI windows keep the two
/// distinct; the renderer receives the pixel size while mouse coordinates
/// stay in the logical space.
///
/// Pure logic, unit-testable: the Window feeds it from SDL resize events.
struct WindowSizeState {
  int width{1};
  int height{1};
  int pixel_width{1};
  int pixel_height{1};

  /// SDL_EVENT_WINDOW_RESIZED: the logical size changed.
  void on_resized(const int new_width, const int new_height) {
    width = new_width;
    height = new_height;
  }

  /// SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: the drawable size changed.
  void on_pixel_size_changed(const int new_pixel_width, const int new_pixel_height) {
    pixel_width = new_pixel_width;
    pixel_height = new_pixel_height;
  }
};

}  // namespace App

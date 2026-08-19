#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Core/Interface/FontManager.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Interface/InterfaceDescriptor.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Omikron/IamStringTable.hpp"
#include "Core/Texture.hpp"

namespace App::Input {
class InputManager;
}

namespace App::Interface {

class I2DRenderer;

/// Runtime instance created from an InterfaceDescriptor by the generic
/// opener. Owns the descriptor's loaded resources and the I2D state graph.
///
/// Ownership order mirrors Runtime's Interface_Open:
///   descriptor -> resources (bitmap, string table) -> I2D state graph.
struct InterfaceInstance {
  const InterfaceDescriptor* descriptor{nullptr};
  /// Identity of this instance (interface id + generation), assigned by the
  /// generic opener and used to validate deferred completions.
  InterfaceHandle handle;
  /// The full open request that produced this instance (operands preserved).
  InterfaceOpenRequest open_request;
  Omikron::IamStringTable strings;
  /// The interface-level bitmap loaded from I2D/bitmaps/<bitmap_name>; null
  /// for interfaces without one.
  std::optional<Texture2D> bitmap;
  /// Animated background (interface 29: IMAGES/CLOUD.BMP), owned here and
  /// referenced by the root state.
  std::unique_ptr<I2DBumpBackground> background;
  /// Owns every I2DState; element child-state links are non-owning pointers
  /// into this vector.
  std::vector<std::unique_ptr<I2DState>> states;
  I2DState* root_state{nullptr};
  I2DState* current_state{nullptr};
  /// Index into the current state's selectable text elements (iteration
  /// order).
  std::size_t selected_element{0};
};

/// Generic interface system: looks up a static descriptor, creates a runtime
/// instance, loads its resources and delegates interface-specific setup to
/// the descriptor's init callback. Owns the font registry, renderer and the
/// active instance.
class InterfaceManager {
 public:
  InterfaceManager();
  ~InterfaceManager();

  InterfaceManager(const InterfaceManager&) = delete;
  InterfaceManager(InterfaceManager&&) = delete;
  InterfaceManager& operator=(const InterfaceManager&) = delete;
  InterfaceManager& operator=(InterfaceManager&&) = delete;

  /// Generic Interface_Open equivalent: locate descriptor -> create instance
  /// -> load bitmap -> load string table -> descriptor-specific init ->
  /// establish root/current state. Requires a current GL context. Returns the
  /// opened instance handle.
  [[nodiscard]] std::expected<InterfaceHandle, std::string> open(
      const InterfaceOpenRequest& request);

  /// Generic close: descriptor destroy callback, then RAII resource release.
  void close();

  [[nodiscard]] bool is_open() const {
    return m_instance.has_value();
  }

  /// Queues a deferred completion request for the active instance (a New Game
  /// child-state action). The completion is drained later by the application
  /// so the active instance is not invalidated during element iteration.
  void request_completion(std::int16_t result);

  /// Returns and clears the deferred completion, if one was requested.
  [[nodiscard]] std::optional<InterfaceCompletion> take_completion();

  /// The handle of the active instance, or nullopt when closed.
  [[nodiscard]] std::optional<InterfaceHandle> active_handle() const {
    return m_instance.has_value() ? std::optional<InterfaceHandle>{m_instance->handle}
                                  : std::nullopt;
  }

  /// Per-frame update: advances the background animation and handles menu
  /// navigation from the resolved action edges.
  void update(float delta_time, const Input::InputManager& input);

  /// Renders the active state into the given drawable framebuffer size.
  void render(int pixel_width, int pixel_height);

  /// Creates a state owned by the active instance; returns its address
  /// (nullptr when no interface is open).
  [[nodiscard]] I2DState* create_state();

  /// Loads the font for `key` through the font registry.
  [[nodiscard]] std::expected<void, std::string> load_font(char key);

  /// The active instance (debug inspector); nullptr when closed.
  [[nodiscard]] const InterfaceInstance* instance() const {
    return m_instance.has_value() ? &*m_instance : nullptr;
  }
  [[nodiscard]] const FontManager& fonts() const {
    return m_fonts;
  }

  // --- Generic navigation (root menu) ---
  void select_previous();
  void select_next();
  void confirm();
  void cancel();

 private:
  void handle_navigation(const Input::InputManager& input);

  std::unique_ptr<I2DRenderer> m_renderer;
  FontManager m_fonts;
  std::optional<InterfaceInstance> m_instance;
  /// Generation counter for the next opened instance (used in handles).
  std::uint32_t m_generation{0};
  /// Deferred completion request for the active instance, drained by the
  /// application outside element iteration.
  std::optional<InterfaceCompletion> m_pending_completion;
};

/// Looks up a descriptor in the static interface registry.
[[nodiscard]] const InterfaceDescriptor* descriptor_for_id(std::int32_t id);

}  // namespace App::Interface

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/Interface/FontManager.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Interface/InterfaceDescriptor.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Interface/InterfacePresentation.hpp"
#include "Core/Omikron/IamStringTable.hpp"
#include "Core/Texture.hpp"
#include "Settings/GameSettings.hpp"

namespace App::Input {
class InputManager;
}

namespace App::Interface {

class I2DRenderer;

enum class InterfacePresentationPhase : std::uint8_t {
  k_idle,
  k_enter_fade,
  k_completion,
  k_completion_queued,
};

/// Runtime-only state for descriptor presentation hints. This state never
/// changes the recovered I2D state graph or the result delivered to AREA.
struct InterfacePresentationState {
  InterfacePresentationPhase phase{InterfacePresentationPhase::k_idle};
  float elapsed_seconds{0.0F};
  InterfacePresentationOverlay overlay;
  const InterfaceCompletionPresentationHint* completion_hint{nullptr};
  std::optional<InterfaceCompletion> pending_completion;
};

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
  /// Optional resident interface that visually/semantically hosts this one.
  /// Runtime keeps START MENU resident while OPTIONS is active; this explicit
  /// relationship lets the child close back to the host without pretending
  /// that states from two different interface instances share ownership.
  std::optional<InterfaceHandle> parent_interface;
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

  /// OpenNomad presentation policy state, separate from recovered I2D state.
  InterfacePresentationState presentation;
};

/// Generic interface system: looks up a static descriptor, creates a runtime
/// instance, loads its resources and delegates interface-specific setup to
/// the descriptor's init callback. Owns the font registry, renderer and every
/// resident instance.
///
/// Residency and focus are tracked separately: many interfaces may be
/// resident at once (Runtime already opens interface 35 while interface 29
/// stays alive), while only one instance receives navigation input.
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
  /// opened instance handle. Opening an interface never implicitly destroys
  /// another resident instance; focus moves to the newly opened instance.
  [[nodiscard]] std::expected<InterfaceHandle, std::string> open(
      const InterfaceOpenRequest& request);

  /// Generic close for one instance by handle: descriptor destroy callback,
  /// then RAII resource release. A stale handle is a harmless no-op. When the
  /// focused instance closes, focus moves to the most recently opened
  /// remaining instance (or clears when none remain).
  void close(InterfaceHandle handle);

  /// Closes every resident interface (full teardown, e.g. destruction).
  void close();

  [[nodiscard]] bool contains(InterfaceHandle handle) const;

  /// Finds a resident instance by handle (or nullptr for a stale handle).
  [[nodiscard]] InterfaceInstance* find(InterfaceHandle handle);
  [[nodiscard]] const InterfaceInstance* find(InterfaceHandle handle) const;

  /// The focused (input-receiving) instance handle, or nullopt when closed.
  [[nodiscard]] std::optional<InterfaceHandle> focused_handle() const {
    return m_focused_interface;
  }

  /// Makes an instance the focused (input-receiving) instance. Stale handles
  /// are ignored.
  void set_focused(InterfaceHandle handle);

  /// Number of resident interfaces.
  [[nodiscard]] std::size_t instance_count() const {
    return m_instances.size();
  }

  /// The resident interface at `index` in opening (presentation) order.
  [[nodiscard]] const InterfaceInstance* instance_at(std::size_t index) const;

  /// The focused instance, or nullptr when none.
  [[nodiscard]] const InterfaceInstance* focused_instance() const;

  /// Queues a deferred completion request for `handle` (a New Game child-state
  /// action). The completion is drained later by the application so the
  /// instance is not invalidated during element iteration.
  void request_completion(InterfaceHandle handle, std::int16_t result);

  /// Returns and removes the oldest deferred completion, if any.
  [[nodiscard]] std::optional<InterfaceCompletion> take_completion();

  /// Top-most full-screen overlay requested by interface presentation hints.
  /// A completion that became opaque this frame is latched here even if the
  /// application has already closed the completed interface before render.
  [[nodiscard]] std::optional<InterfacePresentationOverlay> presentation_overlay() const;

  /// Per-frame update: advances every resident instance's background animation
  /// and handles menu navigation on the focused instance.
  void update(float delta_time, const Input::InputManager& input);

  /// Advances resident presentation without routing navigation input. Used
  /// while the gameplay dialog layer owns the shared confirm/navigation actions.
  void update_without_input(float delta_time);

  /// Renders every resident instance in opening (presentation) order into the
  /// given drawable framebuffer size.
  void render(int pixel_width, int pixel_height);

  /// Renders one active gameplay dialog above resident interfaces.
  [[nodiscard]] float render_dialog(const Dialog::DialogPresentation& dialog,
      std::size_t selected_choice,
      float scroll_offset,
      int pixel_width,
      int pixel_height);

  /// Renders neutral world subtitle presentation using the shared I2D font
  /// resources, without making it an interface or dialog runtime state.
  void render_world_subtitle(std::string_view text, int pixel_width, int pixel_height);

  /// Creates a state owned by `instance`; returns its address (nullptr when
  /// the allocation fails).
  [[nodiscard]] I2DState* create_state(InterfaceInstance& instance);

  /// Loads the font for `key` through the font registry.
  [[nodiscard]] std::expected<void, std::string> load_font(char key);

  [[nodiscard]] const FontManager& fonts() const {
    return m_fonts;
  }

  /// Runtime-independent settings backend used by OPTIONS rows and, later, by
  /// gameplay/rendering consumers and persistence.
  [[nodiscard]] App::Settings::GameSettings& game_settings() {
    return m_game_settings;
  }
  [[nodiscard]] const App::Settings::GameSettings& game_settings() const {
    return m_game_settings;
  }

  /// Sets the animated background's presentation mode (stepped or
  /// interpolated) for every resident instance. Debug/inspection helper.
  void set_background_interpolated(bool interpolated);

  /// Reads the background presentation mode from the first resident instance
  /// (interpolated when none). Debug/inspection helper.
  [[nodiscard]] bool background_interpolated() const;

  // --- Generic navigation (focused interface) ---
  void select_previous();
  void select_next();
  void adjust_selected(std::int32_t delta);
  void confirm();
  void cancel();

 private:
  /// Mutable focused instance, or nullptr when none.
  [[nodiscard]] InterfaceInstance* focused_instance_mut();

  /// Destroys one resident instance (descriptor destroy + RAII release).
  void destroy_instance(InterfaceInstance& instance);

  /// Queues the real completion after any presentation-only deferral.
  void queue_completion(const InterfaceCompletion& completion);
  /// Advances one instance's presentation-only lifecycle.
  void update_presentation(InterfaceInstance& instance, float delta_time);

  void handle_navigation(const Input::InputManager& input);

  std::unique_ptr<I2DRenderer> m_renderer;
  FontManager m_fonts;
  App::Settings::GameSettings m_game_settings;
  /// Resident interfaces in opening order (presentation order).
  std::vector<std::unique_ptr<InterfaceInstance>> m_instances;
  /// Focused (input-receiving) instance; nullopt when none resident.
  std::optional<InterfaceHandle> m_focused_interface;
  /// Generation counter for the next opened instance (used in handles).
  std::uint32_t m_generation{0};
  /// Deferred completion requests, drained one at a time by the application.
  std::deque<InterfaceCompletion> m_completions;
  /// Keeps the final opaque completion colour alive until this frame renders,
  /// even though Application closes completed interfaces before render().
  std::optional<InterfacePresentationOverlay> m_completion_overlay_latch;
};

/// Looks up a descriptor in the static interface registry.
[[nodiscard]] const InterfaceDescriptor* descriptor_for_id(std::int32_t id);

}  // namespace App::Interface

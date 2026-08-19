#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace App::Interface {

/// Axis-aligned rectangle in the 640x480 virtual interface canvas.
/// Recovered Runtime coordinates use this coordinate system, not window
/// pixels.
struct I2DRect {
  int x{0};
  int y{0};
  int width{0};
  int height{0};
};

struct I2DState;
class I2DBumpBackground;
class InterfaceManager;
struct InterfaceInstance;

/// Generic enter action for a child state. Invoked by the generic confirm()
/// path when the selected element's target state has one. The callback must
/// perform the state-specific enter behavior (e.g. queue an interface
/// completion) without inspecting the label text or string-table index.
using I2DStateEnterCallback =
    std::function<void(InterfaceManager&, InterfaceInstance&, I2DState&)>;

/// The two generic element kinds implemented by this milestone. More element
/// kinds (animations, hotspots, ...) are expected from later RE work.
enum class I2DElementKind : std::uint8_t { k_bitmap, k_text };

/// Generic bitmap blit element: a source rectangle of the interface bitmap
/// drawn into a destination rectangle of the virtual canvas.
///
/// runtime_flags preserves the recovered Runtime value (0x40000100 for the
/// gfxint top artwork); its exact bit semantics are not yet established.
struct I2DBitmapElement {
  I2DRect source;
  I2DRect destination;
  std::uint32_t runtime_flags{0};

  /// Separate recovered Runtime blit-mode field (0x03 for the main-menu
  /// artwork), kept distinct from runtime_flags.
  ///
  /// bit 0 (0x01): DDBLT_KEYSRC  — confirmed source colour key (the source
  ///               surface key is pixel value 0).
  /// bit 1 (0x02): DDBLT_KEYDEST — confirmed destination colour key (the
  ///               destination DESTBLT key has not yet been recovered).
  std::uint8_t runtime_blit_mode{0};
};

/// Per-draw source colour-key options for the generic I2D bitmap path.
///
/// Ordinary alpha blending and DirectDraw source colour-keying are distinct
/// concepts; the I2D data model preserves that the recovered operation was a
/// source colour key rather than pretending the bitmap contained alpha.
struct I2DBlitOptions {
  /// When set, source pixels whose recovered 16-bit RGB555 value matches the
  /// key are discarded (see resolve_bitmap_blit_options). nullopt = ordinary
  /// opaque draw.
  std::optional<std::array<float, 3>> source_colour_key{std::nullopt};
};

/// True when the recovered Runtime blit mode requests a source colour key
/// (DDBLT_KEYSRC). Runtime's low-level blit path (~0x004810D0) maps bit 0.
[[nodiscard]] inline bool uses_source_color_key(const std::uint8_t mode) {
  return (mode & 0x01U) != 0U;
}

/// True when the recovered Runtime blit mode requests a destination colour
/// key (DDBLT_KEYDEST). Runtime's low-level blit path (~0x004810D0) maps
/// bit 1. The destination surface's DDCKEY_DESTBLT value has not yet been
/// located, so no destination-key value is modelled here.
[[nodiscard]] inline bool uses_destination_color_key(const std::uint8_t mode) {
  return (mode & 0x02U) != 0U;
}

/// Resolves a recovered Runtime blit mode to draw options.
///
/// Bit 0 enables a source colour key (DDBLT_KEYSRC). Runtime establishes the
/// key as pixel value 0 in a 16-bit RGB555 surface, which corresponds to
/// black: the near-black bitmap background (e.g. palette RGB 4,4,4) truncates
/// to 0 and keys out. Bit 1 (DDBLT_KEYDEST) is recognised but deferred: the
/// destination key value is still unknown.
inline I2DBlitOptions resolve_bitmap_blit_options(const I2DBitmapElement& element) {
  if (uses_source_color_key(element.runtime_blit_mode)) {
    return I2DBlitOptions{.source_colour_key = std::array<float, 3>{0.0F, 0.0F, 0.0F}};
  }
  return I2DBlitOptions{};
}

/// Generic text element. References a string-table index rather than owning
/// its label, and resolves its font through a font key (see FontManager).
///
/// target_state links the element to its child interface state; a non-null
/// target marks the element as selectable.
struct I2DTextElement {
  std::uint16_t string_index{0};
  char font_key{'I'};

  I2DRect bounds;

  std::uint8_t red{255};
  std::uint8_t green{255};
  std::uint8_t blue{255};

  std::uint32_t runtime_flags{0};

  I2DState* target_state{nullptr};

  [[nodiscard]] bool selectable() const {
    return target_state != nullptr;
  }
};

using I2DElementData = std::variant<I2DBitmapElement, I2DTextElement>;

/// How an element (or the animated background) relates to the physical
/// viewport under the modern presentation transform. Recovered Runtime
/// coordinates always remain the authoritative reference space; this policy
/// only describes how OpenNomad presents them on modern displays.
enum class I2DScalePolicy : std::uint8_t {
  /// Ordinary imagery: keep the recovered 640x480 reference rectangle,
  /// height-fitted to the screen and centred horizontally (the generic
  /// default — it falls out of the full-viewport projection automatically).
  k_reference_canvas,
  /// Procedural/full-screen backgrounds: fill the entire physical viewport
  /// and evaluate at native resolution.
  k_full_viewport,
};

/// Presentation adjustments attached to an element by its interface-specific
/// initializer. Kept strictly separate from recovered Runtime data
/// (I2DRect / runtime_flags / runtime_blit_mode) so both the authored values
/// and the modernization choices remain inspectable.
struct I2DPresentationHints {
  I2DScalePolicy scale_policy{I2DScalePolicy::k_reference_canvas};

  /// Anchor the element to the horizontal centre of the viewport at the top
  /// (used for the main-menu logo). Default is the reference-canvas layout.
  bool anchor_top_center{false};

  /// Extra vertical breathing space above a top-anchored element, in
  /// reference units (OpenNomad modernization adjustment, not a Runtime
  /// value).
  float top_margin_reference{0.0F};

  /// Clamp a top-anchored element's own scale so it stays fully visible on
  /// displays narrower than 4:3. No effect on ordinary landscape displays.
  bool clamp_width_to_viewport{false};
};

/// One element of a group.
struct I2DElement {
  I2DElementData data;
  I2DPresentationHints presentation;
};

/// A group of elements sharing one recovered raw flag value (Runtime groups
/// carry flags such as 0x80000010 for the menu text group).
struct I2DGroup {
  std::vector<I2DElement> elements;
  std::uint32_t runtime_flags{0};
};

/// One interface state. States are distinct from groups and from elements;
/// child-state links are stored on the elements themselves.
///
/// background is a non-owning pointer into the owning InterfaceInstance's
/// background object (nullptr for states without one).
///
/// on_enter is an optional generic enter action invoked by the generic
/// confirm() path; states without one simply become the current state.
struct I2DState {
  I2DState* parent{nullptr};
  std::vector<I2DGroup> groups;
  I2DBumpBackground* background{nullptr};
  I2DStateEnterCallback on_enter;
};

/// Collects the selectable text elements of `state` in iteration order
/// (groups then elements). Selection indices used by navigation and rendering
/// refer to this order.
inline std::vector<I2DTextElement*> selectable_text_elements(I2DState& state) {
  std::vector<I2DTextElement*> result;
  for (I2DGroup& group : state.groups) {
    for (I2DElement& element : group.elements) {
      auto* text{std::get_if<I2DTextElement>(&element.data)};
      if (text != nullptr && text->selectable()) {
        result.push_back(text);
      }
    }
  }
  return result;
}

}  // namespace App::Interface

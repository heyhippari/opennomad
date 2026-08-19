#pragma once

#include <cstdint>
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
};

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

/// One element of a group.
struct I2DElement {
  I2DElementData data;
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
struct I2DState {
  I2DState* parent{nullptr};
  std::vector<I2DGroup> groups;
  I2DBumpBackground* background{nullptr};
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

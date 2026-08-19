#pragma once

#include <array>
#include <cstdint>

#include "Core/Interface/I2DModel.hpp"

namespace App::Interface {

/// One recovered root-menu text entry: the IAM string index, the font key and
/// the 640x480 virtual-canvas bounds. Recovered from Runtime's static I2D
/// structures (text elements @ 0x004CE6F0 / 0x004CE738 / 0x004CE780 /
/// 0x004CE7C8); IAM/Menu holds only the strings, not these coordinates.
struct RecoveredTextEntry {
  std::uint16_t string_index;
  char font_key;
  int x;
  int y;
  int width;
  int height;
};

/// The four recovered root-menu entries, in selection order.
inline constexpr std::array<RecoveredTextEntry, 4> k_start_menu_root_entries{{
    {0, 'I', 0, 120, 640, 40},
    {1, 'I', 0, 200, 640, 40},
    {4, 'I', 0, 280, 640, 40},
    {5, 'I', 0, 360, 640, 40},
}};

/// Recovered raw flag for the menu text group (0x80000010). The exact
/// symbolic meaning of every bit is not yet established.
inline constexpr std::uint32_t k_start_menu_text_group_flags{0x80000010};

/// Recovered raw flag for the gfxint top-artwork bitmap element (0x40000100).
inline constexpr std::uint32_t k_start_menu_bitmap_flags{0x40000100};

/// Recovered blit mode for the gfxint top-artwork bitmap element (0x03).
///
/// bit 0 (0x01): DDBLT_KEYSRC  — confirmed source colour key.
/// bit 1 (0x02): DDBLT_KEYDEST — confirmed destination colour key (the
///               destination DESTBLT key has not yet been recovered).
inline constexpr std::uint8_t k_start_menu_bitmap_blit_mode{0x03};

/// Recovered source/destination rectangle of the gfxint top artwork.
inline constexpr I2DRect k_start_menu_bitmap_rect{0, 0, 640, 150};

/// OpenNomad modernization adjustment: extra vertical breathing space above
/// the main-menu logo, in reference units. Applied on top of the recovered
/// Runtime rectangle (which stays y = 0); the on-screen margin scales with
/// the presentation transform (margin_on_screen = value * screen_height/480).
inline constexpr float k_start_menu_logo_top_margin{12.0F};

/// Presentation-only scale applied to the recovered 640x150 logo rectangle.
/// Keeping this separate from k_start_menu_bitmap_rect preserves the Runtime
/// geometry for reverse-engineering/reference purposes.
inline constexpr float k_start_menu_logo_scale{0.84F};

/// OpenNomad's compact presentation of the recovered root-menu entries.
///
/// These are intentionally separate from k_start_menu_root_entries: the
/// latter remains the authoritative Runtime layout, while these values only
/// control the modern on-screen presentation.
inline constexpr std::array<int, 4> k_start_menu_modern_y{{
    168,
    218,
    268,
    318,
}};

inline constexpr int k_start_menu_modern_text_height{36};

}  // namespace App::Interface

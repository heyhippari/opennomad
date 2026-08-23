#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "Core/Omikron/FontFNT.hpp"
#include "Core/Texture.hpp"

struct ImFont;
struct ImFontAtlas;
struct ImFontBaked;

namespace App::Interface {

/// A rasterized font ready for glyph-quad rendering. Retail .FNT resources
/// retain byte-indexed Runtime metrics; TTF/OTF is a warned fallback only.
///
/// Glyph metrics are exposed in the recovered 640x480 reference units so
/// interface layout is resolution-independent: the atlas may be rasterized at
/// a physical size that follows the presentation scale, but every metric is
/// normalized back to reference units.
class FontResource {
 public:
  /// Metrics for one glyph. Visible glyphs can be emitted as textured quads;
  /// invisible glyphs such as spaces still carry layout-significant advance.
  /// UVs are in the same bottom-up texture convention as every other I2D
  /// texture (v_top >= v_bottom). Corner offsets and advance are in reference
  /// units (y grows down).
  struct Glyph {
    /// False for whitespace and other glyphs which have layout metrics but no
    /// pixels to draw. Their advance_x must still be applied by the renderer.
    bool visible{false};

    float u_left{0.0F};
    float u_right{0.0F};
    float v_top{0.0F};
    float v_bottom{0.0F};

    /// Glyph corners, offsets from the cursor in reference units (y grows down).
    float x0{0.0F};
    float y0{0.0F};
    float x1{0.0F};
    float y1{0.0F};

    float advance_x{0.0F};
  };

  /// Builds a font from a TTF/OTF file at the requested physical pixel size.
  /// `logical_size` is the recovered reference-space font size (30 reference
  /// pixels); metrics are exposed in those units. Requires a current GL
  /// context (the atlas is uploaded as a texture immediately).
  [[nodiscard]] static std::expected<FontResource, std::string> load_ttf_fallback(
      const std::filesystem::path& ttf_path, float size_pixels, float logical_size);

  /// Builds a nearest-filtered white/alpha atlas from retail five-bit glyphs.
  /// Requires a current GL context.
  [[nodiscard]] static std::expected<FontResource, std::string> load_retail_fnt(
      const Omikron::FontFntData& font,
      int letter_spacing,
      int blank_width,
      int line_height);

  FontResource(FontResource&& other) noexcept;
  FontResource& operator=(FontResource&& other) noexcept;
  ~FontResource();

  FontResource(const FontResource&) = delete;
  FontResource& operator=(const FontResource&) = delete;

  /// The glyph for a codepoint, or nullopt when it is unavailable. Invisible
  /// glyphs such as spaces are returned because their advance contributes to
  /// text layout even though no textured quad is emitted. Metrics are in
  /// reference units.
  [[nodiscard]] std::optional<Glyph> glyph_for(char32_t codepoint) const;

  /// Decodes the next glyph according to this resource's encoding. Retail
  /// resources consume exactly one byte; the fallback consumes one UTF-8
  /// sequence. Returns nullopt at end of input or for a missing glyph.
  [[nodiscard]] std::optional<Glyph> next_glyph(
      std::string_view text, std::size_t& byte_offset) const;

  /// Total advance width of `text` in reference units (no wrapping, no
  /// newlines).
  [[nodiscard]] float measure(std::string_view text) const;

  /// Nominal line height in reference units (equals the logical font size).
  [[nodiscard]] float line_height() const;

  [[nodiscard]] bool is_retail_fnt() const {
    return m_is_retail_fnt;
  }

  /// The physical atlas raster size (for diagnostics).
  [[nodiscard]] float raster_size_pixels() const {
    return m_size_pixels;
  }

  [[nodiscard]] const Texture2D& texture() const {
    return *m_texture;
  }

 private:
  FontResource() = default;

  std::unique_ptr<ImFontAtlas> m_atlas;
  ImFont* m_font{nullptr};
  ImFontBaked* m_baked{nullptr};
  std::optional<Texture2D> m_texture;
  /// Physical pixel size the atlas was rasterized at.
  float m_size_pixels{0.0F};
  /// Ratio physical pixel -> reference unit (size_pixels / logical_size).
  float m_reference_scale{1.0F};
  std::array<Glyph, 256> m_retail_glyphs;
  float m_line_height{0.0F};
  bool m_is_retail_fnt{false};
};

/// Recovered Runtime font-registry entry and exact logical metrics.
struct FontRegistryEntry {
  std::string_view logical_name;
  int letter_spacing{0};
  int blank_width{0};
  int line_height{0};
};

/// Resolves interface font keys to logical font names and owns the loaded
/// font resources. The key->name mapping is recovered from Runtime's font
/// registry; 'I' resolves the main-menu font to MENUINTR.
///
/// DIALOGUE.FNT and DIALSELE.FNT use the authoritative retail backend. A
/// missing or corrupt retail file falls back to FONTS/OMIKRON.TTF inside this
/// manager, with a warning; presentation code does not know the fallback.
///
/// The manager caches each key at multiple raster-size buckets so a window
/// resize (which changes the presentation scale) rebuilds the atlas only when
/// the quantized physical size actually changes, not every frame.
class FontManager {
 public:
  FontManager() = default;
  ~FontManager() = default;

  FontManager(const FontManager&) = delete;
  FontManager(FontManager&&) = delete;
  FontManager& operator=(const FontManager&) = delete;
  FontManager& operator=(FontManager&&) = delete;

  /// High-resolution raster basis for the TTF fallback. Exposed layout height
  /// still comes from the selected registry entry.
  static constexpr float k_ttf_fallback_logical_size{30.0F};

  /// Quantizes a desired physical raster size to the nearest 2 px bucket so
  /// resizing a window does not rebuild atlases dozens of times per second.
  [[nodiscard]] static std::size_t raster_bucket(float reference_scale);

  /// Loads and caches the default (reference-scale) font for `key`.
  /// Requires a current GL context.
  [[nodiscard]] std::expected<void, std::string> load_font(char key);

  /// The font for `key` rasterized at a physical size matching
  /// `reference_scale` (presentation pixels per reference unit). Builds and
  /// caches a new atlas bucket when needed. Returns nullptr when the key is
  /// unmapped or loading fails. Requires a current GL context for a miss.
  [[nodiscard]] const FontResource* ensure_font(char key, float reference_scale);

  /// The loaded font for `key` at the default reference scale, or nullptr
  /// when not loaded or failed.
  [[nodiscard]] const FontResource* font_for_key(char key) const;

  /// The recovered logical font name for a key (e.g. 'I' -> "MENUINTR").
  /// Empty for an unmapped key.
  [[nodiscard]] static std::string_view font_logical_name(char key);

  /// Full recovered registry entry for a key, including spacing metrics.
  [[nodiscard]] static std::optional<FontRegistryEntry> font_registry_entry(char key);

 private:
  [[nodiscard]] static std::expected<FontResource, std::string> load_retail_font(
      char key, const FontRegistryEntry& entry);

  std::map<char, std::map<std::size_t, FontResource>> m_fonts;
  std::map<char, FontResource> m_retail_fonts;
};

}  // namespace App::Interface

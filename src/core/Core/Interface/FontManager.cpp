#include "Core/Interface/FontManager.hpp"

// NOLINTBEGIN(misc-include-cleaner)
#include <imgui.h>

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"
#include "Core/Resources.hpp"

namespace App::Interface {

namespace {

/// Path to the temporary fallback TTF (see FontManager docs). Lives only in
/// this backend; no interface or renderer code references it.
constexpr std::string_view K_TTF_FALLBACK_PATH{"FONTS/OMIKRON.TTF"};

/// Pixel size the fallback font is rasterized at (and the glyph metrics are
/// expressed in). Sized to fit the recovered 40 px menu text bands.
constexpr float K_FALLBACK_FONT_SIZE{30.0F};

/// Flips the atlas rows so the texture matches Texture2D's bottom-up upload
/// convention (row 0 = bottom). The atlas is produced top-down by ImGui.
std::vector<std::uint8_t> flip_rows(const unsigned char* pixels,
    const int width,
    const int height) {
  const std::size_t row_bytes{static_cast<std::size_t>(width) * 4U};
  std::vector<std::uint8_t> flipped(static_cast<std::size_t>(width) *
                                    static_cast<std::size_t>(height) * 4U);
  for (int row{0}; row < height; ++row) {
    const std::size_t source_row{static_cast<std::size_t>(height - row - 1)};
    const auto* source{pixels + (source_row * row_bytes)};
    auto* destination{flipped.data() + (static_cast<std::size_t>(row) * row_bytes)};
    for (std::size_t byte{0}; byte < row_bytes; ++byte) {
      destination[byte] = source[byte];
    }
  }
  return flipped;
}

}  // namespace

FontResource::FontResource(FontResource&& other) noexcept
    : m_atlas(std::move(other.m_atlas)),
      m_font(other.m_font),
      m_baked(other.m_baked),
      m_texture(std::move(other.m_texture)),
      m_size_pixels(other.m_size_pixels) {
  other.m_font = nullptr;
  other.m_baked = nullptr;
}

FontResource& FontResource::operator=(FontResource&& other) noexcept {
  if (this != &other) {
    m_atlas = std::move(other.m_atlas);
    m_font = other.m_font;
    m_baked = other.m_baked;
    m_texture = std::move(other.m_texture);
    m_size_pixels = other.m_size_pixels;
    other.m_font = nullptr;
    other.m_baked = nullptr;
  }
  return *this;
}

FontResource::~FontResource() = default;

std::expected<FontResource, std::string> FontResource::load_ttf_fallback(
    const std::filesystem::path& ttf_path, const float size_pixels) {
  APP_PROFILE_FUNCTION();

  auto atlas{std::make_unique<ImFontAtlas>()};
  atlas->Flags |= ImFontAtlasFlags_NoMouseCursors | ImFontAtlasFlags_NoBakedLines;

  ImFontConfig config{};
  config.OversampleH = 1;
  config.OversampleV = 1;
  config.PixelSnapH = true;

  ImFont* font{atlas->AddFontFromFileTTF(
      ttf_path.string().c_str(), size_pixels, &config, atlas->GetGlyphRangesDefault())};
  if (font == nullptr) {
    return std::expected<FontResource, std::string>{std::unexpect,
        fmt::format("FontManager: cannot load TTF '{}'", ttf_path.string())};
  }

  unsigned char* pixels{nullptr};
  int width{0};
  int height{0};
  atlas->GetTexDataAsRGBA32(&pixels, &width, &height);
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return std::expected<FontResource, std::string>{
        std::unexpect, "FontManager: font atlas produced no pixel data"};
  }

  const std::vector<std::uint8_t> flipped{flip_rows(pixels, width, height)};
  auto texture{Texture2D::create(
      width, height, std::span<const std::uint8_t>{flipped}, /*srgb=*/false)};
  if (!texture) {
    return std::expected<FontResource, std::string>{
        std::unexpect, fmt::format("FontManager: {}", texture.error())};
  }

  ImFontBaked* baked{font->GetFontBaked(size_pixels)};
  FontResource resource;
  resource.m_atlas = std::move(atlas);
  resource.m_font = font;
  resource.m_baked = baked;
  resource.m_texture.emplace(std::move(texture).value());
  resource.m_size_pixels = size_pixels;
  return resource;
}

std::optional<FontResource::Glyph> FontResource::glyph_for(const char32_t codepoint) const {
  if (m_baked == nullptr || codepoint > 0xFFFFU) {
    return std::nullopt;
  }
  const ImFontGlyph* glyph{m_baked->FindGlyph(static_cast<ImWchar>(codepoint))};
  if (glyph == nullptr || !glyph->Visible) {
    return std::nullopt;
  }
  // ImGui V0/V1 are expressed in a top-down atlas; the atlas was flipped to
  // bottom-up at upload, so the final V coordinates are their complement.
  return Glyph{.u_left = glyph->U0,
      .u_right = glyph->U1,
      .v_top = 1.0F - glyph->V0,
      .v_bottom = 1.0F - glyph->V1,
      .x0 = glyph->X0,
      .y0 = glyph->Y0,
      .x1 = glyph->X1,
      .y1 = glyph->Y1,
      .advance_x = glyph->AdvanceX};
}

namespace {

/// Minimal UTF-8 decoder: advances `cursor` past one codepoint and returns
/// it (U+FFFD for malformed sequences). Menu text is ASCII today, but the
/// decoder keeps the backend correct for Latin-1 labels too.
unsigned int decode_utf8(const char*& cursor, const char* end) {
  const auto first{static_cast<unsigned char>(*cursor)};
  if (first < 0x80U) {
    ++cursor;
    return first;
  }
  unsigned int codepoint{0};
  int extra{0};
  if ((first & 0xE0U) == 0xC0U) {
    codepoint = first & 0x1FU;
    extra = 1;
  } else if ((first & 0xF0U) == 0xE0U) {
    codepoint = first & 0x0FU;
    extra = 2;
  } else if ((first & 0xF8U) == 0xF0U) {
    codepoint = first & 0x07U;
    extra = 3;
  } else {
    ++cursor;
    return 0xFFFDU;
  }
  for (int index{0}; index < extra; ++index) {
    ++cursor;
    if (cursor >= end) {
      return 0xFFFDU;
    }
    codepoint = (codepoint << 6U) | (static_cast<unsigned char>(*cursor) & 0x3FU);
  }
  ++cursor;
  return codepoint;
}

}  // namespace

float FontResource::measure(const std::string_view text) const {
  if (m_baked == nullptr) {
    return 0.0F;
  }
  float width{0.0F};
  const char* cursor{text.data()};
  const char* end{text.data() + text.size()};
  while (cursor < end) {
    const unsigned int codepoint{decode_utf8(cursor, end)};
    const ImFontGlyph* glyph{m_baked->FindGlyph(static_cast<ImWchar>(codepoint))};
    if (glyph != nullptr) {
      width += glyph->AdvanceX;
    }
  }
  return width;
}

std::expected<void, std::string> FontManager::load_font(const char key) {
  APP_PROFILE_FUNCTION();

  if (m_fonts.contains(key)) {
    return {};
  }

  const std::string_view logical{font_logical_name(key)};
  if (logical.empty()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("FontManager: no font mapping for key '{}'", key)};
  }

  // The original resource is FONTS/<logical>.FNT. Its format is not decoded
  // yet; until it is, fall back to the TTF inside the backend only.
  App::Log::debug("[I2D] font key '{}' -> {}", key, logical);
  App::Log::warn(
      "[I2D] {}.FNT renderer unavailable; using temporary OMIKRON.TTF fallback", logical);

  const std::filesystem::path ttf_request{Resources::game_data_path(
      std::filesystem::path{std::string{K_TTF_FALLBACK_PATH}})};
  const std::filesystem::path ttf_resolved{Resources::resolve_case_insensitive(ttf_request)};

  auto font{FontResource::load_ttf_fallback(ttf_resolved, K_FALLBACK_FONT_SIZE)};
  if (!font) {
    return std::expected<void, std::string>{std::unexpect, font.error()};
  }
  m_fonts.emplace(key, std::move(font).value());
  return {};
}

const FontResource* FontManager::font_for_key(const char key) const {
  const auto found{m_fonts.find(key)};
  return found == m_fonts.end() ? nullptr : &found->second;
}

std::string_view FontManager::font_logical_name(const char key) {
  // Recovered Runtime font-key registry.
  switch (key) {
    case 'I': return "MENUINTR";
    case 'M': return "MENUSAVE";
    case 'D': return "DIALOGUE";
    case 'R': return "DIALSELE";
    case 'P': return "PARCHEMI";
    case 'C': return "COMPUTER";
    case 'S': return "SNEAK";
    case 'J': return "JOURNAL";
    case 'V': return "VOIXOFF";
    case '1': return "GENERIC1";
    case '2': return "GENERIC2";
    case '3': return "GENERIC3";
    case 'L': return "SMALL";
    default:   return {};
  }
}

}  // namespace App::Interface

// NOLINTEND(misc-include-cleaner)

#include "Core/Interface/FontManager.hpp"

// NOLINTBEGIN(misc-include-cleaner)
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/FontFNT.hpp"
#include "Core/Resources.hpp"

namespace App::Interface {

namespace {

/// Path to the temporary fallback TTF (see FontManager docs). Lives only in
/// this backend; no interface or renderer code references it.
constexpr std::string_view K_TTF_FALLBACK_PATH{"FONTS/OMIKRON.TTF"};

unsigned int decode_utf8(const char*& cursor, const char* end);

std::expected<std::vector<std::byte>, std::string> read_game_file(
    const std::filesystem::path& relative_path) {
  const std::filesystem::path request{Resources::game_data_path(relative_path)};
  const std::filesystem::path resolved{Resources::resolve_case_insensitive(request)};
  std::size_t size{0};
  void* raw{SDL_LoadFile(resolved.string().c_str(), &size)};
  if (raw == nullptr) {
    return std::expected<std::vector<std::byte>, std::string>{
        std::unexpect, fmt::format("cannot load '{}'", resolved.string())};
  }
  const auto* bytes{static_cast<const std::byte*>(raw)};
  std::vector<std::byte> result(bytes, bytes + size);
  SDL_free(raw);
  return result;
}

/// Flips the atlas rows so the texture matches Texture2D's bottom-up upload
/// convention (row 0 = bottom). The atlas is produced top-down by ImGui.
std::vector<std::uint8_t> flip_rows(
    const unsigned char* pixels, const int width, const int height) {
  const std::size_t row_bytes{static_cast<std::size_t>(width) * 4U};
  std::vector<std::uint8_t> flipped(
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
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
      m_size_pixels(other.m_size_pixels),
      m_reference_scale(other.m_reference_scale),
      m_retail_glyphs(other.m_retail_glyphs),
      m_line_height(other.m_line_height),
      m_is_retail_fnt(other.m_is_retail_fnt) {
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
    m_reference_scale = other.m_reference_scale;
    m_retail_glyphs = other.m_retail_glyphs;
    m_line_height = other.m_line_height;
    m_is_retail_fnt = other.m_is_retail_fnt;
    other.m_font = nullptr;
    other.m_baked = nullptr;
  }
  return *this;
}

FontResource::~FontResource() = default;

std::expected<FontResource, std::string> FontResource::load_ttf_fallback(
    const std::filesystem::path& ttf_path, const float size_pixels, const float logical_size) {
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
    return std::expected<FontResource, std::string>{
        std::unexpect, fmt::format("FontManager: cannot load TTF '{}'", ttf_path.string())};
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
  // Font atlases are scalable content: linear filtering avoids harsh
  // stair-stepping when the glyphs are sampled at a size other than 1:1.
  auto texture{Texture2D::create(width,
      height,
      std::span<const std::uint8_t>{flipped},
      k_linear_data_texture_policy.encoding,
      k_linear_data_texture_policy.filter)};
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
  resource.m_reference_scale = size_pixels / logical_size;
  resource.m_line_height = logical_size;
  return resource;
}

std::expected<FontResource, std::string> FontResource::load_retail_fnt(
    const Omikron::FontFntData& font,
    const int letter_spacing,
    const int blank_width,
    const int line_height) {
  APP_PROFILE_FUNCTION();

  struct Placement {
    std::size_t x{0};
    std::size_t y{0};
  };
  constexpr std::size_t k_min_atlas_width{256U};
  constexpr std::size_t k_padding{1U};
  std::size_t widest{0};
  for (const Omikron::FontFntGlyph& glyph : font.glyphs) {
    if (!glyph.empty()) {
      auto expected_coverage{Omikron::FontFNT::checked_product(
          static_cast<std::size_t>(glyph.width), static_cast<std::size_t>(glyph.height))};
      if (!expected_coverage || glyph.coverage.size() != expected_coverage.value()) {
        return std::expected<FontResource, std::string>{
            std::unexpect, "FontManager: retail FNT glyph coverage size is inconsistent"};
      }
    }
    widest = std::max(widest, static_cast<std::size_t>(glyph.width));
  }
  const std::size_t atlas_width{std::max(k_min_atlas_width, widest + (2U * k_padding))};
  std::array<Placement, 256> placements;
  std::size_t cursor_x{k_padding};
  std::size_t cursor_y{k_padding};
  std::size_t row_height{0};
  for (std::size_t index{0}; index < font.glyphs.size(); ++index) {
    const Omikron::FontFntGlyph& glyph{font.glyphs.at(index)};
    if (glyph.empty()) {
      continue;
    }
    const std::size_t glyph_width{glyph.width};
    const std::size_t glyph_height{glyph.height};
    if (cursor_x + glyph_width + k_padding > atlas_width) {
      cursor_x = k_padding;
      cursor_y += row_height + k_padding;
      row_height = 0U;
    }
    placements.at(index) = Placement{.x = cursor_x, .y = cursor_y};
    cursor_x += glyph_width + k_padding;
    row_height = std::max(row_height, glyph_height);
  }
  const std::size_t atlas_height{std::max<std::size_t>(2U, cursor_y + row_height + k_padding)};
  auto pixel_count{Omikron::FontFNT::checked_product(atlas_width, atlas_height)};
  if (!pixel_count || pixel_count.value() > std::numeric_limits<std::size_t>::max() / 4U ||
      atlas_width > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      atlas_height > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::expected<FontResource, std::string>{
        std::unexpect, "FontManager: retail FNT atlas dimensions overflow"};
  }
  std::vector<std::uint8_t> pixels(pixel_count.value() * 4U, 0U);

  FontResource resource;
  resource.m_is_retail_fnt = true;
  resource.m_line_height = static_cast<float>(line_height);
  resource.m_size_pixels = static_cast<float>(line_height);
  resource.m_reference_scale = 1.0F;
  for (std::size_t index{0}; index < font.glyphs.size(); ++index) {
    const Omikron::FontFntGlyph& source{font.glyphs.at(index)};
    Glyph& target{resource.m_retail_glyphs.at(index)};
    target.advance_x = Omikron::fnt_glyph_advance(source, letter_spacing, blank_width);
    if (source.empty()) {
      continue;
    }
    const Placement placement{placements.at(index)};
    for (std::size_t row{0}; row < static_cast<std::size_t>(source.height); ++row) {
      for (std::size_t column{0}; column < static_cast<std::size_t>(source.width); ++column) {
        const std::size_t source_index{(row * static_cast<std::size_t>(source.width)) + column};
        const std::size_t destination_y{atlas_height - 1U - (placement.y + row)};
        const std::size_t destination_index{
            ((destination_y * atlas_width) + placement.x + column) * 4U};
        pixels.at(destination_index) = 255U;
        pixels.at(destination_index + 1U) = 255U;
        pixels.at(destination_index + 2U) = 255U;
        const unsigned int intensity{source.coverage.at(source_index)};
        pixels.at(destination_index + 3U) =
            static_cast<std::uint8_t>(((intensity * 255U) + 15U) / 31U);
      }
    }
    target.visible = true;
    target.u_left = static_cast<float>(placement.x) / static_cast<float>(atlas_width);
    target.u_right =
        static_cast<float>(placement.x + source.width) / static_cast<float>(atlas_width);
    target.v_top = 1.0F - (static_cast<float>(placement.y) / static_cast<float>(atlas_height));
    target.v_bottom =
        1.0F - (static_cast<float>(placement.y + source.height) / static_cast<float>(atlas_height));
    target.x1 = static_cast<float>(source.width);
    target.y0 = Omikron::fnt_glyph_top(source, 0.0F, line_height);
    target.y1 = target.y0 + static_cast<float>(source.height);
  }

  auto texture{Texture2D::create(static_cast<int>(atlas_width),
      static_cast<int>(atlas_height),
      pixels,
      TextureColorEncoding::k_legacy_encoded,
      TextureFilter::k_nearest)};
  if (!texture) {
    return std::expected<FontResource, std::string>{
        std::unexpect, fmt::format("FontManager: {}", texture.error())};
  }
  resource.m_texture.emplace(std::move(texture).value());
  return resource;
}

std::optional<FontResource::Glyph> FontResource::glyph_for(const char32_t codepoint) const {
  if (m_is_retail_fnt) {
    if (codepoint > 0xFFU) {
      return std::nullopt;
    }
    return m_retail_glyphs.at(static_cast<std::uint8_t>(codepoint));
  }
  if (m_baked == nullptr || codepoint > 0xFFFFU) {
    return std::nullopt;
  }

  const ImFontGlyph* glyph{m_baked->FindGlyph(static_cast<ImWchar>(codepoint))};
  if (glyph == nullptr) {
    return std::nullopt;
  }

  // ImGui V0/V1 are expressed in a top-down atlas; the atlas was flipped to
  // bottom-up at upload, so the final V coordinates are their complement.
  // Corner offsets and advance are normalized from physical atlas pixels to
  // reference units so layout stays resolution-independent. Invisible glyphs
  // (notably spaces) deliberately retain these metrics: although they emit no
  // quad, AdvanceX is part of the text layout.
  const float scale{m_reference_scale};
  return Glyph{.visible = glyph->Visible != 0U,
      .u_left = glyph->U0,
      .u_right = glyph->U1,
      .v_top = 1.0F - glyph->V0,
      .v_bottom = 1.0F - glyph->V1,
      .x0 = glyph->X0 / scale,
      .y0 = glyph->Y0 / scale,
      .x1 = glyph->X1 / scale,
      .y1 = glyph->Y1 / scale,
      .advance_x = glyph->AdvanceX / scale};
}

std::optional<FontResource::Glyph> FontResource::next_glyph(
    const std::string_view text, std::size_t& byte_offset) const {
  if (byte_offset >= text.size()) {
    return std::nullopt;
  }
  if (m_is_retail_fnt) {
    const auto glyph_index{static_cast<unsigned char>(text.at(byte_offset))};
    ++byte_offset;
    return m_retail_glyphs.at(glyph_index);
  }
  const char* cursor{text.data() + byte_offset};
  const char* end{text.data() + text.size()};
  const unsigned int codepoint{decode_utf8(cursor, end)};
  byte_offset = static_cast<std::size_t>(cursor - text.data());
  return glyph_for(static_cast<char32_t>(codepoint));
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
  float width{0.0F};
  std::size_t byte_offset{0};
  while (byte_offset < text.size()) {
    const auto glyph{next_glyph(text, byte_offset)};
    if (glyph.has_value()) {
      width += glyph->advance_x;
    }
  }
  return width;
}

float FontResource::line_height() const {
  return m_line_height;
}

std::size_t FontManager::raster_bucket(const float reference_scale) {
  // Nearest 2 px, minimum 2 px, so resizing a window does not rebuild an
  // atlas dozens of times per second.
  const float desired{k_ttf_fallback_logical_size * reference_scale};
  const int bucket{static_cast<int>(std::lround(desired / 2.0F)) * 2};
  return static_cast<std::size_t>(std::max(2, bucket));
}

std::expected<void, std::string> FontManager::load_font(const char key) {
  APP_PROFILE_FUNCTION();

  if (m_retail_fonts.contains(key) || m_fonts.contains(key)) {
    return {};
  }

  const auto entry{font_registry_entry(key)};
  if (!entry.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("FontManager: no font mapping for key '{}'", key)};
  }

  App::Log::debug(LogCategory::I2D, "font key '{}' -> {}", key, entry->logical_name);

  auto retail{load_retail_font(key, entry.value())};
  if (retail) {
    m_retail_fonts.emplace(key, std::move(retail).value());
    return {};
  }
  App::Log::warn(LogCategory::I2D,
      "{}.FNT unavailable or corrupt ({}); using OMIKRON.TTF fallback",
      entry->logical_name,
      retail.error());

  const std::filesystem::path ttf_request{
      Resources::game_data_path(std::filesystem::path{std::string{K_TTF_FALLBACK_PATH}})};
  const std::filesystem::path ttf_resolved{Resources::resolve_case_insensitive(ttf_request)};

  auto font{FontResource::load_ttf_fallback(
      ttf_resolved, k_ttf_fallback_logical_size, fallback_logical_height(key))};
  if (!font) {
    return std::expected<void, std::string>{std::unexpect, font.error()};
  }
  m_fonts[key].emplace(raster_bucket(1.0F), std::move(font).value());
  return {};
}

const FontResource* FontManager::ensure_font(const char key, const float reference_scale) {
  APP_PROFILE_FUNCTION();

  const auto retail_found{m_retail_fonts.find(key)};
  if (retail_found != m_retail_fonts.end()) {
    return &retail_found->second;
  }

  if (!m_fonts.contains(key)) {
    const auto loaded{load_font(key)};
    if (!loaded) {
      App::Log::warn(LogCategory::I2D, "font key '{}': {}", key, loaded.error());
      return nullptr;
    }
    const auto newly_retail{m_retail_fonts.find(key)};
    if (newly_retail != m_retail_fonts.end()) {
      return &newly_retail->second;
    }
  }

  const std::size_t bucket{raster_bucket(reference_scale)};

  auto key_found{m_fonts.find(key)};
  if (key_found != m_fonts.end()) {
    const auto bucket_found{key_found->second.find(bucket)};
    if (bucket_found != key_found->second.end()) {
      return &bucket_found->second;
    }
  }

  const auto entry{font_registry_entry(key)};
  if (!entry.has_value()) {
    return nullptr;
  }

  const std::filesystem::path ttf_request{
      Resources::game_data_path(std::filesystem::path{std::string{K_TTF_FALLBACK_PATH}})};
  const std::filesystem::path ttf_resolved{Resources::resolve_case_insensitive(ttf_request)};

  auto font{FontResource::load_ttf_fallback(
      ttf_resolved, static_cast<float>(bucket), fallback_logical_height(key))};
  if (!font) {
    App::Log::warn(LogCategory::I2D, "font key '{}' at {} px: {}", key, bucket, font.error());
    return nullptr;
  }
  const auto inserted{m_fonts[key].emplace(bucket, std::move(font).value())};
  return &inserted.first->second;
}

const FontResource* FontManager::font_for_key(const char key) const {
  const auto retail_found{m_retail_fonts.find(key)};
  if (retail_found != m_retail_fonts.end()) {
    return &retail_found->second;
  }
  const auto found{m_fonts.find(key)};
  if (found == m_fonts.end()) {
    return nullptr;
  }
  const auto default_bucket{found->second.find(raster_bucket(1.0F))};
  return default_bucket == found->second.end() ? nullptr : &default_bucket->second;
}

std::string_view FontManager::font_logical_name(const char key) {
  const auto entry{font_registry_entry(key)};
  return entry.has_value() ? entry->logical_name : std::string_view{};
}

std::optional<FontRegistryEntry> FontManager::font_registry_entry(const char key) {
  // Recovered Runtime font-key registry: resource, letter spacing, blank
  // width, and logical line height.
  switch (key) {
    case 'I':
      return FontRegistryEntry{
          .logical_name = "MENUINTR", .letter_spacing = 2, .blank_width = 15, .line_height = 36};
    case 'M':
      return FontRegistryEntry{
          .logical_name = "MENUSAVE", .letter_spacing = 1, .blank_width = 8, .line_height = 17};
    case 'D':
      return FontRegistryEntry{
          .logical_name = "DIALOGUE", .letter_spacing = 1, .blank_width = 6, .line_height = 17};
    case 'R':
      return FontRegistryEntry{
          .logical_name = "DIALSELE", .letter_spacing = -1, .blank_width = 6, .line_height = 17};
    case 'P':
      return FontRegistryEntry{
          .logical_name = "PARCHEMI", .letter_spacing = 1, .blank_width = 6, .line_height = 17};
    case 'C':
      return FontRegistryEntry{
          .logical_name = "COMPUTER", .letter_spacing = 0, .blank_width = 6, .line_height = 14};
    case 'S':
      return FontRegistryEntry{
          .logical_name = "SNEAK", .letter_spacing = 1, .blank_width = 6, .line_height = 20};
    case 'J':
      return FontRegistryEntry{
          .logical_name = "JOURNAL", .letter_spacing = 1, .blank_width = 6, .line_height = 17};
    case 'V':
      return FontRegistryEntry{
          .logical_name = "VOIXOFF", .letter_spacing = 1, .blank_width = 6, .line_height = 23};
    case '1':
      return FontRegistryEntry{
          .logical_name = "GENERIC1", .letter_spacing = 2, .blank_width = 6, .line_height = 12};
    case '2':
      return FontRegistryEntry{
          .logical_name = "GENERIC2", .letter_spacing = 3, .blank_width = 6, .line_height = 18};
    case '3':
      return FontRegistryEntry{
          .logical_name = "GENERIC3", .letter_spacing = 3, .blank_width = 6, .line_height = 24};
    case 'L':
      return FontRegistryEntry{
          .logical_name = "SMALL", .letter_spacing = 0, .blank_width = 6, .line_height = 12};
    default:
      return std::nullopt;
  }
}

std::expected<FontResource, std::string> FontManager::load_retail_font(
    const char key, const FontRegistryEntry& entry) {
  const std::filesystem::path relative{fmt::format("FONTS/{}.FNT", entry.logical_name)};
  auto bytes{read_game_file(relative)};
  if (!bytes) {
    return std::expected<FontResource, std::string>{std::unexpect, bytes.error()};
  }
  auto parsed{Omikron::FontFNT::load(bytes.value())};
  if (!parsed) {
    return std::expected<FontResource, std::string>{std::unexpect, parsed.error()};
  }
  std::size_t non_empty{0};
  for (const Omikron::FontFntGlyph& glyph : parsed->glyphs) {
    non_empty += static_cast<std::size_t>(!glyph.empty());
  }
  App::Log::debug(LogCategory::I2D,
      "retail font key '{}' loaded {} glyphs from {} (lineHeight={} spacing={} blankWidth={})",
      key,
      non_empty,
      relative.string(),
      entry.line_height,
      entry.letter_spacing,
      entry.blank_width);
  return FontResource::load_retail_fnt(
      parsed.value(), entry.letter_spacing, entry.blank_width, entry.line_height);
}

}  // namespace App::Interface

// NOLINTEND(misc-include-cleaner)

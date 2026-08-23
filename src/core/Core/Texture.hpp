#pragma once

#include <glad/glad.h>

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace App {

/// Texture sampling filter policy. Upload call sites select this explicitly;
/// the default nearest mode is retained only for generated/utility textures.
enum class TextureFilter : std::uint8_t { k_nearest, k_linear };

/// Meaning of the RGB numbers stored in a texture. `k_legacy_encoded` is
/// intentionally distinct from `k_linear` even though both use an ordinary
/// (non-sRGB) OpenGL format: legacy values are display encoded, but must reach
/// shaders unchanged for Runtime-compatible filtering and blending.
enum class TextureColorEncoding : std::uint8_t {
  k_linear,
  k_srgb,
  k_legacy_encoded,
};

/// Storage precision for textures allocated as render targets.
enum class TextureStorageFormat : std::uint8_t {
  k_rgba8_unorm,
  k_rgba16_unorm,
  k_rgba16_float,
};

/// Explicit upload policy shared by all retail game textures.
struct TextureUploadPolicy {
  TextureColorEncoding encoding{TextureColorEncoding::k_linear};
  TextureFilter filter{TextureFilter::k_nearest};

  friend constexpr bool operator==(
      const TextureUploadPolicy&, const TextureUploadPolicy&) = default;
};

inline constexpr TextureUploadPolicy k_modern_color_texture_policy{
    .encoding = TextureColorEncoding::k_srgb, .filter = TextureFilter::k_linear};

inline constexpr TextureUploadPolicy k_legacy_effect_texture_policy{
    .encoding = TextureColorEncoding::k_legacy_encoded, .filter = TextureFilter::k_linear};

inline constexpr TextureUploadPolicy k_linear_data_texture_policy{
    .encoding = TextureColorEncoding::k_linear, .filter = TextureFilter::k_linear};

enum class GameColorTextureUsage : std::uint8_t {
  k_modern = 1U,
  k_legacy_effect = 2U,
  k_both = 3U,
};

/// GL internal format for an RGBA8 upload. This is context-free so color
/// policy can be regression-tested without creating an OpenGL context.
[[nodiscard]] constexpr GLint texture_upload_internal_format(const TextureColorEncoding encoding) {
  return encoding == TextureColorEncoding::k_srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
}

/// GL internal format for render-target storage.
[[nodiscard]] constexpr GLint texture_storage_internal_format(const TextureStorageFormat format) {
  switch (format) {
    case TextureStorageFormat::k_rgba16_unorm:
      return GL_RGBA16;
    case TextureStorageFormat::k_rgba16_float:
      return GL_RGBA16F;
    case TextureStorageFormat::k_rgba8_unorm:
    default:
      return GL_RGBA8;
  }
}

/// RAII 2D texture with explicit color semantics and storage policy.
class Texture2D {
 public:
  /// Uploads RGBA8 pixel data (width * height * 4 bytes).
  ///
  static std::expected<Texture2D, std::string> create(int width,
      int height,
      std::span<const std::uint8_t> rgba8,
      TextureColorEncoding encoding,
      TextureFilter filter = TextureFilter::k_nearest);

  /// Allocates uninitialised render-target storage with linear filtering and
  /// clamp-to-edge wrapping.
  static std::expected<Texture2D, std::string> create_render_target(
      int width, int height, TextureColorEncoding encoding, TextureStorageFormat storage_format);

  Texture2D(Texture2D&& other) noexcept;
  Texture2D& operator=(Texture2D&& other) noexcept;
  ~Texture2D();

  Texture2D(const Texture2D&) = delete;
  Texture2D& operator=(const Texture2D&) = delete;

  /// Binds the texture to the given texture image unit (unit 0 == GL_TEXTURE0).
  void bind(std::uint32_t unit) const;
  static void unbind();

  /// Replaces the stored RGBA8 pixels in place (glTexSubImage2D). The input
  /// must provide at least width * height * 4 bytes. No-op when the texture
  /// has not been allocated.
  void update(std::span<const std::uint8_t> rgba8) const;

  [[nodiscard]] GLuint id() const;
  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] TextureColorEncoding color_encoding() const;
  [[nodiscard]] TextureFilter filter() const;
  [[nodiscard]] TextureStorageFormat storage_format() const;

  /// Generates RGBA8 checkerboard pixels with squares_per_side cells per edge.
  ///
  /// Colors are given in sRGB space in [0, 1]. Returns an error for
  /// non-positive dimensions.
  static std::expected<std::vector<std::uint8_t>, std::string> generate_checkerboard(int width,
      int height,
      int squares_per_side,
      std::array<float, 3> color_a,
      std::array<float, 3> color_b);

 private:
  /// Assumes ownership of an already uploaded texture.
  Texture2D(int width,
      int height,
      GLuint id,
      TextureColorEncoding encoding,
      TextureFilter filter,
      TextureStorageFormat storage_format);

  GLuint m_id{0};
  int m_width{0};
  int m_height{0};
  TextureColorEncoding m_color_encoding{TextureColorEncoding::k_linear};
  TextureFilter m_filter{TextureFilter::k_nearest};
  TextureStorageFormat m_storage_format{TextureStorageFormat::k_rgba8_unorm};
};

/// One authored RGBA image with only the GPU color representations required
/// by its draw paths. Both representations are built from the same decoded
/// bytes, never by converting one GPU texture into the other.
class GameColorTexture {
 public:
  static std::expected<GameColorTexture, std::string> create(
      int width, int height, std::span<const std::uint8_t> rgba8, GameColorTextureUsage usage);

  GameColorTexture(GameColorTexture&&) noexcept = default;
  GameColorTexture& operator=(GameColorTexture&&) noexcept = default;
  GameColorTexture(const GameColorTexture&) = delete;
  GameColorTexture& operator=(const GameColorTexture&) = delete;

  [[nodiscard]] const Texture2D* modern() const;
  [[nodiscard]] const Texture2D* legacy_effect() const;

 private:
  GameColorTexture(std::optional<Texture2D> modern, std::optional<Texture2D> legacy_effect);

  std::optional<Texture2D> m_modern;
  std::optional<Texture2D> m_legacy_effect;
};

}  // namespace App

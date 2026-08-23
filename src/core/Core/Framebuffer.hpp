#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "Core/Texture.hpp"

namespace App {

enum class DepthStencilFormat : std::uint8_t {
  k_none,
  k_depth24,
  k_depth24_stencil8,
};

struct FramebufferDescription {
  TextureColorEncoding color_encoding{TextureColorEncoding::k_linear};
  TextureStorageFormat color_storage{TextureStorageFormat::k_rgba8_unorm};
  DepthStencilFormat depth_stencil{DepthStencilFormat::k_none};
};

/// RAII offscreen framebuffer with explicit color-domain/storage and optional
/// depth/stencil attachment policies.
class Framebuffer {
 public:
  static std::expected<Framebuffer, std::string> create(
      int width, int height, FramebufferDescription description);

  Framebuffer(Framebuffer&& other) noexcept;
  Framebuffer& operator=(Framebuffer&& other) noexcept;
  ~Framebuffer();

  Framebuffer(const Framebuffer&) = delete;
  Framebuffer& operator=(const Framebuffer&) = delete;

  /// Makes this framebuffer the active draw target.
  void bind() const;
  /// Restores the default (window) framebuffer.
  static void unbind();
  /// Copies depth and stencil into a same-sized compatible target.
  void blit_depth_stencil_to(const Framebuffer& destination) const;

  /// The colour attachment, bound like any other texture.
  [[nodiscard]] const Texture2D& color_texture() const;
  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] const FramebufferDescription& description() const;

 private:
  /// Assumes ownership of an already built framebuffer.
  Framebuffer(Texture2D color,
      GLuint depth_stencil_renderbuffer,
      GLuint framebuffer,
      int width,
      int height,
      FramebufferDescription description);

  Texture2D m_color;
  GLuint m_depth_stencil_renderbuffer{0};
  GLuint m_framebuffer{0};
  int m_width{0};
  int m_height{0};
  FramebufferDescription m_description;
};

}  // namespace App

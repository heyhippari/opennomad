#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "Core/Texture.hpp"

namespace App {

/// RAII offscreen framebuffer: one colour texture plus a depth renderbuffer.
///
/// Used for render-to-texture passes such as mirror reflections.
class Framebuffer {
 public:
  /// Allocates an sRGB-encoded RGBA8 colour attachment and a 24-bit depth
  /// buffer of the given size.
  static std::expected<Framebuffer, std::string> create(int width, int height);

  Framebuffer(Framebuffer&& other) noexcept;
  Framebuffer& operator=(Framebuffer&& other) noexcept;
  ~Framebuffer();

  Framebuffer(const Framebuffer&) = delete;
  Framebuffer& operator=(const Framebuffer&) = delete;

  /// Makes this framebuffer the active draw target.
  void bind() const;
  /// Restores the default (window) framebuffer.
  static void unbind();

  /// The colour attachment, bound like any other texture.
  [[nodiscard]] const Texture2D& color_texture() const;
  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;

 private:
  /// Assumes ownership of an already built framebuffer.
  Framebuffer(Texture2D color, GLuint depth_renderbuffer, GLuint framebuffer, int width,
              int height);

  Texture2D m_color;
  GLuint m_depth_renderbuffer{0};
  GLuint m_framebuffer{0};
  int m_width{0};
  int m_height{0};
};

}  // namespace App

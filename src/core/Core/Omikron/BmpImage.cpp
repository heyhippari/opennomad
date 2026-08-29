#include "Core/Omikron/BmpImage.hpp"

#include <SDL3/SDL_endian.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"

namespace App::Omikron {

namespace {

/// Bytes per pixel of the RGBA8888 conversion target.
constexpr std::size_t K_BYTES_PER_PIXEL{4};

/// SDL's packed formats expose their "value" in native byte order. Pick the
/// one whose in-memory layout is R, G, B, A on this platform, matching
/// glTexImage2D's GL_RGBA upload.
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
constexpr SDL_PixelFormat K_RGBA_BYTE_ORDER{SDL_PIXELFORMAT_RGBA8888};
#else
constexpr SDL_PixelFormat K_RGBA_BYTE_ORDER{SDL_PIXELFORMAT_ABGR8888};
#endif

}  // namespace

std::expected<BmpImage, std::string> BmpImageDecoder::load(const std::span<const std::byte> data) {
  APP_PROFILE_FUNCTION();

  if (data.empty()) {
    return std::expected<BmpImage, std::string>{std::unexpect, "BmpImage: no data"};
  }

  SDL_IOStream* stream{SDL_IOFromConstMem(data.data(), data.size())};
  if (stream == nullptr) {
    return std::expected<BmpImage, std::string>{
        std::unexpect, fmt::format("BmpImage: cannot create memory stream: {}", SDL_GetError())};
  }
  SDL_Surface* surface{SDL_LoadBMP_IO(stream, true)};
  if (surface == nullptr) {
    return std::expected<BmpImage, std::string>{
        std::unexpect, fmt::format("BmpImage: {}", SDL_GetError())};
  }

  SDL_Surface* converted{SDL_ConvertSurface(surface, K_RGBA_BYTE_ORDER)};
  SDL_DestroySurface(surface);
  if (converted == nullptr) {
    return std::expected<BmpImage, std::string>{
        std::unexpect, fmt::format("BmpImage: cannot convert to RGBA8888: {}", SDL_GetError())};
  }

  if (!SDL_LockSurface(converted)) {
    const std::string error{fmt::format("BmpImage: cannot lock surface: {}", SDL_GetError())};
    SDL_DestroySurface(converted);
    return std::expected<BmpImage, std::string>{std::unexpect, error};
  }

  BmpImage image{.width = converted->w,
      .height = converted->h,
      .rgba8 =
          std::vector<std::uint8_t>(static_cast<std::size_t>(converted->w) *
                                    static_cast<std::size_t>(converted->h) * K_BYTES_PER_PIXEL)};

  const auto* pixels{static_cast<const std::uint8_t*>(converted->pixels)};
  const std::size_t pitch{static_cast<std::size_t>(converted->pitch)};
  const std::size_t height{static_cast<std::size_t>(converted->h)};
  const std::size_t row_bytes{static_cast<std::size_t>(converted->w) * K_BYTES_PER_PIXEL};
  for (std::size_t row{0}; row < height; ++row) {
    // SDL stores row 0 at the top of the image; OpenGL texture uploads read
    // the first stored row as the bottom, so copy the rows bottom-up.
    const std::size_t source_row{height - row - 1U};
    std::memcpy(image.rgba8.data() + (row * row_bytes), pixels + (source_row * pitch), row_bytes);
  }

  SDL_UnlockSurface(converted);
  SDL_DestroySurface(converted);
  return image;
}

}  // namespace App::Omikron

#include "Core/Interface/I2DBumpBackground.hpp"

// NOLINTBEGIN(misc-include-cleaner)
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Interface/I2DBumpEffect.hpp"
#include "Core/Log.hpp"
#include "Core/Omikron/IndexedBmp8.hpp"
#include "Core/Resources.hpp"
#include "Core/Texture.hpp"

namespace App::Interface {

namespace {

constexpr int K_CANVAS_WIDTH{640};
constexpr int K_CANVAS_HEIGHT{480};
constexpr std::string_view K_CLOUD_PATH{"IMAGES/CLOUD.BMP"};

/// One original effect update per 1/30 s, matching the rest of OpenNomad's
/// recovered 30 Hz timing model. Runtime constants are per effect update.
constexpr float K_EFFECT_TICK_SECONDS{1.0F / 30.0F};
/// Largest backlog drained in one host frame (100 ms, mirroring the frame
/// timing model's k_max_dynamic_delta).
constexpr int K_MAX_TICKS_PER_UPDATE{3};

/// Reads a whole file through the case-insensitive game-data resolver.
std::expected<std::vector<std::byte>, std::string> read_file(const std::string& relative_path) {
  const std::filesystem::path root_relative{Resources::game_data_path(
      std::filesystem::path{relative_path})};
  const std::filesystem::path resolved{Resources::resolve_case_insensitive(root_relative)};

  std::size_t size{0};
  void* raw{SDL_LoadFile(resolved.string().c_str(), &size)};
  if (raw == nullptr) {
    return std::expected<std::vector<std::byte>, std::string>{std::unexpect,
        fmt::format("cannot read '{}' (resolved '{}'): {}",
            relative_path,
            resolved.string(),
            SDL_GetError())};
  }

  std::vector<std::byte> bytes(size);
  if (size > 0) {
    std::memcpy(bytes.data(), raw, size);
  }
  SDL_free(raw);
  return bytes;
}

}  // namespace

I2DBumpBackground::I2DBumpBackground(Texture2D canvas, I2DBumpEffect effect)
    : m_canvas(std::move(canvas)), m_effect(std::move(effect)) {
  // Runtime runs the effect once per original game frame (30 Hz); the first
  // frame is produced immediately when the menu opens. Advancing here also
  // uploads a defined surface before the first update(), avoiding an
  // uninitialised texture on the opening frame.
  m_effect.advance_one_tick();
  m_canvas.update(m_effect.rgba_frame());
}

std::expected<std::unique_ptr<I2DBumpBackground>, std::string> I2DBumpBackground::create() {
  APP_PROFILE_FUNCTION();

  auto file{read_file(std::string{K_CLOUD_PATH})};
  if (!file) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", file.error())};
  }

  auto bmp{Omikron::IndexedBmp8Decoder::load(std::span<const std::byte>{file.value()})};
  if (!bmp) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", bmp.error())};
  }
  const int width{bmp->width};
  const int height{bmp->height};

  auto effect{I2DBumpEffect::create(std::move(bmp).value())};
  if (!effect) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", effect.error())};
  }

  // The generated palette bytes are display-ready sRGB values (Runtime wrote
  // its 16-bit pixels straight to the surface), so the canvas uses sRGB
  // storage like the rest of the I2D pipeline.
  auto canvas{Texture2D::create(K_CANVAS_WIDTH, K_CANVAS_HEIGHT, /*srgb=*/true)};
  if (!canvas) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", canvas.error())};
  }

  App::Log::info("[I2D] background: IMAGES/CLOUD.BMP ({}x{}) — recovered Runtime bump effect",
      width,
      height);

  // The constructor is private; only the factory may build one.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return std::unique_ptr<I2DBumpBackground>{new I2DBumpBackground{
      std::move(canvas).value(), std::move(effect).value()}};
}

void I2DBumpBackground::update(const float delta_time) {
  APP_PROFILE_FUNCTION();

  // Runtime's constants are per original effect update, not per second, so
  // the host frame rate must not scale them. Accumulate host time and drain
  // it in fixed 30 Hz ticks; cap the backlog so a long pause (debugger,
  // focus loss) does not trigger a runaway catch-up loop.
  m_tick_accumulator += delta_time;

  int ticks{0};
  while (m_tick_accumulator >= K_EFFECT_TICK_SECONDS && ticks < K_MAX_TICKS_PER_UPDATE) {
    m_effect.advance_one_tick();
    m_tick_accumulator -= K_EFFECT_TICK_SECONDS;
    ++ticks;
  }
  if (m_tick_accumulator >= K_EFFECT_TICK_SECONDS) {
    // Backlog still exceeds one tick after the cap: drop it rather than
    // replaying every missed tick.
    m_tick_accumulator = 0.0F;
  }

  m_canvas.update(m_effect.rgba_frame());
}

}  // namespace App::Interface

// NOLINTEND(misc-include-cleaner)

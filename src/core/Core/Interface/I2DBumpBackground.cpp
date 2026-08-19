#include "Core/Interface/I2DBumpBackground.hpp"

// NOLINTBEGIN(misc-include-cleaner)
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"
#include "Core/Omikron/BmpImage.hpp"
#include "Core/Resources.hpp"
#include "Core/Texture.hpp"

namespace App::Interface {

namespace {

constexpr int K_CANVAS_WIDTH{640};
constexpr int K_CANVAS_HEIGHT{480};
constexpr int K_BYTES_PER_PIXEL{4};
constexpr std::string_view K_CLOUD_PATH{"IMAGES/CLOUD.BMP"};

/// Sine lookup table size (power of two for mask-based indexing).
constexpr int K_SINE_TABLE_SIZE{512};

/// Precomputed sin(2*pi*i/size) table so the per-pixel distortion does not
/// call std::sin 600k+ times per frame.
class SineTable {
 public:
  SineTable() {
    for (int index{0}; index < K_SINE_TABLE_SIZE; ++index) {
      const float phase{2.0F * std::numbers::pi_v<float> *
                        static_cast<float>(index) / static_cast<float>(K_SINE_TABLE_SIZE)};
      m_values.at(static_cast<std::size_t>(index)) = std::sin(phase);
    }
  }

  /// sin(2*pi*phase) with phase in [0,1) cycles.
  [[nodiscard]] float at(const float phase) const {
    const int scaled{static_cast<int>(phase * static_cast<float>(K_SINE_TABLE_SIZE))};
    return m_values.at(static_cast<std::size_t>(scaled & (K_SINE_TABLE_SIZE - 1)));
  }

 private:
  std::array<float, K_SINE_TABLE_SIZE> m_values{};
};

const SineTable& sine_table() {
  static const SineTable table{};
  return table;
}

/// Positive modulo (the result is always in [0, modulus)).
int positive_mod(const int value, const int modulus) {
  const int remainder{value % modulus};
  return remainder < 0 ? remainder + modulus : remainder;
}

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

I2DBumpBackground::I2DBumpBackground(Texture2D canvas,
    std::vector<std::uint8_t> source_rgba8_top_down,
    const int source_size)
    : m_canvas(std::move(canvas)),
      m_source(std::move(source_rgba8_top_down)),
      m_source_size(source_size),
      m_frame(static_cast<std::size_t>(K_CANVAS_WIDTH) *
              static_cast<std::size_t>(K_CANVAS_HEIGHT) * K_BYTES_PER_PIXEL) {}

std::expected<std::unique_ptr<I2DBumpBackground>, std::string> I2DBumpBackground::create() {
  APP_PROFILE_FUNCTION();

  auto file{read_file(std::string{K_CLOUD_PATH})};
  if (!file) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", file.error())};
  }
  auto bmp{Omikron::BmpImageDecoder::load(std::span<const std::byte>{file.value()})};
  if (!bmp) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", bmp.error())};
  }
  if (bmp->width <= 0 || bmp->height <= 0) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, "I2DBumpBackground: cloud source has non-positive dimensions"};
  }

  // Flip the bottom-up decoded rows into top-down storage for direct
  // sampling in update().
  const int width{bmp->width};
  const int height{bmp->height};
  const std::size_t row_bytes{static_cast<std::size_t>(width) * K_BYTES_PER_PIXEL};
  std::vector<std::uint8_t> top_down(static_cast<std::size_t>(width) *
                                     static_cast<std::size_t>(height) * K_BYTES_PER_PIXEL);
  for (int row{0}; row < height; ++row) {
    const std::size_t source_row{static_cast<std::size_t>(height - row - 1)};
    std::memcpy(top_down.data() + (static_cast<std::size_t>(row) * row_bytes),
        bmp->rgba8.data() + (source_row * row_bytes),
        row_bytes);
  }

  auto canvas{Texture2D::create(K_CANVAS_WIDTH, K_CANVAS_HEIGHT, /*srgb=*/false)};
  if (!canvas) {
    return std::expected<std::unique_ptr<I2DBumpBackground>, std::string>{
        std::unexpect, fmt::format("I2DBumpBackground: {}", canvas.error())};
  }

  App::Log::info("[I2D] background: IMAGES/CLOUD.BMP ({}x{}) — approximate bump animation",
      width,
      height);

  // The constructor is private; only the factory may build one.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return std::unique_ptr<I2DBumpBackground>{new I2DBumpBackground{
      std::move(canvas).value(), std::move(top_down), width}};
}

void I2DBumpBackground::update(const float delta_time) {
  APP_PROFILE_FUNCTION();

  m_time += delta_time;
  const SineTable& table{sine_table()};

  // First-pass approximation of I2D_Bump.c: scroll the cloud source with two
  // sine-based coordinate distortions. Not yet the recovered light/bump
  // pipeline — documented as an approximation in the header.
  for (int pixel_y{0}; pixel_y < K_CANVAS_HEIGHT; ++pixel_y) {
    for (int pixel_x{0}; pixel_x < K_CANVAS_WIDTH; ++pixel_x) {
      const float phase_a{(static_cast<float>(pixel_y) * 0.05F) + (m_time * 0.7F)};
      const float phase_b{(static_cast<float>(pixel_x) * 0.05F) + (m_time * 0.5F)};
      const float sample_x{static_cast<float>(pixel_x) + (40.0F * m_time) +
                           (8.0F * table.at(phase_a))};
      const float sample_y{static_cast<float>(pixel_y) + (12.0F * m_time) +
                           (8.0F * table.at(phase_b))};

      const int source_x{positive_mod(static_cast<int>(sample_x), m_source_size)};
      const int source_y{positive_mod(static_cast<int>(sample_y), m_source_size)};

      const std::size_t source_index{
          ((static_cast<std::size_t>(source_y) * static_cast<std::size_t>(m_source_size)) +
              static_cast<std::size_t>(source_x)) *
          K_BYTES_PER_PIXEL};
      const std::size_t frame_index{
          ((static_cast<std::size_t>(pixel_y) * static_cast<std::size_t>(K_CANVAS_WIDTH)) +
              static_cast<std::size_t>(pixel_x)) *
          K_BYTES_PER_PIXEL};

      m_frame.at(frame_index + 0U) = m_source.at(source_index + 0U);
      m_frame.at(frame_index + 1U) = m_source.at(source_index + 1U);
      m_frame.at(frame_index + 2U) = m_source.at(source_index + 2U);
      m_frame.at(frame_index + 3U) = 255U;
    }
  }

  m_canvas.update(std::span<const std::uint8_t>{m_frame});
}

}  // namespace App::Interface

// NOLINTEND(misc-include-cleaner)

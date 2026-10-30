#include "Core/WorldFog.hpp"

#include <glad/glad.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "Core/Shader.hpp"

namespace App::Fog {

namespace {

[[nodiscard]] std::uint8_t channel(const std::uint32_t color, const unsigned shift) {
  return static_cast<std::uint8_t>((color >> shift) & 0xFFU);
}

}  // namespace

void DayClock::advance(const float delta_seconds) {
  if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0F) {
    return;
  }
  m_pending_frames += delta_seconds * K_ENGINE_FRAMES_PER_SECOND;
  while (m_pending_frames >= 1.0F) {
    m_pending_frames -= 1.0F;
    tick();
  }
}

void DayClock::tick() {
  m_accumulator += 1.0F;
  if (m_accumulator > K_FRAMES_PER_CLOCK_STEP) {
    m_accumulator -= K_FRAMES_PER_CLOCK_STEP;
    m_value += K_CLOCK_STEP;
  }
}

void DayClock::set(const std::int64_t value) {
  // 0x0041E750 also restarts the frame accumulator.
  m_value = value;
  m_accumulator = 0.0F;
  m_pending_frames = 0.0F;
}

std::uint32_t day_color(const std::array<std::uint32_t, 4>& colors, const std::int64_t clock) {
  const std::int64_t time_of_day{((clock % K_DAY_LENGTH) + K_DAY_LENGTH) % K_DAY_LENGTH};
  const auto phase{static_cast<std::size_t>(time_of_day / K_DAY_PHASE_LENGTH)};
  const std::size_t next{(phase + 1U) & 3U};
  const float fraction{static_cast<float>(time_of_day % K_DAY_PHASE_LENGTH) /
                       static_cast<float>(K_DAY_PHASE_LENGTH)};

  std::uint32_t result{0};
  for (const unsigned shift : {16U, 8U, 0U}) {
    const std::uint8_t from{channel(colors.at(phase), shift)};
    const std::uint8_t to{channel(colors.at(next), shift)};
    const float delta{static_cast<float>(static_cast<int>(to) - static_cast<int>(from)) * fraction};
    const auto mixed{static_cast<std::uint8_t>(from + static_cast<int>(std::trunc(delta)))};
    result |= static_cast<std::uint32_t>(mixed) << shift;
  }
  return result;
}

std::int32_t effective_distance_metres(
    const std::int32_t area_metres, const std::int32_t clip_metres) {
  return area_metres == 0 || area_metres > clip_metres ? clip_metres : area_metres;
}

Parameters evaluate(
    const Environment& environment, const std::int64_t clock, const std::int32_t clip_metres) {
  const float end{static_cast<float>(
      static_cast<double>(effective_distance_metres(environment.distance_metres, clip_metres)) *
      K_INCHES_PER_METRE)};
  return Parameters{.color_rgb = day_color(environment.colors, clock),
      .start_inches = end * K_FOG_START_FRACTION,
      .end_inches = end,
      .fade_start_inches = end * K_FAR_FADE_START_FRACTION};
}

std::array<float, 3> encoded_color(const std::uint32_t color_rgb) {
  return {static_cast<float>(channel(color_rgb, 16U)) / 255.0F,
      static_cast<float>(channel(color_rgb, 8U)) / 255.0F,
      static_cast<float>(channel(color_rgb, 0U)) / 255.0F};
}

void apply_uniforms(const Shader& shader, const ShaderFog& fog, const float depth_scale) {
  const bool enabled{fog.enabled && fog.parameters.end_inches > fog.parameters.start_inches};
  shader.set_uniform_float("u_fog_enabled", enabled ? 1.0F : 0.0F);
  shader.set_uniform_float("u_fog_radial", fog.radial ? 1.0F : 0.0F);
  shader.set_uniform_float("u_fog_start", fog.parameters.start_inches);
  shader.set_uniform_float("u_fog_end", fog.parameters.end_inches);
  shader.set_uniform_float("u_fog_depth_scale", depth_scale);
  const std::array<float, 3> color{encoded_color(fog.parameters.color_rgb)};
  shader.set_uniform_vec3("u_fog_color", std::span<const GLfloat, 3>{color});
}

void apply_far_fade_uniforms(const Shader& shader, const ShaderFog& fog, const float depth_scale) {
  const bool enabled{fog.parameters.end_inches > fog.parameters.fade_start_inches};
  shader.set_uniform_float("u_far_fade_enabled", enabled ? 1.0F : 0.0F);
  shader.set_uniform_float("u_far_fade_radial", fog.radial ? 1.0F : 0.0F);
  shader.set_uniform_float("u_far_fade_start", fog.parameters.fade_start_inches);
  shader.set_uniform_float("u_far_fade_end", fog.parameters.end_inches);
  shader.set_uniform_float("u_far_fade_depth_scale", depth_scale);
}

}  // namespace App::Fog

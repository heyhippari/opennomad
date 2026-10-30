#include "Core/WorldLighting.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/WorldFog.hpp"

namespace App::Lighting {

Ambient base_ambient(const float base_light_level) {
  const auto level{static_cast<std::int32_t>(std::trunc(base_light_level * 255.0F))};
  return Ambient{.floor = grey(static_cast<std::uint32_t>(level)), .ceiling = 0xFFFFFFU};
}

std::uint32_t day_light_level(const std::int64_t clock) {
  const std::int64_t time_of_day{
      ((clock % Fog::K_DAY_LENGTH) + Fog::K_DAY_LENGTH) % Fog::K_DAY_LENGTH};
  const std::int64_t phase{time_of_day / Fog::K_DAY_PHASE_LENGTH};
  const std::int64_t half_span{K_DAY_LIGHT_SPAN >> 1U};
  const auto ramp{static_cast<std::uint32_t>(
      ((time_of_day % Fog::K_DAY_PHASE_LENGTH) * half_span) / Fog::K_DAY_PHASE_LENGTH)};
  switch (phase) {
    case 0:
      return (K_DAY_LIGHT_SPAN >> 1U) + ramp;
    case 1:
      return K_DAY_LIGHT_SPAN - ramp;
    case 2:
      return (K_DAY_LIGHT_SPAN >> 1U) - ramp;
    default:
      return ramp;
  }
}

Ambient day_ambient(const std::int64_t clock) {
  const std::uint32_t level{day_light_level(clock) & 0xFFU};
  return Ambient{.floor = grey(level), .ceiling = grey(level + K_DAY_CEILING_OFFSET)};
}

Ambient scene_ambient(
    const Fog::Environment& environment, const float base_light_level, const std::int64_t clock) {
  return environment.day_cycle_owner ? day_ambient(clock) : base_ambient(base_light_level);
}

LightSource prepare(const Omikron::Light& light) {
  LightSource source{.position = light.points.at(0),
      .direction = light.direction(),
      .box_min = light.points.at(0),
      .box_max = light.points.at(0),
      .outer_radius = light.attenuation_end,
      .inner_radius = light.attenuation_start,
      .intensity = light.intensity,
      .color_rgb = (static_cast<std::uint32_t>(light.color_bgra.at(2)) << 16U) |
                   (static_cast<std::uint32_t>(light.color_bgra.at(1)) << 8U) |
                   static_cast<std::uint32_t>(light.color_bgra.at(0))};
  for (const Runtime::Vec3& point : light.points) {
    source.box_min = {.x = std::min(source.box_min.x, point.x),
        .y = std::min(source.box_min.y, point.y),
        .z = std::min(source.box_min.z, point.z)};
    source.box_max = {.x = std::max(source.box_max.x, point.x),
        .y = std::max(source.box_max.y, point.y),
        .z = std::max(source.box_max.z, point.z)};
  }
  return source;
}

std::vector<LightSource> prepare_all(const Omikron::Model3DOData& model) {
  std::vector<LightSource> lights;
  lights.reserve(model.lights.size());
  for (const Omikron::Light& light : model.lights) {
    lights.push_back(prepare(light));
  }
  return lights;
}

bool touches(const LightSource& light, const Runtime::Vec3& centre, const float radius) {
  return light.box_min.x < centre.x + radius && light.box_max.x > centre.x - radius &&
         light.box_min.y < centre.y + radius && light.box_max.y > centre.y - radius &&
         light.box_min.z < centre.z + radius && light.box_max.z > centre.z - radius;
}

std::optional<float> node_scale(const LightSource& light, const Runtime::Vec3& node_position) {
  const float delta_x{node_position.x - light.position.x};
  const float delta_y{node_position.y - light.position.y};
  const float delta_z{node_position.z - light.position.z};
  const float distance_squared{(delta_x * delta_x) + (delta_y * delta_y) + (delta_z * delta_z)};
  if (distance_squared > light.outer_radius * light.outer_radius) {
    return std::nullopt;
  }
  float scale{light.intensity * 256.0F};
  if (distance_squared > light.inner_radius * light.inner_radius) {
    scale *= 1.0F - ((std::sqrt(distance_squared) - light.inner_radius) /
                        (light.outer_radius - light.inner_radius));
  }
  return scale;
}

NodeLightSet select_node_lights(const std::span<const LightSource> lights,
    const Runtime::Vec3& actor_centre,
    const float actor_radius,
    const Runtime::Vec3& node_position) {
  NodeLightSet selected;
  std::size_t index{0};
  for (const LightSource& light : lights) {
    if (selected.count >= K_MAX_NODE_LIGHTS) {
      break;
    }
    const std::size_t light_index{index++};
    if (!touches(light, actor_centre, actor_radius)) {
      continue;
    }
    const std::optional<float> scale{node_scale(light, node_position)};
    if (!scale.has_value()) {
      continue;
    }
    selected.lights.at(selected.count) =
        NodeLight{.light_index = light_index, .scale = scale.value()};
    ++selected.count;
  }
  return selected;
}

std::uint32_t accumulate(
    const std::uint32_t color, const float lambert, const std::uint32_t light_rgb) {
  if (!(lambert > 0.0F)) {
    return color;
  }
  const auto k{static_cast<std::int64_t>(std::trunc(lambert))};
  std::uint32_t result{0};
  for (const unsigned shift : {16U, 8U, 0U}) {
    const auto base{static_cast<std::int64_t>((color >> shift) & 0xFFU)};
    const auto light_byte{static_cast<std::int64_t>((light_rgb >> shift) & 0xFFU)};
    const std::int64_t sum{std::min<std::int64_t>(base + ((k * light_byte) >> 8), 0xFF)};
    result |= static_cast<std::uint32_t>(sum) << shift;
  }
  return result;
}

}  // namespace App::Lighting

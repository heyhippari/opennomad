#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/WorldFog.hpp"

namespace App::Lighting {

/// Render-context colour bounds: +0x1A0 (floor, also the lit-actor base) and
/// +0x1A4 (ceiling), packed 0x00RRGGBB.
struct Ambient {
  std::uint32_t floor{0};
  std::uint32_t ceiling{0xFFFFFFU};
};

/// Day-light span (0x004C2C28): the level ramps between 0 and this value.
inline constexpr std::uint32_t K_DAY_LIGHT_SPAN{0x80};
/// Runtime 0x0041E97D: ceiling = level + (0xFF - span).
inline constexpr std::uint32_t K_DAY_CEILING_OFFSET{0xFFU - K_DAY_LIGHT_SPAN};

/// Byte replicated to B/G/R.
[[nodiscard]] constexpr std::uint32_t grey(const std::uint32_t level) {
  const std::uint32_t byte{level & 0xFFU};
  return (byte << 16U) | (byte << 8U) | byte;
}

/// Context creation (0x0044E00D): floor = grey(_ftol(baseLight * 255)), ceiling white.
[[nodiscard]] Ambient base_ambient(float base_light_level);

/// Day-cycle owner level (0x0041E90C): 64 -> 128 -> 64 -> 0 -> 64 over the four phases.
[[nodiscard]] std::uint32_t day_light_level(std::int64_t clock);
[[nodiscard]] Ambient day_ambient(std::int64_t clock);

/// Ambient of one resident scene: the day cycle for its owner, otherwise the
/// decor's base light (Runtime only rewrites +0x1A0/+0x1A4 for the owner).
[[nodiscard]] Ambient scene_ambient(
    const Fog::Environment& environment, float base_light_level, std::int64_t clock);

/// Baked-colour path (0x004947F0): the packed colour is compared as one
/// unsigned integer, raised to the floor and then capped at the ceiling.
[[nodiscard]] constexpr std::uint32_t clamp_baked(
    const std::uint32_t color, const Ambient& ambient) {
  std::uint32_t result{color & 0xFFFFFFU};
  if (result < ambient.floor) {
    result = ambient.floor;
  }
  if (result > ambient.ceiling) {
    result = ambient.ceiling;
  }
  return result;
}

/// One decor light prepared like Runtime 0x00493C30 and 0x0048DEA0.
struct LightSource {
  Runtime::Vec3 position{};   ///< Record +0x30.
  Runtime::Vec3 direction{};  ///< normalize(+0x50 - +0x30), stored at +0x70.
  Runtime::Vec3 box_min{};    ///< AABB of the six shape points (overlap broadphase).
  Runtime::Vec3 box_max{};
  float outer_radius{0.0F};  ///< Record +0x18; +0xF0 holds its square.
  float inner_radius{0.0F};  ///< Record +0x1C; +0xF4 holds its square.
  float intensity{0.0F};     ///< Record +0x20.
  std::uint32_t color_rgb{0};
};

[[nodiscard]] LightSource prepare(const Omikron::Light& light);
[[nodiscard]] std::vector<LightSource> prepare_all(const Omikron::Model3DOData& model);

/// Light/actor pair broadphase (0x0048E590): the actor box is centre +/- radius.
[[nodiscard]] bool touches(const LightSource& light, const Runtime::Vec3& centre, float radius);

/// Per-node scale (0x00493E40): `intensity * 256`, attenuated linearly
/// between the inner and outer radius of the node's distance from the light.
/// Empty when the node is outside the outer radius.
[[nodiscard]] std::optional<float> node_scale(
    const LightSource& light, const Runtime::Vec3& node_position);

/// Maximum explicit lights evaluated per lit node.
inline constexpr std::size_t K_MAX_NODE_LIGHTS{16};

/// One explicit light paired with a lit node.
struct NodeLight {
  std::size_t light_index{0};  ///< Index into the scene's prepared lights.
  float scale{0.0F};           ///< node_scale() of the node.
};

/// The lights of one lit node, in scene light order.
struct NodeLightSet {
  std::array<NodeLight, K_MAX_NODE_LIGHTS> lights{};
  std::size_t count{0};

  [[nodiscard]] std::span<const NodeLight> active() const {
    return std::span<const NodeLight>{lights}.first(count);
  }
};

/// Runtime's lit-instance pairing: every light whose shape box overlaps the
/// actor box (touches()) and whose outer radius contains the node
/// (node_scale()), in scene order, up to K_MAX_NODE_LIGHTS.
[[nodiscard]] NodeLightSet select_node_lights(std::span<const LightSource> lights,
    const Runtime::Vec3& actor_centre,
    float actor_radius,
    const Runtime::Vec3& node_position);

/// Adds one light to a packed colour: `k = _ftol(lambert)` with
/// `lambert = -(normal . direction) * scale`; each byte becomes
/// `min(255, byte + (k * lightByte >> 8))`. Non-positive terms add nothing.
[[nodiscard]] std::uint32_t accumulate(std::uint32_t color, float lambert, std::uint32_t light_rgb);

/// Mesh flags that exclude a node from explicit lights (UV-scrolling surfaces).
inline constexpr std::uint32_t K_UNLIT_MESH_FLAGS{0x03000000U};

}  // namespace App::Lighting

#pragma once

#include <array>
#include <cstddef>

namespace App {

/// Interleaved vertex format shared by all meshes.
///
/// Attribute locations mirror the layout expected by the shaders:
/// 0 = position, 1 = normal, 2 = uv, 3 = colour.
struct Vertex {
  std::array<float, 3> position{};
  std::array<float, 3> normal{};
  std::array<float, 2> uv{};
  std::array<float, 4> color{};  ///< Linear RGBA in [0, 1].

  static constexpr unsigned int k_position_location{0};
  static constexpr unsigned int k_normal_location{1};
  static constexpr unsigned int k_uv_location{2};
  static constexpr unsigned int k_color_location{3};
};

static_assert(sizeof(Vertex) == 12U * sizeof(float));
static_assert(offsetof(Vertex, position) == 0);
static_assert(offsetof(Vertex, normal) == 3U * sizeof(float));
static_assert(offsetof(Vertex, uv) == 6U * sizeof(float));
static_assert(offsetof(Vertex, color) == 8U * sizeof(float));

}  // namespace App

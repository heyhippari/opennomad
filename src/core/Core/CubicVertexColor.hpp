#pragma once

#include <vector>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/Vertex.hpp"

/// Cubic vertex colour interpolation (OpenNomad enhancement).
///
/// Runtime interpolates baked decor colours linearly (Gouraud), which shows
/// the triangulation as creases and Mach bands. The enhancement replaces each
/// triangle's colour with a cubic Bezier triangle whose corners are the
/// Runtime colours and whose edge controls follow the colour gradient fitted
/// at each source vertex, so shading flows smoothly across vertices.
///
/// The gradient is stored per corner as two edge slope ratios: the
/// gradient's derivative along an edge divided by the colour difference
/// across it. Ratios are invariant under the affine decor pose, and applied
/// to the clamped colours on the GPU they follow the ambient floor/ceiling
/// clamp. Each ratio is limited to [0, 3] (Fritsch-Carlson), so every edge
/// curve is monotone and the triangle never leaves its corners' range.
namespace App::CubicColor {

/// Ratio of a straight Gouraud edge; used where no gradient is available.
inline constexpr float K_LINEAR_SLOPE{1.0F};
/// Largest ratio that keeps a cubic Hermite edge monotone.
inline constexpr float K_MAX_MONOTONE_SLOPE{3.0F};

/// Corner slopes of every layout group, parallel to `layout.groups` and to
/// each group's vertices. A corner's gradient is fitted from the triangles
/// that share its source 3DO vertex in the same object space; triangles whose
/// corners span object spaces keep linear edges.
[[nodiscard]] std::vector<std::vector<VertexColorSlopes>> build_corner_slopes(
    const Omikron::Model3DOData& model, const Omikron::PosedGeometryLayout& layout);

}  // namespace App::CubicColor

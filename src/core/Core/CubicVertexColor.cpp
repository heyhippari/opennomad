#include "Core/CubicVertexColor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/Vertex.hpp"

namespace App::CubicColor {

namespace {

using Vec3d = std::array<double, 3>;
/// Colour gradient of one source vertex: [axis][channel].
using Gradient = std::array<Vec3d, 3>;

/// Weighted least-squares normal equations of one source vertex:
/// sum(w dp dp^T) G = sum(w dp dc^T), with w = 1 / |dp|^2 so every neighbour
/// direction counts equally regardless of edge length.
struct GradientFit {
  std::array<Vec3d, 3> moment{};  ///< [axis][axis]
  Gradient rhs{};                 ///< [axis][channel]
  bool used{false};
};

/// Below half an 8-bit step the edge is flat and its ratio is irrelevant.
constexpr double K_FLAT_EDGE{0.5 / 255.0};
/// Tikhonov weight relative to the moment trace. Fans lying in one plane
/// leave the normal axis singular; this keeps the gradient tangent to them.
constexpr double K_REGULARIZATION{1.0e-3};

[[nodiscard]] Vec3d source_position(const Omikron::Model3DOData& model, const std::uint32_t index) {
  const Omikron::RawVertex& raw{model.vertices.at(index)};
  return {raw.position.x, raw.position.y, raw.position.z};
}

[[nodiscard]] Vec3d corner_rgb(const Vertex& vertex) {
  return {vertex.color.at(0), vertex.color.at(1), vertex.color.at(2)};
}

/// Solves (moment + lambda I) G = rhs; nullopt when the vertex has no usable
/// neighbour.
[[nodiscard]] std::optional<Gradient> solve(const GradientFit& fit) {
  const double trace{fit.moment.at(0).at(0) + fit.moment.at(1).at(1) + fit.moment.at(2).at(2)};
  if (!fit.used || !(trace > 0.0)) {
    return std::nullopt;
  }
  std::array<Vec3d, 3> a{fit.moment};
  for (std::size_t axis{0}; axis < 3U; ++axis) {
    a.at(axis).at(axis) += K_REGULARIZATION * trace;
  }
  const auto at = [&a](const std::size_t row, const std::size_t column) {
    return a.at(row).at(column);
  };
  // Cofactors of the symmetric 3x3 system.
  const std::array<Vec3d, 3> cofactor{{
      {(at(1, 1) * at(2, 2)) - (at(1, 2) * at(2, 1)),
          (at(0, 2) * at(2, 1)) - (at(0, 1) * at(2, 2)),
          (at(0, 1) * at(1, 2)) - (at(0, 2) * at(1, 1))},
      {(at(1, 2) * at(2, 0)) - (at(1, 0) * at(2, 2)),
          (at(0, 0) * at(2, 2)) - (at(0, 2) * at(2, 0)),
          (at(0, 2) * at(1, 0)) - (at(0, 0) * at(1, 2))},
      {(at(1, 0) * at(2, 1)) - (at(1, 1) * at(2, 0)),
          (at(0, 1) * at(2, 0)) - (at(0, 0) * at(2, 1)),
          (at(0, 0) * at(1, 1)) - (at(0, 1) * at(1, 0))},
  }};
  const double determinant{(at(0, 0) * cofactor.at(0).at(0)) + (at(0, 1) * cofactor.at(1).at(0)) +
                           (at(0, 2) * cofactor.at(2).at(0))};
  if (!std::isfinite(determinant) || std::abs(determinant) <= 0.0) {
    return std::nullopt;
  }
  Gradient gradient{};
  for (std::size_t row{0}; row < 3U; ++row) {
    for (std::size_t channel{0}; channel < 3U; ++channel) {
      double sum{0.0};
      for (std::size_t column{0}; column < 3U; ++column) {
        sum += cofactor.at(row).at(column) * fit.rhs.at(column).at(channel);
      }
      gradient.at(row).at(channel) = sum / determinant;
    }
  }
  return gradient;
}

/// Monotone slope ratios of the edge from `from` toward `to`.
[[nodiscard]] std::array<float, 3> edge_slopes(const std::optional<Gradient>& gradient,
    const Vec3d& from_position,
    const Vec3d& to_position,
    const Vec3d& from_color,
    const Vec3d& to_color) {
  std::array<float, 3> slopes{K_LINEAR_SLOPE, K_LINEAR_SLOPE, K_LINEAR_SLOPE};
  if (!gradient.has_value()) {
    return slopes;
  }
  for (std::size_t channel{0}; channel < 3U; ++channel) {
    const double difference{to_color.at(channel) - from_color.at(channel)};
    if (std::abs(difference) < K_FLAT_EDGE) {
      continue;
    }
    double derivative{0.0};
    for (std::size_t axis{0}; axis < 3U; ++axis) {
      derivative +=
          gradient->at(axis).at(channel) * (to_position.at(axis) - from_position.at(axis));
    }
    const double ratio{derivative / difference};
    slopes.at(channel) =
        std::isfinite(ratio)
            ? static_cast<float>(std::clamp(ratio, 0.0, static_cast<double>(K_MAX_MONOTONE_SLOPE)))
            : K_LINEAR_SLOPE;
  }
  return slopes;
}

/// Calls `visit(group, first_index)` for every whole triangle whose corners
/// all have a valid source in one object space.
template <typename Visit>
void for_each_triangle(const Omikron::Model3DOData& model,
    const Omikron::PosedGeometryLayout& layout,
    const Visit& visit) {
  for (std::size_t group_index{0}; group_index < layout.groups.size(); ++group_index) {
    const Omikron::MaterialGroup& group{layout.groups.at(group_index)};
    const std::vector<Omikron::PosedCornerSource>& sources{layout.sources.at(group_index)};
    for (std::size_t first{0}; first + 2U < group.indices.size(); first += 3U) {
      bool valid{true};
      for (std::size_t corner{0}; corner < 3U; ++corner) {
        const std::uint32_t index{group.indices.at(first + corner)};
        valid = valid && index < sources.size() && index < group.vertices.size() &&
                sources.at(index).vertex_index < model.vertices.size() &&
                sources.at(index).object_index == sources.at(group.indices.at(first)).object_index;
      }
      if (valid) {
        visit(group_index, first);
      }
    }
  }
}

}  // namespace

std::vector<std::vector<VertexColorSlopes>> build_corner_slopes(
    const Omikron::Model3DOData& model, const Omikron::PosedGeometryLayout& layout) {
  std::vector<std::vector<VertexColorSlopes>> slopes;
  slopes.reserve(layout.groups.size());
  for (const Omikron::MaterialGroup& group : layout.groups) {
    slopes.emplace_back(group.vertices.size(),
        VertexColorSlopes{.next = {K_LINEAR_SLOPE, K_LINEAR_SLOPE, K_LINEAR_SLOPE},
            .previous = {K_LINEAR_SLOPE, K_LINEAR_SLOPE, K_LINEAR_SLOPE}});
  }
  if (layout.sources.size() != layout.groups.size()) {
    return slopes;
  }

  // Corners sharing a source 3DO vertex share its colour, so a gradient per
  // source vertex is continuous across faces, materials and groups.
  std::vector<GradientFit> fits(model.vertices.size());
  for_each_triangle(model, layout, [&](const std::size_t group_index, const std::size_t first) {
    const Omikron::MaterialGroup& group{layout.groups.at(group_index)};
    const std::vector<Omikron::PosedCornerSource>& sources{layout.sources.at(group_index)};
    for (std::size_t corner{0}; corner < 3U; ++corner) {
      const std::uint32_t self{group.indices.at(first + corner)};
      const Vec3d self_position{source_position(model, sources.at(self).vertex_index)};
      const Vec3d self_color{corner_rgb(group.vertices.at(self))};
      GradientFit& fit{fits.at(sources.at(self).vertex_index)};
      for (const std::size_t step : {1U, 2U}) {
        const std::uint32_t other{group.indices.at(first + ((corner + step) % 3U))};
        const Vec3d other_position{source_position(model, sources.at(other).vertex_index)};
        const Vec3d other_color{corner_rgb(group.vertices.at(other))};
        Vec3d offset{};
        double length_squared{0.0};
        for (std::size_t axis{0}; axis < 3U; ++axis) {
          offset.at(axis) = other_position.at(axis) - self_position.at(axis);
          length_squared += offset.at(axis) * offset.at(axis);
        }
        if (!(length_squared > 0.0) || !std::isfinite(length_squared)) {
          continue;
        }
        const double weight{1.0 / length_squared};
        for (std::size_t row{0}; row < 3U; ++row) {
          for (std::size_t column{0}; column < 3U; ++column) {
            fit.moment.at(row).at(column) += weight * offset.at(row) * offset.at(column);
            fit.rhs.at(row).at(column) +=
                weight * offset.at(row) * (other_color.at(column) - self_color.at(column));
          }
        }
        fit.used = true;
      }
    }
  });

  std::vector<std::optional<Gradient>> gradients;
  gradients.reserve(fits.size());
  for (const GradientFit& fit : fits) {
    gradients.push_back(solve(fit));
  }

  for_each_triangle(model, layout, [&](const std::size_t group_index, const std::size_t first) {
    const Omikron::MaterialGroup& group{layout.groups.at(group_index)};
    const std::vector<Omikron::PosedCornerSource>& sources{layout.sources.at(group_index)};
    for (std::size_t corner{0}; corner < 3U; ++corner) {
      const std::uint32_t self{group.indices.at(first + corner)};
      const std::uint32_t next{group.indices.at(first + ((corner + 1U) % 3U))};
      const std::uint32_t previous{group.indices.at(first + ((corner + 2U) % 3U))};
      const std::uint32_t source{sources.at(self).vertex_index};
      const Vec3d self_position{source_position(model, source)};
      const Vec3d self_color{corner_rgb(group.vertices.at(self))};
      VertexColorSlopes& corner_slopes{slopes.at(group_index).at(self)};
      corner_slopes.next = edge_slopes(gradients.at(source),
          self_position,
          source_position(model, sources.at(next).vertex_index),
          self_color,
          corner_rgb(group.vertices.at(next)));
      corner_slopes.previous = edge_slopes(gradients.at(source),
          self_position,
          source_position(model, sources.at(previous).vertex_index),
          self_color,
          corner_rgb(group.vertices.at(previous)));
    }
  });
  return slopes;
}

}  // namespace App::CubicColor

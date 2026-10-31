#include "Core/CubicVertexColor.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/Vertex.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

namespace {

using Rgb = std::array<float, 3>;
using ColorField = std::function<Rgb(float, float)>;

/// Synthetic decor: a planar grid of `size` x `size` source vertices coloured
/// by `field`, emitted like Model3DO::build_posed_layout() with one corner per
/// triangle vertex in a single group.
struct GridFixture {
  App::Omikron::Model3DOData model;
  App::Omikron::PosedGeometryLayout layout;
};

[[nodiscard]] GridFixture make_grid(const std::size_t size, const ColorField& field) {
  GridFixture fixture;
  std::vector<Rgb> colors;
  for (std::size_t row{0}; row < size; ++row) {
    for (std::size_t column{0}; column < size; ++column) {
      App::Omikron::RawVertex vertex;
      vertex.position = {.x = static_cast<float>(column), .y = static_cast<float>(row), .z = 0.0F};
      fixture.model.vertices.push_back(vertex);
      colors.push_back(field(static_cast<float>(column), static_cast<float>(row)));
    }
  }
  App::Omikron::MaterialGroup group;
  std::vector<App::Omikron::PosedCornerSource> sources;
  const auto emit = [&](const std::size_t index) {
    App::Vertex vertex;
    const Rgb& color{colors.at(index)};
    vertex.color = {color.at(0), color.at(1), color.at(2), 1.0F};
    group.vertices.push_back(vertex);
    group.indices.push_back(static_cast<std::uint32_t>(group.vertices.size() - 1U));
    sources.push_back(App::Omikron::PosedCornerSource{
        .object_index = 0U, .vertex_index = static_cast<std::uint32_t>(index)});
  };
  for (std::size_t row{0}; row + 1U < size; ++row) {
    for (std::size_t column{0}; column + 1U < size; ++column) {
      const std::size_t corner{(row * size) + column};
      emit(corner);
      emit(corner + 1U);
      emit(corner + size + 1U);
      emit(corner);
      emit(corner + size + 1U);
      emit(corner + size);
    }
  }
  fixture.layout.groups.push_back(std::move(group));
  fixture.layout.sources.push_back(std::move(sources));
  return fixture;
}

void check_all_slopes(const std::vector<std::vector<App::VertexColorSlopes>>& slopes,
    const std::function<void(float)>& check) {
  for (const std::vector<App::VertexColorSlopes>& group : slopes) {
    for (const App::VertexColorSlopes& corner : group) {
      for (std::size_t channel{0}; channel < 3U; ++channel) {
        check(corner.next.at(channel));
        check(corner.previous.at(channel));
      }
    }
  }
}

}  // namespace

TEST_SUITE("Core::CubicVertexColor") {
  TEST_CASE("Slopes parallel the layout groups and their corners") {
    const GridFixture grid{make_grid(4U, [](const float x, const float y) {
      return Rgb{x * 0.1F, y * 0.1F, 0.5F};
    })};
    const auto slopes{App::CubicColor::build_corner_slopes(grid.model, grid.layout)};
    REQUIRE(slopes.size() == grid.layout.groups.size());
    CHECK(slopes.front().size() == grid.layout.groups.front().vertices.size());
  }

  TEST_CASE("A linear colour field reproduces Gouraud interpolation") {
    // The fitted gradient of a linear field is exact, so every edge slope is
    // the colour difference itself; the flat blue channel keeps ratio 1.
    const GridFixture grid{make_grid(5U, [](const float x, const float y) {
      return Rgb{0.1F + (0.05F * x), 0.2F + (0.03F * y) + (0.02F * x), 0.5F};
    })};
    const auto slopes{App::CubicColor::build_corner_slopes(grid.model, grid.layout)};
    check_all_slopes(slopes, [](const float ratio) {
      CHECK(ratio == doctest::Approx(App::CubicColor::K_LINEAR_SLOPE).epsilon(0.01));
    });
  }

  TEST_CASE("Slopes stay within the monotone range around a local maximum") {
    const GridFixture grid{make_grid(5U, [](const float x, const float y) {
      const float distance{((x - 2.0F) * (x - 2.0F)) + ((y - 2.0F) * (y - 2.0F))};
      const float value{1.0F / (1.0F + distance)};
      return Rgb{value, value * value, 1.0F - value};
    })};
    const auto slopes{App::CubicColor::build_corner_slopes(grid.model, grid.layout)};
    check_all_slopes(slopes, [](const float ratio) {
      CHECK(ratio >= 0.0F);
      CHECK(ratio <= App::CubicColor::K_MAX_MONOTONE_SLOPE);
    });

    // The peak's symmetric fan gives it a zero gradient: its edges leave it flat.
    const std::size_t peak{(2U * 5U) + 2U};
    const auto& sources{grid.layout.sources.front()};
    bool peak_seen{false};
    for (std::size_t corner{0}; corner < sources.size(); ++corner) {
      if (sources.at(corner).vertex_index == peak) {
        peak_seen = true;
        CHECK(slopes.front().at(corner).next.at(0) == doctest::Approx(0.0F).epsilon(0.01));
        CHECK(slopes.front().at(corner).previous.at(0) == doctest::Approx(0.0F).epsilon(0.01));
      }
    }
    CHECK(peak_seen);
  }

  TEST_CASE("A curved field bends interior edges away from Gouraud") {
    const GridFixture grid{make_grid(5U, [](const float x, const float /*y*/) {
      return Rgb{0.04F * x * x, 0.0F, 0.0F};
    })};
    const auto slopes{App::CubicColor::build_corner_slopes(grid.model, grid.layout)};
    bool curved{false};
    check_all_slopes(slopes, [&curved](const float ratio) {
      curved = curved || std::abs(ratio - App::CubicColor::K_LINEAR_SLOPE) > 0.1F;
    });
    CHECK(curved);
  }

  TEST_CASE("Triangles spanning object spaces keep linear edges") {
    GridFixture grid{make_grid(3U, [](const float x, const float y) {
      return Rgb{0.04F * x * x, 0.1F * y, 0.0F};
    })};
    // Move one corner of the first triangle into another object's space.
    grid.layout.sources.front().at(1).object_index = 1U;
    const auto slopes{App::CubicColor::build_corner_slopes(grid.model, grid.layout)};
    for (std::size_t corner{0}; corner < 3U; ++corner) {
      for (std::size_t channel{0}; channel < 3U; ++channel) {
        CHECK(slopes.front().at(corner).next.at(channel) == App::CubicColor::K_LINEAR_SLOPE);
        CHECK(slopes.front().at(corner).previous.at(channel) == App::CubicColor::K_LINEAR_SLOPE);
      }
    }
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

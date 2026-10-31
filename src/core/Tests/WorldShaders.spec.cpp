#include "Core/WorldShaders.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <string>
#include <string_view>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

namespace {

constexpr std::string_view K_VERSION{"#version 410 core\n"};
constexpr std::string_view K_PER_PIXEL_DEFINE{"#define PER_PIXEL_LIGHTING\n"};
constexpr std::string_view K_CUBIC_COLOR_DEFINE{"#define CUBIC_VERTEX_COLOR\n"};

/// The define must follow `#version` directly, which GLSL requires to come first.
bool defines_after_version(const std::string& source, const std::string_view define) {
  const std::size_t version{source.find(K_VERSION)};
  return version != std::string::npos &&
         source.compare(version + K_VERSION.size(), define.size(), define) == 0;
}

}  // namespace

TEST_SUITE("Core::WorldShaders") {
  TEST_CASE("Default options build the original Runtime sources unchanged") {
    const App::WorldShaderOptions original{};
    CHECK(App::world_vertex_shader(original) == App::K_WORLD_VERTEX_SHADER);
    CHECK(App::modern_world_fragment_shader(original) ==
          App::fogged_fragment_shader(App::K_MODERN_WORLD_FRAGMENT_BODY));
    CHECK(App::world_vertex_shader(original).find("#define") == std::string::npos);
    CHECK(App::modern_world_fragment_shader(original).find("#define") == std::string::npos);
  }

  TEST_CASE("Per-pixel lighting is a define on both stages") {
    const App::WorldShaderOptions per_pixel{.per_pixel_lighting = true};
    const std::string vertex{App::world_vertex_shader(per_pixel)};
    const std::string fragment{App::modern_world_fragment_shader(per_pixel)};
    CHECK(defines_after_version(vertex, K_PER_PIXEL_DEFINE));
    CHECK(defines_after_version(fragment, K_PER_PIXEL_DEFINE));

    // Only the define is added; the guarded code is the same source.
    std::string stripped{vertex};
    stripped.erase(stripped.find(K_PER_PIXEL_DEFINE), K_PER_PIXEL_DEFINE.size());
    CHECK(stripped == App::K_WORLD_VERTEX_SHADER);
  }

  TEST_CASE("Both stages declare the per-pixel interface they share") {
    for (const std::string_view symbol : {"v_normal", "v_normal_length"}) {
      CHECK(App::K_WORLD_VERTEX_SHADER.find(symbol) != std::string_view::npos);
      CHECK(App::K_MODERN_WORLD_FRAGMENT_BODY.find(symbol) != std::string_view::npos);
    }
    CHECK(
        App::K_MODERN_WORLD_FRAGMENT_BODY.find("per_pixel_actor_color") != std::string_view::npos);
  }

  TEST_CASE("Cubic vertex colour is a define on both stages") {
    const App::WorldShaderOptions cubic{.cubic_vertex_color = true};
    const std::string vertex{App::world_vertex_shader(cubic)};
    const std::string fragment{App::modern_world_fragment_shader(cubic)};
    CHECK(defines_after_version(vertex, K_CUBIC_COLOR_DEFINE));
    CHECK(defines_after_version(fragment, K_CUBIC_COLOR_DEFINE));

    std::string stripped{fragment};
    stripped.erase(stripped.find(K_CUBIC_COLOR_DEFINE), K_CUBIC_COLOR_DEFINE.size());
    CHECK(stripped == App::fogged_fragment_shader(App::K_MODERN_WORLD_FRAGMENT_BODY));
  }

  TEST_CASE("The cubic geometry stage links the vertex and fragment interfaces") {
    // The vertex stage renames its outputs, and the geometry stage forwards
    // them under the names the fragment and fog stages read.
    for (const std::string_view symbol : {"vertex_uv",
             "vertex_color",
             "vertex_view_position",
             "vertex_color_slope_next",
             "vertex_color_slope_previous"}) {
      CHECK(App::K_WORLD_VERTEX_SHADER.find(symbol) != std::string_view::npos);
      CHECK(App::K_WORLD_CUBIC_COLOR_GEOMETRY_SHADER.find(symbol) != std::string_view::npos);
    }
    for (const std::string_view symbol : {"out vec2 v_uv;",
             "out vec4 v_color;",
             "out vec3 v_view_position;",
             "out vec3 v_barycentric;",
             "flat out vec3 v_color_controls[10];"}) {
      CHECK(App::K_WORLD_CUBIC_COLOR_GEOMETRY_SHADER.find(symbol) != std::string_view::npos);
    }
    for (const std::string_view symbol :
        {"in vec3 v_barycentric;", "flat in vec3 v_color_controls[10];"}) {
      CHECK(App::K_MODERN_WORLD_FRAGMENT_BODY.find(symbol) != std::string_view::npos);
    }
    CHECK(App::K_WORLD_CUBIC_COLOR_GEOMETRY_SHADER.starts_with("\n#version 410 core\n"));
  }

  TEST_CASE("Every world program carries Runtime's environment-map stage") {
    // Spherical coordinates from the view-space normal (0x004947F0) and a
    // stage-1 modulate by the environment texture (0x00460FCB).
    for (const std::string_view symbol : {"uniform int u_environment_mapped;",
             "uniform vec2 u_environment_phase;",
             "out vec2 v_environment_uv;",
             "out vec2 vertex_environment_uv;",
             "asin(runtime_normal) * (2.0 / 3.14159265358979) + u_environment_phase * 2.5 + 0.5",
             "vec2(normal.x, -normal.y)"}) {
      CHECK(App::K_WORLD_VERTEX_SHADER.find(symbol) != std::string_view::npos);
    }
    for (const std::string_view symbol :
        {"in vec2 vertex_environment_uv[];", "out vec2 v_environment_uv;"}) {
      CHECK(App::K_WORLD_CUBIC_COLOR_GEOMETRY_SHADER.find(symbol) != std::string_view::npos);
    }
    for (const std::string_view symbol : {"in vec2 v_environment_uv;",
             "uniform sampler2D u_texture1;",
             "uniform int u_environment_mapped;",
             "color *= texture(u_texture1, v_environment_uv).rgb;"}) {
      CHECK(App::K_MODERN_WORLD_FRAGMENT_BODY.find(symbol) != std::string_view::npos);
    }
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

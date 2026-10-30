#include "Core/WorldShaders.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <string>
#include <string_view>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

namespace {

constexpr std::string_view K_VERSION{"#version 410 core\n"};
constexpr std::string_view K_PER_PIXEL_DEFINE{"#define PER_PIXEL_LIGHTING\n"};

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
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

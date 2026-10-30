#include "Core/WorldLighting.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/WorldFog.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

namespace {

/// A downward light (Runtime +Y is down) at the origin.
App::Omikron::Light make_light() {
  App::Omikron::Light light;
  light.attenuation_end = 100.0F;
  light.attenuation_start = 50.0F;
  light.intensity = 1.25F;
  light.color_bgra = {0x00, 0x80, 0xFF, 0x00};
  light.points.at(0) = {.x = 0.0F, .y = 0.0F, .z = 0.0F};
  light.points.at(1) = {.x = 0.0F, .y = 10.0F, .z = 0.0F};
  light.points.at(2) = {.x = -40.0F, .y = 20.0F, .z = -40.0F};
  light.points.at(3) = {.x = 40.0F, .y = 20.0F, .z = -40.0F};
  light.points.at(4) = {.x = 40.0F, .y = 20.0F, .z = 40.0F};
  light.points.at(5) = {.x = -40.0F, .y = 20.0F, .z = 40.0F};
  return light;
}

}  // namespace

TEST_SUITE("Core::WorldLighting") {
  TEST_CASE("Base ambient replicates the truncated base-light byte") {
    // ANEKBAH 0.1 and AIMPASSE 0.22 decors.
    CHECK_EQ(App::Lighting::base_ambient(0.1F).floor, 0x191919U);
    CHECK_EQ(App::Lighting::base_ambient(0.22F).floor, 0x383838U);
    CHECK_EQ(App::Lighting::base_ambient(0.22F).ceiling, 0xFFFFFFU);
  }

  TEST_CASE("Day light follows Runtime's four-phase ramp") {
    constexpr std::int64_t k_phase{App::Fog::K_DAY_PHASE_LENGTH};
    CHECK_EQ(App::Lighting::day_light_level(0), 64U);
    CHECK_EQ(App::Lighting::day_light_level(k_phase), 128U);
    CHECK_EQ(App::Lighting::day_light_level(k_phase + (k_phase / 2)), 96U);
    CHECK_EQ(App::Lighting::day_light_level(2 * k_phase), 64U);
    CHECK_EQ(App::Lighting::day_light_level(3 * k_phase), 0U);
    CHECK_EQ(App::Lighting::day_light_level(App::Fog::K_DAY_LENGTH - 1), 63U);

    // New game: 200000 * 64 / 900000 = 14 into the darkening phase.
    const App::Lighting::Ambient ambient{App::Lighting::day_ambient(App::Fog::K_NEW_GAME_CLOCK)};
    CHECK_EQ(ambient.floor, 0x323232U);
    CHECK_EQ(ambient.ceiling, 0xB1B1B1U);
  }

  TEST_CASE("Only the day-cycle owner follows the clock") {
    const App::Fog::Environment owner{.day_cycle_owner = true};
    const App::Fog::Environment interior{.day_cycle_owner = false};
    CHECK_EQ(App::Lighting::scene_ambient(owner, 0.3F, 0).floor, 0x404040U);
    CHECK_EQ(App::Lighting::scene_ambient(interior, 0.3F, 0).floor, 0x4C4C4CU);
  }

  TEST_CASE("Baked colours are clamped as one packed integer") {
    const App::Lighting::Ambient ambient{.floor = 0x323232U, .ceiling = 0xB1B1B1U};
    CHECK_EQ(App::Lighting::clamp_baked(0x00FF00U, ambient), 0x323232U);
    CHECK_EQ(App::Lighting::clamp_baked(0x400000U, ambient), 0x400000U);
    // Compared lexicographically: a red byte inside the range keeps bright G/B.
    CHECK_EQ(App::Lighting::clamp_baked(0xB0FFFFU, ambient), 0xB0FFFFU);
    CHECK_EQ(App::Lighting::clamp_baked(0xB20000U, ambient), 0xB1B1B1U);
  }

  TEST_CASE("Light preparation matches Runtime's load-time derivation") {
    const App::Lighting::LightSource light{App::Lighting::prepare(make_light())};
    CHECK(light.direction.y == doctest::Approx(1.0F));
    CHECK_EQ(light.color_rgb, 0xFF8000U);
    CHECK(light.box_min.x == doctest::Approx(-40.0F));
    CHECK(light.box_max.y == doctest::Approx(20.0F));

    CHECK(App::Lighting::touches(light, {.x = 0.0F, .y = 60.0F, .z = 0.0F}, 50.0F));
    CHECK_FALSE(App::Lighting::touches(light, {.x = 0.0F, .y = 80.0F, .z = 0.0F}, 50.0F));
    CHECK_FALSE(App::Lighting::touches(light, {.x = 200.0F, .y = 0.0F, .z = 0.0F}, 50.0F));
  }

  TEST_CASE("Node scale is full inside the inner radius and linear to the outer radius") {
    const App::Lighting::LightSource light{App::Lighting::prepare(make_light())};
    const std::optional<float> inside{App::Lighting::node_scale(light, {.y = 30.0F})};
    REQUIRE(inside.has_value());
    CHECK(inside.value_or(-1.0F) == doctest::Approx(320.0F));
    const std::optional<float> falloff{App::Lighting::node_scale(light, {.y = 75.0F})};
    REQUIRE(falloff.has_value());
    CHECK(falloff.value_or(-1.0F) == doctest::Approx(160.0F));
    CHECK_FALSE(App::Lighting::node_scale(light, {.y = 101.0F}).has_value());
  }

  TEST_CASE("Node lights pair the actor box and the node radius in scene order") {
    const App::Lighting::LightSource light{App::Lighting::prepare(make_light())};
    App::Lighting::LightSource far_light{light};
    far_light.box_min.x += 1000.0F;
    far_light.box_max.x += 1000.0F;
    const std::vector<App::Lighting::LightSource> lights{light, far_light, light};

    const App::Lighting::NodeLightSet selected{App::Lighting::select_node_lights(
        lights, {.x = 0.0F, .y = 60.0F, .z = 0.0F}, 50.0F, {.y = 75.0F})};
    REQUIRE_EQ(selected.count, 2U);
    CHECK_EQ(selected.active()[0].light_index, 0U);
    CHECK_EQ(selected.active()[1].light_index, 2U);
    CHECK(selected.active()[0].scale == doctest::Approx(160.0F));

    // The actor box touches the light, but the node is outside its outer radius.
    CHECK_EQ(App::Lighting::select_node_lights(
                 lights, {.x = 0.0F, .y = 60.0F, .z = 0.0F}, 50.0F, {.y = 101.0F})
                 .count,
        0U);
  }

  TEST_CASE("Node lights stop at the shader's light capacity") {
    const std::vector<App::Lighting::LightSource> lights(
        App::Lighting::K_MAX_NODE_LIGHTS + 4U, App::Lighting::prepare(make_light()));
    const App::Lighting::NodeLightSet selected{
        App::Lighting::select_node_lights(lights, {.y = 30.0F}, 20.0F, {.y = 30.0F})};
    CHECK_EQ(selected.count, App::Lighting::K_MAX_NODE_LIGHTS);
    CHECK_EQ(selected.active().back().light_index, App::Lighting::K_MAX_NODE_LIGHTS - 1U);
  }

  TEST_CASE("Light contributions saturate each byte") {
    // Up-facing normal (0,-1,0) against the downward light at full scale 320:
    // k = 320; R += 320 * 255 >> 8 (saturates), G += 320 * 128 >> 8 = 160.
    CHECK_EQ(App::Lighting::accumulate(0x323232U, 320.0F, 0xFF8000U), 0xFFD232U);
    // Back-facing or grazing normals add nothing.
    CHECK_EQ(App::Lighting::accumulate(0x323232U, -10.0F, 0xFF8000U), 0x323232U);
    CHECK_EQ(App::Lighting::accumulate(0x323232U, 0.0F, 0xFF8000U), 0x323232U);
    // _ftol truncates: 1.9 contributes k = 1, so 1 * 255 >> 8 = 0.
    CHECK_EQ(App::Lighting::accumulate(0x000000U, 1.9F, 0xFFFFFFU), 0x000000U);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

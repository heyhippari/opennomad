#include <doctest/doctest.h>

// std::span has no checked at(); every indexed access below follows an
// explicit size assertion or a fixture invariant.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Omikron/SFX.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Sfx/SfxRuntime.hpp"
#include "Core/Sprite/SpriteInstance.hpp"
#include "Core/Sprite/SpritePool.hpp"
#include "Core/Sprite/SpriteRenderMode.hpp"

namespace {

class FakeHost final : public App::Sfx::Host {
 public:
  FakeHost() {
    sprite_ids.emplace(9U, 90U);
    sprite_ids.emplace(10U, 100U);
  }

  [[nodiscard]] std::expected<std::size_t, std::string> resolve_sfx_sprite_id(
      const std::uint16_t authored_sprite_id) const override {
    const auto found{sprite_ids.find(authored_sprite_id)};
    if (found == sprite_ids.end()) {
      return std::expected<std::size_t, std::string>{std::unexpect, "missing authored sprite ID"};
    }
    return found->second;
  }

  [[nodiscard]] std::expected<App::Sfx::SpawnedSprite, std::string> spawn_sfx_sprite(
      const std::size_t resource_index, const App::Runtime::Vec3 position) override {
    auto handle{pool.create(resource_index, 0U, frame_count, {position.x, position.y, position.z})};
    if (!handle) {
      return std::expected<App::Sfx::SpawnedSprite, std::string>{
          std::unexpect, std::move(handle).error()};
    }
    if (auto attached{pool.attach(handle.value())}; !attached) {
      return std::expected<App::Sfx::SpawnedSprite, std::string>{
          std::unexpect, std::move(attached).error()};
    }
    spawned.push_back(handle.value());
    return App::Sfx::SpawnedSprite{.handle = handle.value(), .frame_count = frame_count};
  }

  [[nodiscard]] App::Sprite::SpriteInstance* find_sfx_sprite(
      const App::Sprite::SpriteHandle handle) override {
    return pool.find(handle);
  }

  void destroy_sfx_sprite(const App::Sprite::SpriteHandle handle) override {
    if (pool.find(handle) != nullptr) {
      REQUIRE(pool.destroy(handle).has_value());
      ++destroyed_count;
    }
  }

  [[nodiscard]] std::optional<App::Runtime::Transform> resolve_sfx_character_anchor(
      const std::int32_t packed_reference_id) const override {
    const auto found{character_anchors.find(packed_reference_id)};
    return found == character_anchors.end() ? std::nullopt
                                            : std::optional<App::Runtime::Transform>{found->second};
  }

  [[nodiscard]] App::Sprite::SpriteInstance* last_sprite() {
    return spawned.empty() ? nullptr : pool.find(spawned.back());
  }

  [[nodiscard]] App::Sprite::SpriteInstance* first_sprite() {
    return spawned.empty() ? nullptr : pool.find(spawned.front());
  }

  std::unordered_map<std::uint16_t, std::size_t> sprite_ids;
  std::unordered_map<std::int32_t, App::Runtime::Transform> character_anchors;
  App::Sprite::SpritePool pool;
  std::vector<App::Sprite::SpriteHandle> spawned;
  std::size_t frame_count{5U};
  std::size_t destroyed_count{0U};
};

App::Omikron::SfxDefinition make_definition(
    const std::int32_t id = 1, const std::uint16_t sprite_id = 9U) {
  return App::Omikron::SfxDefinition{.definition_id = id,
      .sound_id = 0x0000FFFF,
      .sprite_id_raw = sprite_id,
      .flags = 0U,
      .direction = {},
      .vertical_acceleration = 0.0F,
      .lifetime = 10.0F,
      .sound_delay = 0.0F,
      .emission_delay = 0.0F,
      .raw_2c = 0.0F,
      .start_color_rgb = 0x00FFFFFFU,
      .end_color_rgb = 0x00FFFFFFU,
      .initial_scale = 1.0F,
      .cone_angle_degrees = 0.0F,
      .angular_velocity_degrees = 0.0F,
      .spawn_count = 1,
      .name = "test",
      .sprite_render_mode = 4U,
      .raw_4f = 0U};
}

App::Omikron::SfxTrackPoint make_point(const std::int32_t id,
    const std::int32_t definition_id,
    const float x_coordinate,
    const float duration,
    const std::int32_t reference_type = 0,
    const std::int32_t reference_id = 0) {
  return App::Omikron::SfxTrackPoint{.point_id = id,
      .definition_id = definition_id,
      .position = {.x = x_coordinate, .y = 0.0F, .z = 0.0F},
      .segment_duration = duration,
      .reference_type = reference_type,
      .reference_id = reference_id,
      .serialized_reference_ptr = 0U};
}

App::Omikron::SfxTrack make_track(
    const std::int32_t id, std::vector<App::Omikron::SfxTrackPoint> points) {
  return App::Omikron::SfxTrack{.track_id = id,
      .label = "trk",
      .point_count = static_cast<std::uint32_t>(points.size()),
      .mutable_duration_seed = 0.0F,
      .points = std::move(points)};
}

App::Omikron::SfxNode make_node(const std::int32_t id,
    const std::int32_t track_id,
    const std::int32_t fixed_definition_id = 1) {
  return App::Omikron::SfxNode{.node_id = id,
      .label = "node",
      .trigger_type = 1,
      .trigger_id = -1,
      .track_id = track_id,
      .serialized_track_ptr = 0U,
      .serialized_point_ptr = 0U,
      .serialized_runtime_position = {},
      .anchor_reference_type = 0,
      .anchor_reference_id = 0,
      .serialized_anchor_ptr = 0U,
      .fixed_definition_id = fixed_definition_id,
      .startup_delay = 0.0F,
      .serialized_elapsed = 123.0F,
      .repeat_limit = 1,
      .serialized_repeat_index = 456,
      .flags = 0U};
}

App::Omikron::SfxData basic_data() {
  App::Omikron::SfxData data;
  data.magic = App::Omikron::k_sfx_magic;
  data.definitions.push_back(make_definition());
  data.tracks.push_back(make_track(1, {make_point(0, 1, 0.0F, 10.0F)}));
  data.nodes.push_back(make_node(1, 1));
  return data;
}

std::unique_ptr<App::Sfx::Runtime> create_runtime(
    App::Omikron::SfxData& data, FakeHost& host, App::Sfx::Runtime::Random01 random = {}) {
  auto created{App::Sfx::Runtime::create(data, host, std::move(random))};
  REQUIRE(created.has_value());
  return std::move(created).value();
}

}  // namespace

TEST_SUITE("Core::Sfx::Runtime") {
  TEST_CASE("automatic and explicit triggers use exact type and ID matches") {
    FakeHost host;
    auto data{basic_data()};
    data.nodes.front().fixed_definition_id = 0;
    data.nodes.front().trigger_type = 1;
    data.nodes.front().trigger_id = -1;
    for (const std::int32_t trigger_id : {1, 8, 20, 20, 20, 20, 20}) {
      App::Omikron::SfxNode value{
          make_node(static_cast<std::int32_t>(data.nodes.size() + 1U), 1, 0)};
      value.trigger_type = 0;
      value.trigger_id = trigger_id;
      data.nodes.push_back(value);
    }
    auto runtime{create_runtime(data, host)};
    CHECK(runtime->diagnostics().active_node_count == 1U);
    CHECK(runtime->trigger(0, 1) == 1U);
    CHECK(runtime->trigger(0, 8) == 1U);
    CHECK(runtime->trigger(0, 20) == 5U);
    CHECK(runtime->trigger(1, 20) == 0U);
    CHECK(runtime->diagnostics().active_node_count == 8U);
  }

  TEST_CASE("startup delay suppresses emission and fixed stepping ignores render frequency") {
    FakeHost host;
    auto data{basic_data()};
    data.nodes.front().startup_delay = 2.0F;
    auto runtime{create_runtime(data, host)};
    runtime->tick(App::Sfx::Runtime::k_fixed_step_seconds * 0.5F);
    CHECK(host.pool.live_count() == 0U);
    runtime->tick(App::Sfx::Runtime::k_fixed_step_seconds * 0.5F);
    CHECK(host.pool.live_count() == 0U);
    runtime->step();
    CHECK(host.pool.live_count() == 0U);
    runtime->step();
    CHECK(host.pool.live_count() == 1U);
  }

  TEST_CASE("finite repetition terminates while sentinel 999 stays active at repeat index one") {
    FakeHost host;
    auto data{basic_data()};
    data.tracks.front().points.front().segment_duration = 1.0F;
    data.nodes.front().repeat_limit = 1;
    auto runtime{create_runtime(data, host)};
    runtime->step();
    CHECK_FALSE(runtime->nodes().front().active());

    FakeHost infinite_host;
    auto infinite_data{basic_data()};
    infinite_data.tracks.front().points.front().segment_duration = 1.0F;
    infinite_data.nodes.front().repeat_limit = 999;
    auto infinite{create_runtime(infinite_data, infinite_host)};
    for (std::size_t index{0}; index < 1200U; ++index) {
      infinite->step();
    }
    CHECK(infinite->nodes().front().active());
    CHECK(infinite->nodes().front().repeat_index == 1);
  }

  TEST_CASE("activation derives current reverse from authored bit 04 and ping-pong toggles it") {
    FakeHost residue_host;
    auto residue_data{basic_data()};
    residue_data.nodes.front().flags = 0x08U;
    auto residue{create_runtime(residue_data, residue_host)};
    CHECK_FALSE(residue->nodes().front().current_reverse());

    FakeHost reverse_host;
    auto reverse_data{basic_data()};
    reverse_data.nodes.front().flags = 0x04U;
    auto reverse{create_runtime(reverse_data, reverse_host)};
    CHECK(reverse->nodes().front().current_reverse());

    FakeHost ping_host;
    auto ping_data{basic_data()};
    ping_data.tracks.front().points.front().segment_duration = 1.0F;
    ping_data.nodes.front().flags = 0x10U;
    ping_data.nodes.front().repeat_limit = 2;
    auto ping{create_runtime(ping_data, ping_host)};
    ping->step();
    CHECK(ping->nodes().front().current_reverse());
    CHECK(ping->nodes().front().repeat_index == 2);
  }

  TEST_CASE("tracks interpolate linearly and choose dynamic definitions by traversal direction") {
    FakeHost forward_host;
    auto forward_data{basic_data()};
    forward_data.definitions.push_back(make_definition(2, 10U));
    forward_data.tracks.front() =
        make_track(1, {make_point(0, 1, 0.0F, 10.0F), make_point(1, 2, 10.0F, 0.0F)});
    forward_data.nodes.front().fixed_definition_id = 0;
    auto forward{create_runtime(forward_data, forward_host)};
    forward->step();
    forward->step();
    CHECK(forward->nodes().front().current_position.x == doctest::Approx(1.0F));
    REQUIRE(forward_host.last_sprite() != nullptr);
    CHECK(forward_host.last_sprite()->resource_index == 90U);

    FakeHost reverse_host;
    auto reverse_data{basic_data()};
    reverse_data.definitions.push_back(make_definition(2, 10U));
    reverse_data.tracks.front() = make_track(1,
        {make_point(0, 1, 0.0F, 10.0F),
            make_point(1, 2, 10.0F, 10.0F),
            make_point(2, 1, 20.0F, 0.0F)});
    reverse_data.nodes.front().fixed_definition_id = 0;
    reverse_data.nodes.front().flags = 0x04U;
    auto reverse{create_runtime(reverse_data, reverse_host)};
    reverse->step();
    reverse->step();
    CHECK(reverse->nodes().front().current_position.x == doctest::Approx(19.0F));
    CHECK(reverse->nodes().front().current_point_index == 1U);
    REQUIRE(reverse_host.last_sprite() != nullptr);
    CHECK(reverse_host.last_sprite()->resource_index == 100U);
  }

  TEST_CASE("reference types and node-over-point precedence use live anchors") {
    FakeHost host;
    host.character_anchors.emplace(0x00484F31,
        App::Runtime::Transform{.matrix = {},
            .translation = {.x = 20.0F, .y = 0.0F, .z = 0.0F},
            .scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F}});
    auto data{basic_data()};
    data.nodes.clear();
    data.tracks.clear();
    data.tracks.push_back(make_track(0, {make_point(0, 0, 10.0F, 5.0F)}));
    data.tracks.push_back(make_track(1, {make_point(0, 0, 1.0F, 5.0F, 1, 0)}));
    data.tracks.push_back(make_track(2, {make_point(0, 0, 2.0F, 5.0F, 3, 0)}));
    data.tracks.push_back(make_track(3, {make_point(0, 0, 4.0F, 5.0F, 3, 0)}));
    data.tracks.push_back(make_track(4, {make_point(0, 0, 5.0F, 5.0F, 3, 0)}));
    for (std::int32_t index{0}; index < 5; ++index) {
      data.nodes.push_back(make_node(index, index, 0));
    }
    data.nodes.at(3).anchor_reference_type = 1;
    data.nodes.at(3).anchor_reference_id = 1;
    data.nodes.at(4).anchor_reference_type = 2;
    data.nodes.at(4).anchor_reference_id = 0x00484F31;
    auto runtime{create_runtime(data, host)};
    REQUIRE(runtime->nodes().size() == 5U);
    CHECK(runtime->nodes()[0].current_position.x == doctest::Approx(10.0F));
    CHECK(runtime->nodes()[1].current_position.x == doctest::Approx(11.0F));
    CHECK(runtime->nodes()[2].current_position.x == doctest::Approx(2.0F));
    CHECK(runtime->nodes()[3].current_position.x == doctest::Approx(15.0F));
    CHECK(runtime->nodes()[4].current_position.x == doctest::Approx(25.0F));
  }

  TEST_CASE(
      "particles preserve sprite appearance and advance retail motion, color, frame, and "
      "lifetime") {
    FakeHost host;
    auto data{basic_data()};
    App::Omikron::SfxDefinition& definition{data.definitions.front()};
    definition.flags = 0x0004U | 0x0200U;
    definition.vertical_acceleration = 1.0F;
    definition.lifetime = 4.0F;
    definition.initial_scale = 2.0F;
    definition.start_color_rgb = 0x00000000U;
    definition.end_color_rgb = 0x00FFFFFFU;
    definition.sprite_render_mode = 6U;
    auto runtime{create_runtime(data, host)};
    runtime->step();
    App::Sprite::SpriteInstance* sprite{host.last_sprite()};
    REQUIRE(sprite != nullptr);
    CHECK(sprite->resource_index == 90U);
    CHECK(sprite->render_mode == App::Sprite::SpriteRenderMode::k_darken);
    CHECK(sprite->type == 6U);
    CHECK(sprite->unknown_24 == doctest::Approx(0.5F));
    CHECK(sprite->scale_x == doctest::Approx(2.5F));
    CHECK(sprite->scale_y == doctest::Approx(2.5F));
    CHECK(sprite->tint.at(0) == doctest::Approx(0.25F));
    CHECK(sprite->frame_index == 0U);
    runtime->step();
    REQUIRE(host.first_sprite() != nullptr);
    CHECK(host.first_sprite()->position.at(1) == doctest::Approx(1.0F));
    CHECK(host.first_sprite()->frame_index == 1U);
    runtime->step();
    runtime->step();
    CHECK(host.pool.live_count() == 4U);
    runtime->step();
    CHECK(host.destroyed_count >= 1U);
  }

  TEST_CASE(
      "deterministic RNG drives random rotation, direction perturbation, and cone randomization") {
    FakeHost rotation_host;
    auto rotation_data{basic_data()};
    rotation_data.definitions.front().flags = 0x0010U;
    auto rotation{create_runtime(rotation_data, rotation_host, [] {
      return 0.25F;
    })};
    rotation->step();
    REQUIRE(rotation_host.last_sprite() != nullptr);
    CHECK(
        rotation_host.last_sprite()->rotation == doctest::Approx(std::numbers::pi_v<float> / 2.0F));

    std::vector<float> perturb_values{0.5F, 0.0F, 0.0F, 0.0F};
    std::size_t perturb_index{0U};
    FakeHost perturb_host;
    auto perturb_data{basic_data()};
    perturb_data.definitions.front().flags = 0x0040U;
    perturb_data.definitions.front().direction = {.x = 1.0F, .y = 0.0F, .z = 0.0F};
    auto perturb{create_runtime(perturb_data, perturb_host, [&] {
      return perturb_values.at(perturb_index++);
    })};
    perturb->step();
    REQUIRE(perturb_host.last_sprite() != nullptr);
    CHECK(perturb_host.last_sprite()->position.at(0) == doctest::Approx(2.0F));

    std::vector<float> cone_values{0.5F, 0.0F};
    std::size_t cone_index{0U};
    FakeHost cone_host;
    auto cone_data{basic_data()};
    cone_data.definitions.front().flags = 0x1000U;
    cone_data.definitions.front().direction = {.x = 1.0F, .y = 0.0F, .z = 0.0F};
    cone_data.definitions.front().cone_angle_degrees = 10.0F;
    auto cone{create_runtime(cone_data, cone_host, [&] {
      return cone_values.at(cone_index++);
    })};
    cone->step();
    REQUIRE(cone_host.last_sprite() != nullptr);
    CHECK(cone_host.last_sprite()->position.at(0) ==
          doctest::Approx(std::cos(15.0F * std::numbers::pi_v<float> / 180.0F)));
    CHECK(cone_host.last_sprite()->position.at(2) ==
          doctest::Approx(-std::sin(15.0F * std::numbers::pi_v<float> / 180.0F)));
  }

  TEST_CASE("request and particle pools enforce retail capacities") {
    FakeHost request_host;
    auto request_data{basic_data()};
    request_data.definitions.front().emission_delay = 200.0F;
    request_data.nodes.clear();
    for (std::size_t index{0}; index < 101U; ++index) {
      request_data.nodes.push_back(make_node(static_cast<std::int32_t>(index), 1));
    }
    auto requests{create_runtime(request_data, request_host)};
    requests->step();
    CHECK(requests->diagnostics().queued_request_count == App::Sfx::Runtime::k_request_capacity);

    FakeHost particle_host;
    auto particle_data{basic_data()};
    particle_data.definitions.front().spawn_count = 1001;
    auto particles{create_runtime(particle_data, particle_host)};
    particles->step();
    CHECK(particles->diagnostics().active_particle_count == App::Sfx::Runtime::k_particle_capacity);
    CHECK(particle_host.pool.attached_count() == App::Sfx::Runtime::k_particle_capacity);
  }

  TEST_CASE("teardown destroys every remaining SFX-owned SpriteHandle") {
    FakeHost host;
    auto data{basic_data()};
    data.definitions.front().spawn_count = 3;
    auto runtime{create_runtime(data, host)};
    runtime->step();
    REQUIRE(host.pool.live_count() == 3U);
    runtime.reset();
    CHECK(host.pool.live_count() == 0U);
    CHECK(host.pool.attached_count() == 0U);
    CHECK(host.destroyed_count == 3U);
  }
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Crowd/CrowdRuntime.hpp"
#include "Core/Omikron/AnimationBankAni.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/TrajectoryOpt.hpp"
#include "Core/RuntimeMath.hpp"
#include "OmikronTestBuffer.hpp"

namespace {

constexpr float K_SEGMENT_LENGTH{400.0F};

/// Two trajectories: 0 runs +X over three segments and routes to 1, which
/// runs -X over two segments and routes back to 0. No junction steps.
/// Trajectories at or after `pedestrian_end` are slider lanes.
std::vector<std::byte> make_opt(
    const std::int16_t route_target = 1, const std::int32_t pedestrian_end = 2) {
  constexpr std::uint32_t k_trajectory_offset{0x4C};
  constexpr std::uint32_t k_segment_offset{k_trajectory_offset + (2U * 0x18U)};
  constexpr std::uint32_t k_stop_offset{k_segment_offset + (5U * 0x14U)};
  constexpr std::uint32_t k_route_offset{k_stop_offset + 0x14U};
  constexpr std::uint32_t k_step_offset{k_route_offset + (2U * 0x0CU)};
  Buffer bytes;
  bytes.chars("V1.0", 4)
      .i32(0)  // pedestrian begin
      .i32(pedestrian_end)
      .i32(1)  // pedestrian spacing (metres at density 4)
      .i32(1)  // slider spacing
      .i32(2)  // trajectory count
      .u32(k_trajectory_offset)
      .u32(5)  // segments
      .u32(k_segment_offset)
      .u32(1)  // stop points
      .u32(k_stop_offset)
      .u32(2)  // routes
      .u32(k_route_offset)
      .u32(0)  // route steps
      .u32(k_step_offset)
      .u32(0)  // crossings
      .u32(k_step_offset)
      .u32(0)  // crossing conflicts
      .u32(k_step_offset);
  // Trajectory 0 at the origin, trajectory 1 at its far end.
  bytes.f32(0.0F).f32(0.0F).f32(0.0F).u32(0).u16(0).u16(0).u8(1).u8(3).u16(0);
  bytes.f32(3.0F * K_SEGMENT_LENGTH).f32(0.0F).f32(0.0F).u32(0).u16(3).u16(1).u8(1).u8(2).u16(0);
  for (int segment{0}; segment < 3; ++segment) {
    bytes.u32(0).f32(K_SEGMENT_LENGTH).f32(0.0F).f32(0.0F).u16(0xFFFFU).u16(0);
  }
  for (int segment{0}; segment < 2; ++segment) {
    bytes.u32(0).f32(-K_SEGMENT_LENGTH).f32(0.0F).f32(0.0F).u16(0xFFFFU).u16(0);
  }
  // A disabled stop point (animation 0).
  bytes.f32(0.0F).f32(0.0F).f32(10.0F).f32(90.0F).u16(0).u8(1).u8(0);
  // Route 0 -> trajectory 1, route 1 -> trajectory 0, no steps.
  bytes.u32(0).u16(static_cast<std::uint16_t>(route_target)).u16(0).u16(0xFFFFU).u8(0).u8(0);
  bytes.u32(0).u16(0).u16(0).u16(0xFFFFU).u8(0).u8(0);
  return bytes.data();
}

/// 3DA payload: one root channel walking 10 inches along -Z per frame.
void append_walk_3da(Buffer& bytes) {
  constexpr std::uint32_t k_frames{3};
  constexpr std::uint32_t k_translation_offset{8U + 0x28U};
  constexpr std::uint32_t k_rotation_offset{k_translation_offset + ((k_frames + 1U) * 12U)};
  bytes.u32(k_frames).u32(1);
  bytes.u32(0)
      .chars("Root", 20)
      .u32(k_frames + 1U)
      .u32(k_translation_offset)
      .u32(k_frames + 1U)
      .u32(k_rotation_offset);
  bytes.f32(0.0F).f32(0.0F).f32(0.0F);
  for (std::uint32_t frame{0}; frame < k_frames; ++frame) {
    bytes.f32(0.0F).f32(0.0F).f32(-10.0F);
  }
  for (std::uint32_t frame{0}; frame <= k_frames; ++frame) {
    bytes.f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
  }
}

/// ANI with an empty type-0 entry and a male passerby (type 1) whose walk and
/// blocked nodes share one embedded animation.
std::vector<std::byte> make_ani() {
  constexpr std::uint32_t k_node_offset{8U + (2U * 0x18U)};
  constexpr std::uint32_t k_second_node{k_node_offset + 0x24U};
  constexpr std::uint32_t k_animation_offset{k_second_node + 0x24U};
  Buffer bytes;
  bytes.chars("3.0V", 4).u32(2);
  bytes.i32(0).u32(0).zeros(0x10);
  bytes.i32(1).u32(k_node_offset).zeros(0x10);
  bytes.i32(9)
      .i32(18)
      .u32(k_animation_offset)
      .f32(1.0F)
      .f32(2.0F)
      .u16(5)
      .u16(6)
      .u32(k_second_node)
      .chars("NULL", 8);
  bytes.i32(11)
      .i32(16)
      .u32(k_animation_offset)
      .f32(0.0F)
      .f32(0.0F)
      .u16(0xFFFFU)
      .u16(0xFFFFU)
      .u32(0)
      .chars("NULL", 8);
  append_walk_3da(bytes);
  return bytes.data();
}

/// A passerby-style model with four parentless single-object levels whose
/// own polygon counts are 1, 3, 2 and 4 in serialized order.
std::shared_ptr<const App::Character::ModelResource> make_passerby_model() {
  auto resource{std::make_shared<App::Character::ModelResource>()};
  resource->name = "TEST_FN";
  App::Omikron::Model3DOData& model{resource->model};
  model.materials.emplace_back();
  constexpr std::array<std::uint32_t, 4> k_triangles{1U, 3U, 2U, 4U};
  for (std::size_t index{0}; index < k_triangles.size(); ++index) {
    App::Omikron::MeshDescriptor mesh;
    mesh.name = "Level" + std::to_string(index);
    mesh.script_id = static_cast<std::uint32_t>(index);
    mesh.vertex_count = 3U;
    mesh.vertex_base = index * 3U;
    mesh.triangle_count = k_triangles.at(index);
    mesh.bounding_radius = 40.0F;
    model.meshes.push_back(mesh);
    App::Omikron::MeshPolygons polygons;
    for (std::uint32_t triangle_index{0}; triangle_index < mesh.triangle_count; ++triangle_index) {
      App::Omikron::Triangle triangle;
      triangle.material_id = 0;
      triangle.vertices.at(0).index = 0U;
      triangle.vertices.at(1).index = 1U;
      triangle.vertices.at(2).index = 2U;
      polygons.triangles.push_back(triangle);
    }
    model.polygons.push_back(std::move(polygons));
    for (int corner{0}; corner < 3; ++corner) {
      model.vertices.push_back(App::Omikron::RawVertex{
          .position = {.x = static_cast<float>(corner), .y = 0.0F, .z = 0.0F},
          .normal = {.x = 0.0F, .y = 1.0F, .z = 0.0F}});
    }
    model.hierarchy_parent_index.push_back(-1);
    model.hierarchy_first_child_index.push_back(-1);
    model.hierarchy_next_sibling_index.push_back(
        index + 1U < k_triangles.size() ? static_cast<std::int32_t>(index + 1U) : -1);
    model.hierarchy_reachable.push_back(1U);
    model.skin_parent_index.push_back(-1);
    model.runtime_objects.emplace_back();
  }
  model.root_mesh_index = 0;
  REQUIRE(App::Omikron::Model3DO::resolve_runtime_transforms(model).has_value());
  return resource;
}

App::Crowd::PopulationRequest make_request(const std::uint8_t street_activity) {
  auto network{App::Omikron::TrajectoryOpt::load(make_opt())};
  REQUIRE(network.has_value());
  auto animations{App::Omikron::AnimationBankAni::load(make_ani())};
  REQUIRE(animations.has_value());
  return App::Crowd::PopulationRequest{
      .network = std::make_shared<const App::Omikron::TrajectoryOpt>(std::move(network).value()),
      .animations =
          std::make_shared<const App::Omikron::AnimationBankAni>(std::move(animations).value()),
      .male_models = {make_passerby_model()},
      .female_models = {},
      .slider_models = {},
      .moto_models = {},
      .street_activity = street_activity};
}

/// Both trajectories as slider lanes, two `sli_fn` variants and one `moto`.
App::Crowd::PopulationRequest make_slider_request() {
  auto network{App::Omikron::TrajectoryOpt::load(make_opt(1, 0))};
  REQUIRE(network.has_value());
  auto animations{App::Omikron::AnimationBankAni::load(make_ani())};
  REQUIRE(animations.has_value());
  return App::Crowd::PopulationRequest{
      .network = std::make_shared<const App::Omikron::TrajectoryOpt>(std::move(network).value()),
      .animations =
          std::make_shared<const App::Omikron::AnimationBankAni>(std::move(animations).value()),
      .male_models = {make_passerby_model()},
      .female_models = {},
      .slider_models = {make_passerby_model(), make_passerby_model()},
      .moto_models = {make_passerby_model()},
      .street_activity = App::Crowd::K_DEFAULT_STREET_ACTIVITY};
}

}  // namespace

TEST_CASE("TrajectoryOpt decodes the street network tables") {
  const auto network{App::Omikron::TrajectoryOpt::load(make_opt())};
  REQUIRE(network.has_value());
  CHECK(network->pedestrian_begin == 0);
  CHECK(network->pedestrian_end == 2);
  CHECK(network->pedestrian_spacing == 1);
  REQUIRE(network->trajectories.size() == 2U);
  CHECK(network->trajectories.at(1).start.x == doctest::Approx(1200.0F));
  CHECK(network->trajectories.at(1).first_segment == 3);
  CHECK(network->trajectories.at(1).first_route == 1);
  CHECK(network->trajectories.at(0).segment_count == 3U);
  REQUIRE(network->segments.size() == 5U);
  CHECK(network->segments.at(4).vector.x == doctest::Approx(-K_SEGMENT_LENGTH));
  CHECK(network->segments.at(0).stop_point == -1);
  REQUIRE(network->stop_points.size() == 1U);
  CHECK(network->stop_points.at(0).yaw_degrees == doctest::Approx(90.0F));
  CHECK(network->stop_points.at(0).loop_count == 1);
  REQUIRE(network->routes.size() == 2U);
  CHECK(network->routes.at(0).next_trajectory == 1);
  CHECK(network->routes.at(0).entry_crossing == -1);
}

TEST_CASE("TrajectoryOpt rejects a bad signature and dangling routes") {
  std::vector<std::byte> bytes{make_opt()};
  bytes.at(0) = std::byte{'X'};
  CHECK_FALSE(App::Omikron::TrajectoryOpt::load(bytes).has_value());
  CHECK_FALSE(App::Omikron::TrajectoryOpt::load(make_opt(7)).has_value());
}

TEST_CASE("AnimationBankAni resolves node lists and shares embedded animations") {
  const auto bank{App::Omikron::AnimationBankAni::load(make_ani())};
  REQUIRE(bank.has_value());
  REQUIRE(bank->characters.size() == 2U);
  CHECK(bank->characters.at(0).nodes.empty());
  REQUIRE(bank->animations.size() == 1U);
  CHECK(bank->animations.at(0).max_frame_index == 3U);
  const App::Omikron::AniCharacter* const male{bank->character(1)};
  REQUIRE(male != nullptr);
  REQUIRE(male->nodes.size() == 2U);
  CHECK(male->nodes.at(0).kind == 9);
  CHECK(male->nodes.at(0).footstep_progress.at(1) == doctest::Approx(2.0F));
  CHECK(male->nodes.at(0).footstep_sound.at(0) == 5);
  CHECK(male->nodes.at(1).animation_index == male->nodes.at(0).animation_index);
  CHECK(
      App::Omikron::ani_nodes_of_kind(*male, App::Omikron::AniAnimationKind::k_walk).size() == 1U);
  CHECK(App::Omikron::ani_node_by_id(*male, 16) == &male->nodes.at(1));
  CHECK(App::Omikron::ani_node_by_id(*male, 0) == nullptr);
  CHECK(bank->character(2) == nullptr);
}

TEST_CASE("Level of detail follows Runtime's depth chain and detail bias") {
  const std::span<const float> passer{App::Crowd::K_PASSER_LOD_DEPTHS};
  // Default detail starts at the most detailed level.
  CHECK(App::Crowd::select_level_of_detail(4U, 0U, passer, 1U, 100.0F) == 0U);
  CHECK(App::Crowd::select_level_of_detail(4U, 0U, passer, 1U, 393.700775F) == 1U);
  CHECK(App::Crowd::select_level_of_detail(4U, 0U, passer, 1U, 1000.0F) == 2U);
  // The last level is kept at any depth.
  CHECK(App::Crowd::select_level_of_detail(4U, 0U, passer, 1U, 1.0e6F) == 3U);
  // Low detail skips the first level; high detail skips none.
  CHECK(App::Crowd::select_level_of_detail(4U, 0U, passer, 0U, 100.0F) == 1U);
  CHECK(App::Crowd::select_level_of_detail(4U, 0U, passer, 2U, 100.0F) == 0U);
  // A single-level model never switches.
  CHECK(App::Crowd::select_level_of_detail(1U, 0U, passer, 0U, 1.0e6F) == 0U);

  // The slider chain starts at level 1 and switches at 20/30 m.
  const std::span<const float> slider{App::Crowd::K_SLIDER_LOD_DEPTHS};
  CHECK(App::Crowd::select_level_of_detail(4U, 1U, slider, 1U, 500.0F) == 1U);
  CHECK(App::Crowd::select_level_of_detail(4U, 1U, slider, 1U, 800.0F) == 2U);
  CHECK(App::Crowd::select_level_of_detail(4U, 1U, slider, 1U, 1.0e6F) == 3U);
  CHECK(App::Crowd::select_level_of_detail(4U, 1U, slider, 0U, 0.0F) == 2U);
}

TEST_CASE("Model3DO subtree helpers follow Runtime instance order") {
  const auto resource{make_passerby_model()};
  App::Omikron::Model3DOData model{resource->model};
  // Make level 1 a child of level 0 and level 2 a child of level 1.
  model.hierarchy_parent_index = {-1, 0, 1, -1};
  model.hierarchy_first_child_index = {1, 2, -1, -1};
  model.hierarchy_next_sibling_index = {3, -1, -1, -1};
  CHECK(
      App::Omikron::Model3DO::subtree_preorder(model, 0U) == std::vector<std::size_t>{0U, 1U, 2U});
  CHECK(
      App::Omikron::Model3DO::subtree_mask(model, 1U) == std::vector<std::uint8_t>{0U, 1U, 1U, 0U});
  CHECK(App::Omikron::Model3DO::parentless_objects(model) == std::vector<std::size_t>{0U, 3U});
  CHECK(App::Omikron::Model3DO::subtree_preorder(model, 9U).empty());
}

TEST_CASE("IamAreaRecord exposes the passerby variant masks") {
  std::vector<std::byte> data(App::Omikron::IamAreaRecord::k_header_size, std::byte{});
  const std::uint32_t male{0x201U};
  const std::uint32_t female{0x41U};
  std::memcpy(data.data() + App::Omikron::IamAreaRecord::k_offset_male_passer_variants, &male, 4U);
  std::memcpy(
      data.data() + App::Omikron::IamAreaRecord::k_offset_female_passer_variants, &female, 4U);
  const auto area{App::Omikron::IamAreaRecord::load(data)};
  REQUIRE(area.has_value());
  CHECK(area->male_passer_variants() == male);
  CHECK(area->female_passer_variants() == female);

  const std::uint16_t sliders{0x3U};
  const std::uint16_t motos{0x1U};
  std::memcpy(data.data() + App::Omikron::IamAreaRecord::k_offset_slider_variants, &sliders, 2U);
  std::memcpy(data.data() + App::Omikron::IamAreaRecord::k_offset_moto_variants, &motos, 2U);
  const auto vehicles{App::Omikron::IamAreaRecord::load(data)};
  REQUIRE(vehicles.has_value());
  CHECK(vehicles->slider_variants() == sliders);
  CHECK(vehicles->moto_variants() == motos);
}

TEST_CASE("Crowd prototypes order levels of detail by descending polygon count") {
  App::Crowd::Runtime crowd;
  REQUIRE(crowd.populate(make_request(4U)).has_value());
  REQUIRE(crowd.prototypes().size() == 1U);
  const App::Crowd::Prototype& prototype{crowd.prototypes().front()};
  CHECK(prototype.level_roots == std::vector<std::size_t>{3U, 1U, 2U, 0U});
  CHECK(prototype.level_layouts.size() == 4U);
  CHECK(prototype.root_radius == doctest::Approx(40.0F));
  CHECK(prototype.remaining_share < 100);
}

TEST_CASE("Crowd spawns passersby at spaced segment starts") {
  App::Crowd::Runtime crowd;
  REQUIRE(crowd.populate(make_request(4U)).has_value());
  // Spacing 1 m x (5 - 4): a passerby at every segment start after the first.
  const std::span<const App::Crowd::Agent> pedestrians{crowd.agents()};
  REQUIRE(pedestrians.size() == 3U);
  CHECK(pedestrians[0].trajectory == 0U);
  CHECK(pedestrians[0].segment == 2);
  CHECK(pedestrians[0].target.x == doctest::Approx(K_SEGMENT_LENGTH));
  CHECK(pedestrians[0].position.x == doctest::Approx(K_SEGMENT_LENGTH - 117.0F));
  CHECK(pedestrians[1].segment == 3);
  CHECK(pedestrians[2].trajectory == 1U);
  CHECK(pedestrians[2].segment == 2);
  for (const App::Crowd::Agent& pedestrian : pedestrians) {
    CHECK(pedestrian.character_type == 1);
    CHECK((pedestrian.flags & App::Crowd::K_FLAG_ON_NETWORK) != 0U);
    CHECK(pedestrian.ground_offset == doctest::Approx(40.0F));
    CHECK(pedestrian.playback_rate >= 0.8F);
    CHECK(pedestrian.playback_rate <= 1.25F);
    CHECK(pedestrian.nominal_speed > 0.0F);
  }

  // Lower density spaces them further apart.
  App::Crowd::Runtime sparse;
  auto request{make_request(4U)};
  request.network = std::make_shared<const App::Omikron::TrajectoryOpt>([] {
    auto network{App::Omikron::TrajectoryOpt::load(make_opt())};
    REQUIRE(network.has_value());
    App::Omikron::TrajectoryOpt copy{std::move(network).value()};
    copy.pedestrian_spacing = 30;
    return copy;
  }());
  REQUIRE(sparse.populate(std::move(request)).has_value());
  CHECK(sparse.agents().empty());
}

TEST_CASE("Crowd passersby walk the network and follow routes onto other trajectories") {
  App::Crowd::Runtime crowd;
  REQUIRE(crowd.populate(make_request(4U)).has_value());
  const float start_x{crowd.agents()[0].position.x};

  for (int tick{0}; tick < 30; ++tick) {
    crowd.tick(1.0F);
  }
  const App::Crowd::Agent& walker{crowd.agents()[0]};
  CHECK(walker.position.x > start_x);
  // Facing turns the model's -Z axis toward +X.
  const App::Runtime::Vec3 forward{
      App::Runtime::transform_vector(App::Runtime::Vec3{.z = -1.0F}, walker.facing)};
  CHECK(forward.x == doctest::Approx(1.0F).epsilon(0.01));

  bool changed_trajectory{false};
  for (int tick{0}; tick < 2000 && !changed_trajectory; ++tick) {
    crowd.tick(1.0F);
    changed_trajectory = crowd.agents()[0].trajectory == 1U;
  }
  CHECK(changed_trajectory);
  for (const App::Crowd::Agent& pedestrian : crowd.agents()) {
    CHECK(std::isfinite(pedestrian.position.x));
    CHECK(std::isfinite(pedestrian.instance.y));
  }

  crowd.clear();
  CHECK(crowd.empty());
  CHECK(crowd.prototypes().empty());
}

TEST_CASE("Crowd spawns sliders on slider lanes with a boardable first slot") {
  App::Crowd::Runtime crowd;
  REQUIRE(crowd.populate(make_slider_request()).has_value());
  // No passerby trajectories: only vehicle prototypes, chained from level 1
  // for `sli_fn` and level 0 for `moto`.
  REQUIRE(crowd.prototypes().size() == 3U);
  CHECK(crowd.prototypes()[0].kind == App::Crowd::AgentKind::k_slider);
  CHECK(crowd.prototypes()[0].first_level == 1U);
  CHECK(crowd.prototypes()[2].first_level == 0U);
  CHECK(crowd.prototypes()[0].switch_depths == App::Crowd::K_SLIDER_LOD_DEPTHS);
  for (const App::Omikron::MaterialGroup& group :
      crowd.prototypes()[0].level_layouts.front().groups) {
    CHECK((group.flags & 0x04000000U) != 0U);
  }

  // Slider spacing 1 m, unscaled by density: one per segment start after
  // the first, as for passersby.
  const std::span<const App::Crowd::Agent> sliders{crowd.agents()};
  REQUIRE(sliders.size() == 3U);
  CHECK(sliders[0].boardable);
  CHECK(sliders[0].prototype == 0U);
  CHECK_FALSE(sliders[1].boardable);
  for (const App::Crowd::Agent& slider : sliders) {
    CHECK(slider.kind == App::Crowd::AgentKind::k_slider);
    CHECK(slider.max_speed == doctest::Approx(5120.0F));
    CHECK(slider.nominal_speed == doctest::Approx(256.0F));
    CHECK(slider.radius == doctest::Approx(80.0F));
    CHECK(slider.instance.y == doctest::Approx(slider.position.y - 30.75F));
  }
}

TEST_CASE("Crowd sliders accelerate toward their leading point and follow routes") {
  App::Crowd::Runtime crowd;
  REQUIRE(crowd.populate(make_slider_request()).has_value());
  const float start_x{crowd.agents()[0].position.x};
  for (int tick{0}; tick < 5; ++tick) {
    crowd.tick(1.0F);
  }
  const App::Crowd::Agent& slider{crowd.agents()[0]};
  // +1 inch/tick^2 from 1 inch/tick.
  CHECK(slider.nominal_speed == doctest::Approx(256.0F * 6.0F));
  CHECK(slider.position.x > start_x);
  const App::Runtime::Vec3 forward{
      App::Runtime::transform_vector(App::Runtime::Vec3{.z = -1.0F}, slider.facing)};
  CHECK(forward.x == doctest::Approx(1.0F).epsilon(0.01));

  bool changed_trajectory{false};
  float fastest{0.0F};
  for (int tick{0}; tick < 2000 && !changed_trajectory; ++tick) {
    crowd.tick(1.0F);
    fastest = std::max(fastest, crowd.agents()[0].nominal_speed);
    changed_trajectory = crowd.agents()[0].trajectory == 1U;
  }
  CHECK(changed_trajectory);
  CHECK(fastest <= 5120.0F);
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/GameState.hpp"
#include "Core/Object/ObjectPlacementRuntime.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Omikron/IamStart.hpp"
#include "Core/RuntimeMath.hpp"
#include "IamStartTestData.hpp"

namespace {

using App::GameState;
using App::ObjectPlacement::ModelResource;
using App::ObjectPlacement::RuntimePlacement;
using ObjectRuntime = App::ObjectPlacement::Runtime;
using App::Omikron::IamAreaRecord;
using App::Omikron::IamSceneRecord;

template <typename Value>
void write(std::vector<std::byte>& data, const std::size_t offset, const Value value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_model_name(
    std::vector<std::byte>& data, const std::size_t offset, const std::string_view name) {
  std::memcpy(data.data() + offset, name.data(), name.size());
}

void write_object_pair(std::vector<std::byte>& data,
    const std::size_t placement_offset,
    const std::size_t definition_offset,
    const std::int16_t object_id,
    const std::int16_t state_index,
    const std::string_view model_name) {
  write(data, placement_offset + 0x00U, static_cast<std::int16_t>(-1));
  write(data, placement_offset + 0x02U, object_id);
  write(data, placement_offset + 0x04U, static_cast<std::int32_t>(100));
  write(data, placement_offset + 0x08U, static_cast<std::int32_t>(-200));
  write(data, placement_offset + 0x0CU, static_cast<std::int32_t>(300));
  write(data, placement_offset + 0x10U, static_cast<std::int16_t>(10));
  write(data, placement_offset + 0x12U, static_cast<std::int16_t>(20));
  write(data, placement_offset + 0x14U, static_cast<std::int16_t>(30));
  write(data, placement_offset + 0x16U, state_index);

  write(data, definition_offset + 0x00U, object_id);
  write(data, definition_offset + 0x02U, static_cast<std::uint16_t>(0x1234));
  write_model_name(data, definition_offset + 0x0EU, model_name);
}

std::vector<std::byte> make_area_object(
    const std::int16_t object_id, const std::int16_t state_index) {
  constexpr std::size_t k_placement{IamAreaRecord::k_header_size};
  constexpr std::size_t k_definition{k_placement + 0x18U};
  std::vector<std::byte> data(k_definition + 0x18U, std::byte{});
  write(data, IamAreaRecord::k_offset_primary_event, 0U);
  write(data,
      IamAreaRecord::k_offset_table_offsets + (1U * 4U),
      static_cast<std::uint32_t>(k_placement));
  write(data, IamAreaRecord::k_offset_table_counts + (1U * 2U), static_cast<std::uint16_t>(1));
  write(data,
      IamAreaRecord::k_offset_table_offsets + (3U * 4U),
      static_cast<std::uint32_t>(k_definition));
  write(data, IamAreaRecord::k_offset_table_counts + (3U * 2U), static_cast<std::uint16_t>(1));
  write_object_pair(data, k_placement, k_definition, object_id, state_index, "RINGS3");
  return data;
}

std::vector<std::byte> make_scene_object(
    const std::int16_t object_id, const std::int16_t state_index) {
  constexpr std::size_t k_placement{IamSceneRecord::k_header_size};
  constexpr std::size_t k_definition{k_placement + 0x18U};
  std::vector<std::byte> data(k_definition + 0x18U, std::byte{});
  write(data,
      IamSceneRecord::k_offset_table_offsets + (1U * 4U),
      static_cast<std::uint32_t>(k_placement));
  write(data, IamSceneRecord::k_offset_table_counts + (1U * 2U), static_cast<std::int16_t>(1));
  write(data,
      IamSceneRecord::k_offset_table_offsets + (3U * 4U),
      static_cast<std::uint32_t>(k_definition));
  write(data, IamSceneRecord::k_offset_table_counts + (3U * 2U), static_cast<std::int16_t>(1));
  write_object_pair(data, k_placement, k_definition, object_id, state_index, "RINGS3");
  return data;
}

GameState make_game_state() {
  const auto bytes{App::Tests::make_canonical_start()};
  const auto start{App::Omikron::IamStart::load(bytes)};
  if (!start) {
    throw std::runtime_error(start.error());
  }
  auto state{GameState::from_start(start.value())};
  if (!state) {
    throw std::runtime_error(state.error());
  }
  return std::move(state).value();
}

auto fake_model_loader() {
  return [](const std::string_view name)
             -> std::expected<std::shared_ptr<const ModelResource>, std::string> {
    auto resource{std::make_shared<ModelResource>()};
    resource->name = name;
    return std::shared_ptr<const ModelResource>{std::move(resource)};
  };
}

}  // namespace

TEST_SUITE("Core::ObjectPlacement::Runtime") {
  TEST_CASE("packed bit 0 gates materialization and bit 1 selects initial enabled state") {
    const auto area{IamAreaRecord::load(make_area_object(162, 471))};
    REQUIRE(area.has_value());

    SUBCASE("state 2 remains absent because bit 0 is clear") {
      GameState game_state{make_game_state()};
      REQUIRE(game_state.set_packed_state(471, 2).has_value());
      ObjectRuntime runtime{fake_model_loader()};
      REQUIRE(runtime.materialize_area_objects(118, area.value(), game_state).has_value());
      CHECK(runtime.placements().empty());
    }

    SUBCASE("state 1 materializes disabled") {
      GameState game_state{make_game_state()};
      REQUIRE(game_state.set_packed_state(471, 1).has_value());
      ObjectRuntime runtime{fake_model_loader()};
      REQUIRE(runtime.materialize_area_objects(118, area.value(), game_state).has_value());
      REQUIRE_EQ(runtime.placements().size(), 1U);
      CHECK_FALSE(runtime.placements().front().enabled);
    }

    SUBCASE("state 3 materializes enabled") {
      GameState game_state{make_game_state()};
      REQUIRE(game_state.set_packed_state(471, 3).has_value());
      ObjectRuntime runtime{fake_model_loader()};
      REQUIRE(runtime.materialize_area_objects(118, area.value(), game_state).has_value());
      REQUIRE_EQ(runtime.placements().size(), 1U);
      CHECK(runtime.placements().front().enabled);
    }
  }

  TEST_CASE(
      "object placement transform reproduces Runtime direct XYZ and literal-degree Euler path") {
    const auto area{IamAreaRecord::load(make_area_object(162, 471))};
    REQUIRE(area.has_value());
    GameState game_state{make_game_state()};
    REQUIRE(game_state.set_packed_state(471, 3).has_value());
    ObjectRuntime runtime{fake_model_loader()};
    REQUIRE(runtime.materialize_area_objects(118, area.value(), game_state).has_value());
    const RuntimePlacement* const placement{runtime.find(118, std::nullopt, 162)};
    REQUIRE(placement != nullptr);

    CHECK_EQ(placement->transform.translation.x, 100.0F);
    CHECK_EQ(placement->transform.translation.y, -200.0F);
    CHECK_EQ(placement->transform.translation.z, 300.0F);
    constexpr float k_pi{3.14159265358979323846F};
    const App::Runtime::Matrix3 expected{App::Runtime::euler_rotation(
        10.0F * k_pi / 180.0F, 20.0F * k_pi / 180.0F, 30.0F * k_pi / 180.0F)};
    for (std::size_t index{0}; index < expected.values.size(); ++index) {
      CHECK(placement->transform.matrix.values.at(index) ==
            doctest::Approx(expected.values.at(index)).epsilon(0.00001));
    }

    REQUIRE(runtime.set_enabled(118, std::nullopt, 162, false).has_value());
    CHECK_FALSE(placement->enabled);
    REQUIRE(runtime.set_enabled(118, std::nullopt, 162, true).has_value());
    CHECK(placement->enabled);
  }

  TEST_CASE("SCENE dematerialization removes only SCENE-owned placements") {
    const auto area{IamAreaRecord::load(make_area_object(162, 471))};
    const auto scene{IamSceneRecord::load(make_scene_object(163, 472))};
    REQUIRE(area.has_value());
    REQUIRE(scene.has_value());
    GameState game_state{make_game_state()};
    REQUIRE(game_state.set_packed_state(471, 3).has_value());
    REQUIRE(game_state.set_packed_state(472, 3).has_value());
    ObjectRuntime runtime{fake_model_loader()};
    REQUIRE(runtime.materialize_area_objects(118, area.value(), game_state).has_value());
    REQUIRE(runtime.materialize_scene_objects(118, 55, scene.value(), game_state).has_value());
    REQUIRE_EQ(runtime.placements().size(), 2U);

    runtime.dematerialize_scene_objects(118, 55);
    REQUIRE_EQ(runtime.placements().size(), 1U);
    CHECK(runtime.find(118, std::nullopt, 162) != nullptr);
    CHECK(runtime.find(118, std::optional<std::int32_t>{55}, 163) == nullptr);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
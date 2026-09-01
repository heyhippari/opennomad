#include "Core/Scenario/CharacterReferenceRuntime.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamScene.hpp"

namespace {

void write_i16(std::vector<std::byte>& data, const std::size_t offset, const std::int16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u16(std::vector<std::byte>& data, const std::size_t offset, const std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u32(std::vector<std::byte>& data, const std::size_t offset, const std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

App::Omikron::IamAreaRecord make_area(const std::int16_t character_id,
    const std::optional<std::int16_t> second_character_id = std::nullopt) {
  constexpr std::size_t k_placement_offset{App::Omikron::IamAreaRecord::k_header_size};
  std::vector<std::byte> data(
      k_placement_offset + (second_character_id.has_value() ? 0x28U : 0x14U), std::byte{});
  // NOLINTNEXTLINE(readability-suspicious-call-argument)
  write_u32(data, App::Omikron::IamAreaRecord::k_offset_table_offsets, k_placement_offset);
  write_u16(data,
      App::Omikron::IamAreaRecord::k_offset_table_counts,
      second_character_id.has_value() ? 2U : 1U);
  write_i16(data, k_placement_offset, -1);
  write_i16(data, k_placement_offset + 2U, character_id);
  if (second_character_id.has_value()) {
    write_i16(data, k_placement_offset + 0x14U, -1);
    write_i16(data, k_placement_offset + 0x16U, second_character_id.value_or(0));
  }
  auto area{App::Omikron::IamAreaRecord::load(data)};
  REQUIRE(area.has_value());
  return std::move(area).value();
}

App::Omikron::IamSceneRecord make_scene(const std::int16_t character_id) {
  constexpr std::size_t k_placement_offset{App::Omikron::IamSceneRecord::k_header_size};
  constexpr std::size_t k_record_size{k_placement_offset + 0x14U};
  std::vector<std::byte> data(k_record_size, std::byte{});
  write_u32(data,
      App::Omikron::IamSceneRecord::k_offset_table_offsets,
      static_cast<std::uint32_t>(k_placement_offset));
  write_u16(data, App::Omikron::IamSceneRecord::k_offset_table_counts, 1U);
  for (const std::size_t table_index : {1U, 2U, 3U, 4U, 6U, 7U}) {
    write_u32(data,
        App::Omikron::IamSceneRecord::k_offset_table_offsets + (table_index * 4U),
        static_cast<std::uint32_t>(k_record_size));
  }
  write_i16(data, k_placement_offset, -1);
  write_i16(data, k_placement_offset + 2U, character_id);
  auto scene{App::Omikron::IamSceneRecord::load(data)};
  REQUIRE(scene.has_value());
  return std::move(scene).value();
}

App::ResolvedCharacterReference resolved_body(
    const App::Character::BodyIdentity body_identity, const std::uint32_t world_scene_id) {
  return App::ResolvedCharacterReference{
      .body_identity = body_identity, .body_world_scene_id = world_scene_id};
}

}  // namespace

TEST_SUITE("Core::Scenario::CharacterReferenceRuntime") {
  TEST_CASE("installation copies mutable references without mutating parsed provenance") {
    const App::Omikron::IamAreaRecord area{make_area(310)};
    const App::Omikron::IamSceneRecord scene{make_scene(57)};
    App::CharacterReferenceRuntime runtime;
    runtime.install_area(0U, 118, area);
    runtime.install_scene(0U, 118, 4, scene);

    REQUIRE_EQ(runtime.entries().size(), 2U);
    CHECK(runtime.entries().at(0).source == App::CharacterReferenceSource::k_area);
    CHECK(runtime.entries().at(1).source == App::CharacterReferenceSource::k_scene);
    CHECK_EQ(runtime.entries().at(0).serialized_character_id, 310);
    CHECK_EQ(runtime.entries().at(1).serialized_character_id, 57);
    CHECK(runtime.entries().at(0).binding_state ==
          App::CharacterPlacementBindingState::k_unmaterialized);

    REQUIRE(runtime.rebind_placement(0U, 57, 41U).has_value());
    CHECK_EQ(runtime.entries().at(0).reference_character_id, 57);
    CHECK_EQ(runtime.entries().at(0).serialized_character_id, 310);
    CHECK_EQ(area.character_placements().at(0).character_id, 310);
    CHECK_EQ(scene.character_placements().at(0).character_id, 57);

    REQUIRE(runtime
            .mark_placement_explicitly_unbound(
                App::CharacterReferenceSource::k_area, 0U, 118, -1, 0U)
            .has_value());
    CHECK(runtime.entries().at(0).binding_state ==
          App::CharacterPlacementBindingState::k_explicitly_unbound);
    CHECK_FALSE(runtime.entries().at(0).body_identity.has_value());
  }

  TEST_CASE("AREA placement takes precedence over an attached SCENE and global body") {
    App::CharacterReferenceRuntime runtime;
    runtime.set_body_locator([](const App::Character::BodyIdentity body_identity) {
      return std::optional<App::ResolvedCharacterReference>{resolved_body(body_identity, 7U)};
    });
    runtime.set_canonical_locator([](const std::int16_t) {
      return std::optional<App::ResolvedCharacterReference>{resolved_body(99U, 9U)};
    });
    runtime.install_area(0U, 118, make_area(310));
    runtime.install_scene(0U, 118, 4, make_scene(310));
    REQUIRE(runtime.bind_placement_body(App::CharacterReferenceSource::k_area, 0U, 118, -1, 0U, 11U)
            .has_value());
    REQUIRE(runtime.bind_placement_body(App::CharacterReferenceSource::k_scene, 0U, 118, 4, 0U, 22U)
            .has_value());

    const App::CharacterReferenceResolution result{runtime.resolve(0U, 118, 310)};
    REQUIRE(result.resolved.has_value());
    const App::ResolvedCharacterReference resolution{result.resolved.value_or({})};
    CHECK(result.status == App::CharacterReferenceResolutionStatus::k_resolved);
    CHECK(resolution.source == App::CharacterReferenceResolutionSource::k_area_placement);
    CHECK_EQ(resolution.body_identity, 11U);
    CHECK_EQ(resolution.body_world_scene_id, 7U);
  }

  TEST_CASE("SCENE placement takes precedence over global body") {
    App::CharacterReferenceRuntime runtime;
    runtime.set_body_locator([](const App::Character::BodyIdentity body_identity) {
      return std::optional<App::ResolvedCharacterReference>{resolved_body(body_identity, 2U)};
    });
    runtime.set_canonical_locator([](const std::int16_t) {
      return std::optional<App::ResolvedCharacterReference>{resolved_body(99U, 9U)};
    });
    runtime.install_scene(0U, 118, 4, make_scene(57));
    REQUIRE(runtime.bind_placement_body(App::CharacterReferenceSource::k_scene, 0U, 118, 4, 0U, 22U)
            .has_value());

    const App::CharacterReferenceResolution result{runtime.resolve(0U, 118, 57)};
    REQUIRE(result.resolved.has_value());
    const App::ResolvedCharacterReference resolution{result.resolved.value_or({})};
    CHECK(resolution.source == App::CharacterReferenceResolutionSource::k_scene_placement);
    CHECK_EQ(resolution.body_identity, 22U);
  }

  TEST_CASE("matching AREA and SCENE no-body states block global fallback") {
    const auto verify_blocked = [](const App::CharacterReferenceSource source,
                                    const bool explicitly_unbound) {
      App::CharacterReferenceRuntime runtime;
      runtime.set_canonical_locator([](const std::int16_t) {
        return std::optional<App::ResolvedCharacterReference>{resolved_body(99U, 9U)};
      });
      if (source == App::CharacterReferenceSource::k_area) {
        runtime.install_area(0U, 118, make_area(310));
      } else {
        runtime.install_scene(0U, 118, 4, make_scene(310));
      }
      if (explicitly_unbound) {
        REQUIRE(runtime
                .mark_placement_explicitly_unbound(
                    source, 0U, 118, source == App::CharacterReferenceSource::k_area ? -1 : 4, 0U)
                .has_value());
      }
      const App::CharacterReferenceResolution result{runtime.resolve(0U, 118, 310)};
      CHECK(result.status == App::CharacterReferenceResolutionStatus::k_placement_without_body);
      CHECK_FALSE(result.resolved.has_value());
      CHECK(result.binding_state.value_or(App::CharacterPlacementBindingState::k_bound) ==
            (explicitly_unbound ? App::CharacterPlacementBindingState::k_explicitly_unbound
                                : App::CharacterPlacementBindingState::k_unmaterialized));
    };

    verify_blocked(App::CharacterReferenceSource::k_area, false);
    verify_blocked(App::CharacterReferenceSource::k_scene, false);
    verify_blocked(App::CharacterReferenceSource::k_area, true);
    verify_blocked(App::CharacterReferenceSource::k_scene, true);
  }

  TEST_CASE("bound displaced mapping wins over an earlier duplicate unbound reference") {
    App::CharacterReferenceRuntime runtime;
    runtime.set_body_locator([](const App::Character::BodyIdentity body_identity) {
      return std::optional<App::ResolvedCharacterReference>{resolved_body(body_identity, 1U)};
    });
    runtime.install_area(0U, 118, make_area(310, 57));
    REQUIRE(runtime
            .mark_placement_explicitly_unbound(
                App::CharacterReferenceSource::k_area, 0U, 118, -1, 0U)
            .has_value());
    REQUIRE(runtime.rebind_placement(1U, 310, 42U).has_value());

    const auto placement{runtime.find_mutable_placement(0U, 118, 310)};
    REQUIRE(placement.has_value());
    CHECK_EQ(placement.value_or(0U), 1U);
  }

  TEST_CASE("stale bound BodyIdentity blocks global fallback") {
    App::CharacterReferenceRuntime runtime;
    runtime.set_body_locator([](const App::Character::BodyIdentity) {
      return std::optional<App::ResolvedCharacterReference>{};
    });
    runtime.set_canonical_locator([](const std::int16_t) {
      return std::optional<App::ResolvedCharacterReference>{resolved_body(99U, 9U)};
    });
    runtime.install_area(0U, 118, make_area(310));
    REQUIRE(runtime.bind_placement_body(App::CharacterReferenceSource::k_area, 0U, 118, -1, 0U, 41U)
            .has_value());

    const App::CharacterReferenceResolution result{runtime.resolve(0U, 118, 310)};
    CHECK(result.status == App::CharacterReferenceResolutionStatus::k_placement_without_body);
    CHECK(result.binding_state.value_or(App::CharacterPlacementBindingState::k_unmaterialized) ==
          App::CharacterPlacementBindingState::k_bound);
  }

  TEST_CASE("matching no-body placement prevents global fallback until rebound") {
    App::CharacterReferenceRuntime runtime;
    runtime.set_body_locator([](const App::Character::BodyIdentity body_identity) {
      return std::optional<App::ResolvedCharacterReference>{resolved_body(body_identity, 3U)};
    });
    runtime.set_canonical_locator([](const std::int16_t) {
      return std::optional<App::ResolvedCharacterReference>{resolved_body(99U, 9U)};
    });
    runtime.install_area(0U, 118, make_area(310));

    const auto placement{runtime.find_mutable_placement(0U, 118, 310)};
    REQUIRE(placement.has_value());
    CHECK(runtime.resolve(0U, 118, 310).status ==
          App::CharacterReferenceResolutionStatus::k_placement_without_body);
    REQUIRE(runtime
            .mark_placement_explicitly_unbound(
                App::CharacterReferenceSource::k_area, 0U, 118, -1, 0U)
            .has_value());
    const App::CharacterReferenceResolution unbound{runtime.resolve(0U, 118, 310)};
    CHECK(unbound.status == App::CharacterReferenceResolutionStatus::k_placement_without_body);
    REQUIRE(unbound.binding_state.has_value());
    CHECK(unbound.binding_state.value_or(App::CharacterPlacementBindingState::k_unmaterialized) ==
          App::CharacterPlacementBindingState::k_explicitly_unbound);

    REQUIRE(runtime.rebind_placement(placement.value_or(0U), 310, 42U).has_value());
    const App::CharacterReferenceResolution rebound{runtime.resolve(0U, 118, 310)};
    REQUIRE(rebound.resolved.has_value());
    const App::ResolvedCharacterReference rebound_resolution{rebound.resolved.value_or({})};
    CHECK(rebound_resolution.source == App::CharacterReferenceResolutionSource::k_area_placement);
    CHECK_EQ(rebound_resolution.body_identity, 42U);
  }

  TEST_CASE("global body resolves only when no matching mutable placement exists") {
    App::CharacterReferenceRuntime runtime;
    runtime.set_canonical_locator([](const std::int16_t character_id) {
      return character_id == 57
                 ? std::optional<App::ResolvedCharacterReference>{resolved_body(88U, 2U)}
                 : std::nullopt;
    });
    runtime.install_area(0U, 118, make_area(310));

    const App::CharacterReferenceResolution result{runtime.resolve(0U, 118, 57)};
    REQUIRE(result.resolved.has_value());
    const App::ResolvedCharacterReference resolution{result.resolved.value_or({})};
    CHECK(result.status == App::CharacterReferenceResolutionStatus::k_resolved);
    CHECK(resolution.source == App::CharacterReferenceResolutionSource::k_global_body);
    CHECK_EQ(resolution.body_identity, 88U);
  }

  TEST_CASE("remove reset and reinstall restore serialized placement state") {
    App::CharacterReferenceRuntime runtime;
    runtime.install_area(0U, 118, make_area(310));
    runtime.install_scene(0U, 118, 4, make_scene(57));
    REQUIRE(runtime.rebind_placement(0U, 57, 41U).has_value());

    runtime.remove_scene(0U, 4);
    REQUIRE_EQ(runtime.entries().size(), 1U);
    runtime.remove_area(0U, 118);
    CHECK(runtime.entries().empty());

    runtime.install_area(0U, 118, make_area(310));
    REQUIRE_EQ(runtime.entries().size(), 1U);
    CHECK_EQ(runtime.entries().front().serialized_character_id, 310);
    CHECK_EQ(runtime.entries().front().reference_character_id, 310);
    CHECK(runtime.entries().front().binding_state ==
          App::CharacterPlacementBindingState::k_unmaterialized);
    runtime.reset();
    CHECK(runtime.entries().empty());
  }

  TEST_CASE("bound BodyIdentity remains stable when its owning world changes") {
    std::uint32_t owning_world{1U};
    App::CharacterReferenceRuntime runtime;
    runtime.set_body_locator([&owning_world](const App::Character::BodyIdentity body_identity) {
      return std::optional<App::ResolvedCharacterReference>{
          resolved_body(body_identity, owning_world)};
    });
    runtime.install_scene(0U, 118, 4, make_scene(57));
    REQUIRE(runtime.bind_placement_body(App::CharacterReferenceSource::k_scene, 0U, 118, 4, 0U, 88U)
            .has_value());

    owning_world = 2U;
    const App::CharacterReferenceResolution result{runtime.resolve(0U, 118, 57)};
    REQUIRE(result.resolved.has_value());
    const App::ResolvedCharacterReference resolution{result.resolved.value_or({})};
    CHECK_EQ(resolution.body_identity, 88U);
    CHECK_EQ(resolution.body_world_scene_id, 2U);
  }
}
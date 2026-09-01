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

App::Omikron::IamAreaRecord make_area(const std::int16_t character_id) {
  constexpr std::size_t k_placement_offset{App::Omikron::IamAreaRecord::k_header_size};
  std::vector<std::byte> data(k_placement_offset + 0x14U, std::byte{});
  // NOLINTNEXTLINE(readability-suspicious-call-argument)
  write_u32(data, App::Omikron::IamAreaRecord::k_offset_table_offsets, k_placement_offset);
  write_u16(data, App::Omikron::IamAreaRecord::k_offset_table_counts, 1U);
  write_i16(data, k_placement_offset, -1);
  write_i16(data, k_placement_offset + 2U, character_id);
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
}
#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "Core/Script/ScriptRuntime.hpp"
#include "Core/Sprite/SpriteInstance.hpp"

namespace {

/// One inert parsed command; content is not dispatched by these tests.
App::Omikron::ScxScriptCommand make_command() {
  return App::Omikron::ScxScriptCommand{.opcode = 0x04000029U,
      .value_count = 0,
      .first_value_index = 0,
      .next_linked_command_index = std::nullopt,
      .execution_limit = 0xFFFFFFFFU,
      .initial_execution_count = 0,
      .file_offset = 0};
}

/// One script template with the given root command count.
App::Omikron::ScxScript make_script(const std::string_view name,
    const std::size_t root_command_count) {
  App::Omikron::ScxScript script;
  script.name = std::string{name};
  script.root_command_count = static_cast<std::uint32_t>(root_command_count);
  script.linked_command_count = 0;
  if (root_command_count > 0) {
    script.root_commands.push_back(make_command());
  }
  return script;
}

void write_i16(std::vector<std::byte>& data, const std::size_t offset, const std::int16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u16(std::vector<std::byte>& data, const std::size_t offset, const std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u32(std::vector<std::byte>& data, const std::size_t offset, const std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

App::Omikron::IamAreaRecord make_character_area() {
  constexpr std::size_t k_placement_offset{App::Omikron::IamAreaRecord::k_header_size};
  constexpr std::size_t k_definition_offset{k_placement_offset + 0x14U};
  std::vector<std::byte> data(k_definition_offset + 0x114U, std::byte{});
  write_u32(
      data, App::Omikron::IamAreaRecord::k_offset_script, static_cast<std::uint32_t>(data.size()));
  write_u32(data, App::Omikron::IamAreaRecord::k_offset_table_offsets, k_placement_offset);
  write_u16(data, App::Omikron::IamAreaRecord::k_offset_table_counts, 1);
  write_i16(data, k_placement_offset, -1);
  write_i16(data, k_placement_offset + 0x02U, 310);
  write_u16(data, k_placement_offset + 0x12U, 468);
  write_u32(
      data, App::Omikron::IamAreaRecord::k_offset_table_offsets + (4U * 4U), k_definition_offset);
  write_u16(data, App::Omikron::IamAreaRecord::k_offset_table_counts + (4U * 2U), 1);
  constexpr std::string_view k_name{"KAY'L 669"};
  std::memcpy(data.data() + k_definition_offset + 0x08U, k_name.data(), k_name.size());
  constexpr std::string_view k_model{"HO1_FNM"};
  std::memcpy(data.data() + k_definition_offset + 0x90U, k_model.data(), k_model.size());
  write_u16(data, k_definition_offset + 0x110U, 310);
  return App::Omikron::IamAreaRecord::load(data).value();
}

}  // namespace

TEST_SUITE("Core::Scenario::ScenarioRuntime") {
  TEST_CASE("Initializes an empty scenario") {
    App::Omikron::ScxData scx;
    App::ScenarioRuntime runtime;

    const auto result{runtime.initialize(
        scx, std::span<const std::byte>{}, "empty", nullptr, false)};
    REQUIRE(result.has_value());
    CHECK(runtime.initialized());
    CHECK(runtime.script_runtime() != nullptr);
    CHECK_EQ(runtime.script_scenario_name(), "empty");
    CHECK_EQ(runtime.sprite_resource_count(), 0U);
    CHECK(runtime.script_runtime()->instances().empty());
  }

  TEST_CASE("Loads script templates inactive by default") {
    App::Omikron::ScxData scx;
    scx.scripts.push_back(make_script("a", 1));
    scx.scripts.push_back(make_script("b", 1));
    scx.scripts.push_back(make_script("c", 0));
    App::ScenarioRuntime runtime;

    const auto result{runtime.initialize(
        scx, std::span<const std::byte>{}, "templates", nullptr, false)};
    REQUIRE(result.has_value());
    REQUIRE(runtime.script_runtime() != nullptr);
    CHECK_EQ(runtime.script_runtime()->scx().scripts.size(), 3U);
    CHECK(runtime.script_runtime()->instances().empty());
  }

  TEST_CASE("Activating startup scripts creates one instance per rooted script") {
    App::Omikron::ScxData scx;
    scx.scripts.push_back(make_script("a", 1));
    scx.scripts.push_back(make_script("b", 0));
    App::ScenarioRuntime runtime;

    const auto result{runtime.initialize(
        scx, std::span<const std::byte>{}, "active", nullptr, true)};
    REQUIRE(result.has_value());
    REQUIRE(runtime.script_runtime() != nullptr);
    REQUIRE_EQ(runtime.script_runtime()->instances().size(), 1U);
    CHECK_EQ(runtime.script_runtime()->instances().at(0).script_name, "a");
  }

  TEST_CASE("Spawns a script instance on an inactive runtime") {
    App::Omikron::ScxData scx;
    scx.scripts.push_back(make_script("a", 1));
    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(scx, std::span<const std::byte>{}, "inactive", nullptr, false)
                .has_value());

    const auto instance{runtime.spawn_script_instance(0)};
    REQUIRE(instance.has_value());
    REQUIRE_EQ(runtime.script_runtime()->instances().size(), 1U);
    CHECK_EQ(runtime.script_runtime()->instances().at(0).script_name, "a");
  }

  TEST_CASE("Spawns a character-bound script only for an active runtime character") {
    App::Omikron::ScxData scx;
    scx.scripts.push_back(make_script("unrelated", 1));
    scx.scripts.push_back(make_script("1KaylArrives", 1));
    scx.scripts.at(0).script_id = 99;
    scx.scripts.at(1).script_id = 1;
    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(scx, std::span<const std::byte>{}, "character", nullptr, false)
            .has_value());

    auto missing{runtime.spawn_character_script_instance(1, 310, 0)};
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().find("does not exist") != std::string::npos);

    runtime.character_runtime().set_model_loader(
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          auto resource{std::make_shared<App::Character::ModelResource>()};
          resource->name = name;
          resource->groups.push_back(App::Omikron::MaterialGroup{});
          return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
        });
    const App::Omikron::IamAreaRecord area{make_character_area()};
    const auto activated{runtime.activate_character(118,
        area,
        App::Script::AreaCharacterActivationRequest{
            .character_id = 310, .apply_area_transform = true})};
    const std::string activation_error{activated ? std::string{} : activated.error()};
    CAPTURE(activation_error);
    REQUIRE(activated.has_value());

    const auto created{runtime.spawn_character_script_instance(1, 310, -5)};
    REQUIRE(created.has_value());
    const App::Script::ScriptInstance* instance{
        runtime.script_runtime()->instance(created.value())};
    REQUIRE(instance != nullptr);
    CHECK_EQ(instance->source_script_index, 1U);
    CHECK_EQ(instance->script_name, "1KaylArrives");
    CHECK_EQ(instance->launch_context.character_id, std::optional<std::int16_t>{310});
    CHECK_EQ(instance->launch_context.parameter, -5);
  }

  TEST_CASE("Sprite pool lifecycle works through the runtime") {
    App::Omikron::ScxData scx;
    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(scx, std::span<const std::byte>{}, "pool", nullptr, false)
                .has_value());

    App::Sprite::SpritePool& pool{runtime.sprite_pool()};
    const auto handle{pool.create(0, 0, 4, {1.0F, 2.0F, 3.0F})};
    REQUIRE(handle.has_value());
    CHECK(pool.find(handle.value()) != nullptr);
    REQUIRE(pool.attach(handle.value()).has_value());
    CHECK(pool.attached(handle.value()));
    REQUIRE(pool.detach(handle.value()).has_value());
    CHECK_FALSE(pool.attached(handle.value()));
    REQUIRE(pool.destroy(handle.value()).has_value());
    CHECK(pool.find(handle.value()) == nullptr);
  }

  TEST_CASE("World anchor defaults to the origin and is settable") {
    App::Omikron::ScxData scx;
    App::ScenarioRuntime runtime;
    REQUIRE(runtime.initialize(scx, std::span<const std::byte>{}, "anchor", nullptr, false)
                .has_value());

    CHECK_EQ(runtime.world_anchor().at(0), 0.0F);
    CHECK_EQ(runtime.world_anchor().at(1), 0.0F);
    CHECK_EQ(runtime.world_anchor().at(2), 0.0F);

    runtime.set_world_anchor({4.0F, 5.0F, 6.0F});
    CHECK_EQ(runtime.world_anchor().at(0), 4.0F);
    CHECK_EQ(runtime.world_anchor().at(1), 5.0F);
    CHECK_EQ(runtime.world_anchor().at(2), 6.0F);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// cppcoreguidelines-pro-bounds-pointer-arithmetic)

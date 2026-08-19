#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Omikron/SCX.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
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
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

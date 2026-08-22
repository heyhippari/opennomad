#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <SDL3/SDL_stdinc.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Debug/DebugRuntimeContext.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "OmikronTestBuffer.hpp"

namespace {

constexpr std::uint32_t K_MAGIC{0x00DEAD00U};
constexpr std::uint32_t K_SCRIPTS_TAG{0xDEAD0002U};
constexpr std::uint32_t K_SOUNDS_TAG{0xDEAD0003U};
constexpr std::uint32_t K_SPRITES_TAG{0xDEAD0004U};
constexpr std::uint32_t K_END_TAG{0xDEADFFFFU};
constexpr std::uint32_t K_SET_FRAME{0x04000029U};
constexpr std::uint32_t K_RIFF{0x46464952U};
constexpr std::uint32_t K_WAVE{0x45564157U};

/// Writes a fixture's bytes to disk, creating parent directories.
void write_bytes(const std::filesystem::path& path, const Buffer& buffer) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream{path, std::ios::binary};
  const std::vector<std::byte>& data{buffer.data()};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

/// Wraps a descriptor block in a full SCX container (no appended stream).
Buffer make_scx(const Buffer& descriptor) {
  Buffer bytes;
  bytes.u32(K_MAGIC).u32(5).u32(8).u32(static_cast<std::uint32_t>(descriptor.data().size()));
  for (const std::byte byte : descriptor.data()) {
    bytes.u8(std::to_integer<std::uint8_t>(byte));
  }
  return bytes;
}

/// Appends one 0x18-byte serialized command record.
Buffer& command(Buffer& buffer,
    const std::uint32_t opcode,
    const std::uint32_t value_count,
    const std::uint32_t first_value_index,
    const std::int32_t next_command_index) {
  buffer.u32(opcode).u32(value_count).u32(first_value_index).i32(next_command_index)
      .u32(1).u32(0);
  return buffer;
}

/// Appends one fixed 0x64-byte script record.
Buffer& script_record(Buffer& buffer,
    const std::string_view name,
    const std::uint16_t script_id,
    const std::uint32_t root_command_count,
    const std::uint32_t linked_command_count) {
  buffer.u32(0x00596C60U);
  buffer.chars(name, 22);
  buffer.u16(script_id).u16(1).u16(0);
  buffer.u32(root_command_count).u32(0).u32(0);
  buffer.u32(linked_command_count).u32(0);
  buffer.i32(1).u32(0);
  buffer.u32(0).u32(0).u32(0);
  buffer.u32(0).u32(0).u32(0);
  buffer.u32(0).u32(0);
  buffer.zeros(8);
  return buffer;
}

/// Appends the two empty trailing binding tables.
Buffer& empty_binding_tables(Buffer& buffer) {
  buffer.u32(0);  // binding table A count 0.
  buffer.u32(0);  // binding table B count 0.
  return buffer;
}

/// Builds a descriptor with `count` one-command scripts sharing a 2-value
/// pool, followed by an empty sprite table and the end tag.
Buffer make_script_scx_descriptor(const std::size_t count) {
  Buffer descriptor;
  descriptor.u32(K_SCRIPTS_TAG).u32(static_cast<std::uint32_t>(count));
  for (std::size_t index{0}; index < count; ++index) {
    const std::string name{"script_" + std::to_string(index)};
    script_record(descriptor, name, static_cast<std::uint16_t>(index + 1U), 1, 0);
  }
  descriptor.u32(2).u32(0).f32(1.5F);  // shared value pool (2 values).
  for (std::size_t index{0}; index < count; ++index) {
    descriptor.u8(0);  // related block absent.
    command(descriptor, K_SET_FRAME, 2, 0, -1);
    empty_binding_tables(descriptor);
  }
  descriptor.u32(K_SPRITES_TAG).u32(0);
  descriptor.u32(K_END_TAG);
  return descriptor;
}

/// Builds a valid script-bearing SCX with the given number of templates.
Buffer make_script_scx(const std::size_t count) {
  return make_scx(make_script_scx_descriptor(count));
}

/// Builds a descriptor with a sound table and no scripts.
Buffer make_sounds_scx_descriptor(const std::size_t count) {
  Buffer descriptor;
  descriptor.u32(K_SOUNDS_TAG).u32(static_cast<std::uint32_t>(count));
  for (std::size_t index{0}; index < count; ++index) {
    const std::string name{"SOUND_" + std::to_string(index)};
    descriptor.chars(name, 22).u16(0xFFFFU).u16(0x0011U);
  }
  descriptor.u32(K_SPRITES_TAG).u32(0);
  descriptor.u32(K_END_TAG);
  return descriptor;
}

/// Builds a valid sound-bearing SCX (no scripts) with one RIFF/WAVE payload
/// per sound record, in descriptor order.
Buffer make_sounds_scx(const std::size_t count) {
  Buffer descriptor{make_sounds_scx_descriptor(count)};
  const std::uint32_t stream_offset{
      static_cast<std::uint32_t>(16U + descriptor.data().size())};

  Buffer stream;
  for (std::size_t index{0}; index < count; ++index) {
    const std::uint32_t position{stream_offset + static_cast<std::uint32_t>(index) * 20U};
    stream.u32(position).u32(12).u32(K_RIFF).u32(0).u32(K_WAVE);
  }

  Buffer bytes;
  bytes.u32(K_MAGIC).u32(5).u32(8).u32(static_cast<std::uint32_t>(descriptor.data().size()));
  for (const std::byte byte : descriptor.data()) {
    bytes.u8(std::to_integer<std::uint8_t>(byte));
  }
  for (const std::byte byte : stream.data()) {
    bytes.u8(std::to_integer<std::uint8_t>(byte));
  }
  return bytes;
}

/// Builds an invalid SCX (bad magic).
Buffer make_malformed_scx() {
  Buffer bytes;
  bytes.u32(0x12345678U).u32(5).u32(8).u32(0);
  return bytes;
}

/// Scratch directory wiped on construction and destruction.
class TempDirectory {
 public:
  TempDirectory()
      : m_root{std::filesystem::temp_directory_path() / "opennomad-scenario-test"} {
    std::filesystem::remove_all(m_root);
    std::filesystem::create_directories(m_root);
  }

  ~TempDirectory() { std::filesystem::remove_all(m_root); }

  TempDirectory(const TempDirectory&) = delete;
  TempDirectory(TempDirectory&&) = delete;
  TempDirectory& operator=(const TempDirectory&) = delete;
  TempDirectory& operator=(TempDirectory&&) = delete;

  [[nodiscard]] const std::filesystem::path& root() const { return m_root; }

 private:
  std::filesystem::path m_root;
};

/// RAII override of the game-data root for the duration of a test.
class ScopedGameDataRoot {
 public:
  explicit ScopedGameDataRoot(const std::filesystem::path& root) {
    static_cast<void>(SDL_SetEnvironmentVariable(SDL_GetEnvironment(),
        "OPENNOMAD_GAME_DATA_ROOT",
        root.string().c_str(),
        true));
  }

  ~ScopedGameDataRoot() {
    static_cast<void>(SDL_UnsetEnvironmentVariable(
        SDL_GetEnvironment(), "OPENNOMAD_GAME_DATA_ROOT"));
  }

  ScopedGameDataRoot(const ScopedGameDataRoot&) = delete;
  ScopedGameDataRoot(ScopedGameDataRoot&&) = delete;
  ScopedGameDataRoot& operator=(const ScopedGameDataRoot&) = delete;
  ScopedGameDataRoot& operator=(ScopedGameDataRoot&&) = delete;
};

/// Writes the standard boot fixtures (aventure.scx + Grid.SCX) into a root.
void write_boot_fixtures(const TempDirectory& temp) {
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_script_scx(3));
  write_bytes(temp.root() / "SCPTDATA" / "Grid.SCX", make_script_scx(5));
}

/// Test fixture only: reproduce the historical initial pair explicitly.
/// GRID remains a fixture value here, not a ScenarioManager bootstrap policy.
void initialize_grid_fixture(App::ScenarioManager& manager) {
  REQUIRE(manager.set_gameplay_mode(App::GameplayMode::Adventure).has_value());
  REQUIRE(manager.load_world_context(0, std::nullopt, "SCPTDATA/GRID.SCX").has_value());
  REQUIRE(manager.activate_world_context(0).has_value());
}

}  // namespace

TEST_SUITE("Core::Scenario::ScenarioManager") {
  TEST_CASE("Exposes one mode slot plus exactly two world contexts after boot") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);

    CHECK_EQ(manager.world_contexts().size(), 2U);
    CHECK(manager.gameplay_mode_scx() != nullptr);
    CHECK_EQ(manager.loaded_scenario_count(), 2U);
    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::LoadedActive);
    CHECK_EQ(manager.world_contexts()[0].scene_id, 0U);
    CHECK(manager.world_contexts()[1].residency == App::WorldSceneResidencyState::Free);
  }

  TEST_CASE("Fixture installs aventure then GRID into world context 0") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);

    const std::vector<App::LoadedScenarioView> inventory{manager.scenario_inventory()};
    REQUIRE_EQ(inventory.size(), 3U);
    CHECK_EQ(inventory.at(0).identity.role, App::ScenarioRole::GameplayMode);
    CHECK_EQ(inventory.at(0).scenario_path, "SCPTDATA/aventure.scx");
    CHECK_EQ(inventory.at(1).scenario_path, "SCPTDATA/GRID.SCX");
    CHECK_EQ(inventory.at(1).scene_id, 0U);
    CHECK(inventory.at(1).resolved_path.ends_with("Grid.SCX"));
    CHECK_EQ(inventory.at(2).residency, App::WorldSceneResidencyState::Free);
  }

  TEST_CASE("Unloading world context 0 leaves the gameplay mode unchanged") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);
    const std::string mode_path{manager.scenario_inventory().at(0).resolved_path};

    REQUIRE(manager.deactivate_world_context(0).has_value());
    REQUIRE(manager.unload_world_context(0).has_value());

    CHECK(manager.gameplay_mode_scx() != nullptr);
    CHECK_EQ(manager.scenario_inventory().at(0).resolved_path, mode_path);
    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::Free);
  }

  TEST_CASE("Replacing the gameplay mode leaves both world contexts unchanged") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    write_bytes(temp.root() / "SCPTDATA" / "shoot2.scx", make_script_scx(2));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);

    REQUIRE(manager.set_gameplay_mode(App::GameplayMode::FirstPersonShooting).has_value());
    CHECK(manager.current_gameplay_mode() == App::GameplayMode::FirstPersonShooting);
    CHECK_EQ(manager.scenario_inventory().at(0).scenario_path, "SCPTDATA/shoot2.scx");
    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::LoadedActive);
    CHECK_EQ(manager.world_contexts()[0].scenario_path, "SCPTDATA/GRID.SCX");
    CHECK(manager.world_contexts()[1].residency == App::WorldSceneResidencyState::Free);
  }

  TEST_CASE("Two distinct world contexts coexist and allocation prefers the lowest free") {
    const TempDirectory temp;
    write_bytes(temp.root() / "SCPTDATA" / "Hall27.SCX", make_script_scx(1));
    write_bytes(temp.root() / "SCPTDATA" / "bar56.SCX", make_script_scx(1));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    auto first{manager.load_world_context(27, std::nullopt, "SCPTDATA/Hall27.SCX")};
    REQUIRE(first.has_value());
    CHECK(first.value() == &manager.world_contexts()[0]);

    auto second{manager.load_world_context(56, std::nullopt, "SCPTDATA/bar56.SCX")};
    REQUIRE(second.has_value());
    CHECK(second.value() == &manager.world_contexts()[1]);

    CHECK(manager.find_world_context(27) == &manager.world_contexts()[0]);
    CHECK(manager.find_world_context(56) == &manager.world_contexts()[1]);
  }

  TEST_CASE("With no free entry, allocation recycles the lowest-index inactive context") {
    const TempDirectory temp;
    write_bytes(temp.root() / "SCPTDATA" / "A.SCX", make_script_scx(1));
    write_bytes(temp.root() / "SCPTDATA" / "B.SCX", make_script_scx(1));
    write_bytes(temp.root() / "SCPTDATA" / "C.SCX", make_script_scx(1));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    REQUIRE(manager.load_world_context(1, std::nullopt, "SCPTDATA/A.SCX").has_value());
    REQUIRE(manager.load_world_context(2, std::nullopt, "SCPTDATA/B.SCX").has_value());

    auto third{manager.load_world_context(3, std::nullopt, "SCPTDATA/C.SCX")};
    REQUIRE(third.has_value());
    CHECK_EQ(manager.world_contexts()[0].scene_id, 3U);
    CHECK_EQ(manager.world_contexts()[1].scene_id, 2U);
    CHECK(manager.find_world_context(1) == nullptr);  // A was recycled.
  }

  TEST_CASE("A LoadedActive context is never evicted") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    write_bytes(temp.root() / "SCPTDATA" / "B.SCX", make_script_scx(1));
    write_bytes(temp.root() / "SCPTDATA" / "C.SCX", make_script_scx(1));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);  // context 0 active.
    REQUIRE(manager.load_world_context(2, std::nullopt, "SCPTDATA/B.SCX").has_value());
    REQUIRE(manager.load_world_context(3, std::nullopt, "SCPTDATA/C.SCX").has_value());

    CHECK_EQ(manager.world_contexts()[0].scene_id, 0U);
    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::LoadedActive);
    CHECK_EQ(manager.world_contexts()[1].scene_id, 3U);  // B (inactive) was recycled.
  }

  TEST_CASE("Both contexts LoadedActive causes a clean capacity failure") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    write_bytes(temp.root() / "SCPTDATA" / "B.SCX", make_script_scx(1));
    write_bytes(temp.root() / "SCPTDATA" / "C.SCX", make_script_scx(1));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);  // context 0 active.
    REQUIRE(manager.load_world_context(2, std::nullopt, "SCPTDATA/B.SCX").has_value());
    REQUIRE(manager.activate_world_context(2).has_value());  // context 1 active.

    auto third{manager.load_world_context(3, std::nullopt, "SCPTDATA/C.SCX")};
    REQUIRE_FALSE(third.has_value());
    CHECK(third.error().find("LoadedActive") != std::string::npos);

    CHECK_EQ(manager.world_contexts()[0].scene_id, 0U);
    CHECK_EQ(manager.world_contexts()[1].scene_id, 2U);
    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::LoadedActive);
    CHECK(manager.world_contexts()[1].residency == App::WorldSceneResidencyState::LoadedActive);
  }

  TEST_CASE("Deactivation preserves loaded state and reactivation does not reparse") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);

    const App::Omikron::ScxData* before{manager.world_context_scx(0)};
    const std::uint32_t generation_before{manager.world_contexts()[0].generation};
    REQUIRE(before != nullptr);

    REQUIRE(manager.deactivate_world_context(0).has_value());
    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::LoadedInactive);
    CHECK(manager.world_context_scx(0) == before);  // Same parsed data, no reparse.

    REQUIRE(manager.activate_world_context(0).has_value());
    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::LoadedActive);
    CHECK(manager.world_context_scx(0) == before);
    CHECK_EQ(manager.world_contexts()[0].generation, generation_before);
  }

  TEST_CASE("Unloading one context leaves the other and the mode slot intact") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    write_bytes(temp.root() / "SCPTDATA" / "B.SCX", make_script_scx(1));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);
    REQUIRE(manager.deactivate_world_context(0).has_value());
    REQUIRE(manager.load_world_context(2, std::nullopt, "SCPTDATA/B.SCX").has_value());
    REQUIRE(manager.unload_world_context(0).has_value());

    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::Free);
    CHECK_EQ(manager.world_contexts()[1].scene_id, 2U);
    CHECK(manager.gameplay_mode_scx() != nullptr);
  }

  TEST_CASE("Scene ID lookup is independent of the cache index") {
    const TempDirectory temp;
    write_bytes(temp.root() / "SCPTDATA" / "A.SCX", make_script_scx(1));
    write_bytes(temp.root() / "SCPTDATA" / "B.SCX", make_script_scx(1));
    write_bytes(temp.root() / "SCPTDATA" / "C.SCX", make_script_scx(1));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    REQUIRE(manager.load_world_context(42, std::nullopt, "SCPTDATA/A.SCX").has_value());
    REQUIRE(manager.load_world_context(7, std::nullopt, "SCPTDATA/B.SCX").has_value());
    CHECK(manager.find_world_context(42) == &manager.world_contexts()[0]);
    CHECK(manager.find_world_context(7) == &manager.world_contexts()[1]);

    REQUIRE(manager.unload_world_context(42).has_value());
    CHECK(manager.find_world_context(42) == nullptr);
    REQUIRE(manager.load_world_context(99, std::nullopt, "SCPTDATA/C.SCX").has_value());
    CHECK(manager.find_world_context(99) == &manager.world_contexts()[0]);
    CHECK(manager.find_world_context(7) == &manager.world_contexts()[1]);
  }

  TEST_CASE("Loading templates schedules zero scripts; sounds create zero voices") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    write_bytes(temp.root() / "SCPTDATA" / "Snd.SCX", make_sounds_scx(4));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);
    REQUIRE(manager.load_world_context(9, std::nullopt, "SCPTDATA/Snd.SCX").has_value());

    CHECK_EQ(manager.active_script_instances_total(), 0U);
    REQUIRE(manager.gameplay_mode_scx() != nullptr);
    CHECK_EQ(manager.gameplay_mode_scx()->scripts.size(), 3U);
    const App::Omikron::ScxData* sounds{manager.world_context_scx(9)};
    REQUIRE(sounds != nullptr);
    CHECK_EQ(sounds->sounds.size(), 4U);
    CHECK_EQ(manager.active_script_instances_total(), 0U);  // No implicit execution.
  }

  TEST_CASE("A missing world SCX leaves the gameplay-mode slot intact") {
    const TempDirectory temp;
    write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_script_scx(1));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    REQUIRE(manager.set_gameplay_mode(App::GameplayMode::Adventure).has_value());
    auto result{manager.load_world_context(0, std::nullopt, "SCPTDATA/GRID.SCX")};
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("SCPTDATA/GRID.SCX") != std::string::npos);
    CHECK(manager.gameplay_mode_scx() != nullptr);
    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::Free);
    CHECK(manager.world_contexts()[1].residency == App::WorldSceneResidencyState::Free);
  }

  TEST_CASE("A malformed world-context replacement preserves the current owner") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    write_bytes(temp.root() / "SCPTDATA" / "Bad.SCX", make_malformed_scx());
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);

    auto result{manager.load_world_context(5, std::nullopt, "SCPTDATA/Bad.SCX")};
    REQUIRE_FALSE(result.has_value());

    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::LoadedActive);
    CHECK_EQ(manager.world_contexts()[0].scenario_path, "SCPTDATA/GRID.SCX");
    CHECK(manager.world_contexts()[1].residency == App::WorldSceneResidencyState::Free);
    CHECK(manager.gameplay_mode_scx() != nullptr);
  }

  TEST_CASE("A malformed mode replacement preserves the current mode slot") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    write_bytes(temp.root() / "SCPTDATA" / "shoot2.scx", make_malformed_scx());
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);

    auto result{manager.set_gameplay_mode(App::GameplayMode::FirstPersonShooting)};
    REQUIRE_FALSE(result.has_value());

    CHECK(manager.current_gameplay_mode() == App::GameplayMode::Adventure);
    CHECK_EQ(manager.scenario_inventory().at(0).scenario_path, "SCPTDATA/aventure.scx");
    CHECK(manager.gameplay_mode_scx() != nullptr);
    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::LoadedActive);
  }

  TEST_CASE("SCPTDATA/GRID.SCX resolves an on-disk Grid.SCX fixture") {
    const TempDirectory temp;
    write_bytes(temp.root() / "SCPTDATA" / "Grid.SCX", make_script_scx(2));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    auto result{manager.load_world_context(0, std::nullopt, "SCPTDATA/GRID.SCX")};
    REQUIRE(result.has_value());
    CHECK(manager.world_contexts()[0].resolved_scenario_path.ends_with("Grid.SCX"));
  }

  TEST_CASE("Windows separators resolve to the same game asset") {
    const TempDirectory temp;
    write_bytes(temp.root() / "SCPTDATA" / "Grid.SCX", make_script_scx(2));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    auto result{manager.load_world_context(0, std::nullopt, "SCPTDATA\\GRID.SCX")};
    REQUIRE(result.has_value());
    CHECK(manager.world_contexts()[0].resolved_scenario_path.ends_with("Grid.SCX"));
  }

  TEST_CASE("Repeated load/deactivate/reactivate/recycle/unload stays consistent") {
    const TempDirectory temp;
    write_bytes(temp.root() / "SCPTDATA" / "A.SCX", make_script_scx(1));
    write_bytes(temp.root() / "SCPTDATA" / "B.SCX", make_script_scx(1));
    write_bytes(temp.root() / "SCPTDATA" / "C.SCX", make_script_scx(1));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    REQUIRE(manager.load_world_context(1, std::nullopt, "SCPTDATA/A.SCX").has_value());
    REQUIRE(manager.activate_world_context(1).has_value());
    REQUIRE(manager.deactivate_world_context(1).has_value());
    // Context 0 (inactive) stays resident; B claims the still-free context 1.
    REQUIRE(manager.load_world_context(2, std::nullopt, "SCPTDATA/B.SCX").has_value());
    CHECK(manager.find_world_context(1) != nullptr);
    REQUIRE(manager.activate_world_context(2).has_value());
    REQUIRE(manager.deactivate_world_context(2).has_value());
    // Both contexts are now inactive; loading C recycles the lowest-index
    // inactive entry (context 0), evicting scene 1.
    REQUIRE(manager.load_world_context(3, std::nullopt, "SCPTDATA/C.SCX").has_value());
    CHECK(manager.find_world_context(1) == nullptr);
    REQUIRE(manager.activate_world_context(3).has_value());
    REQUIRE(manager.deactivate_world_context(3).has_value());
    REQUIRE(manager.unload_world_context(3).has_value());
    REQUIRE(manager.unload_world_context(2).has_value());

    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::Free);
    CHECK(manager.world_contexts()[1].residency == App::WorldSceneResidencyState::Free);
    CHECK_EQ(manager.loaded_scenario_count(), 0U);
  }

  TEST_CASE("Fixture installs a gameplay runtime and one world runtime, context 1 has none") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);

    CHECK(manager.gameplay_runtime() != nullptr);
    CHECK(manager.world_runtime(0) != nullptr);
    CHECK(manager.world_runtime(1) == nullptr);
    CHECK_EQ(manager.active_script_instances_total(), 0U);  // All templates inactive.
  }

  TEST_CASE("Replacing the gameplay mode rebuilds the gameplay runtime, not the world runtime") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    write_bytes(temp.root() / "SCPTDATA" / "shoot2.scx", make_script_scx(2));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);

    App::ScenarioRuntime* gameplay_before{manager.gameplay_runtime()};
    App::ScenarioRuntime* world_before{manager.world_runtime(0)};
    REQUIRE(gameplay_before != nullptr);
    REQUIRE(world_before != nullptr);

    REQUIRE(manager.set_gameplay_mode(App::GameplayMode::FirstPersonShooting).has_value());

    CHECK(manager.gameplay_runtime() != gameplay_before);
    CHECK(manager.world_runtime(0) == world_before);  // World context untouched.
    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::LoadedActive);
  }

  TEST_CASE("World unload destroys the world runtime and advances the generation") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    initialize_grid_fixture(manager);

    REQUIRE(manager.world_runtime(0) != nullptr);
    const std::uint32_t generation_before{manager.world_contexts()[0].generation};

    REQUIRE(manager.deactivate_world_context(0).has_value());
    REQUIRE(manager.unload_world_context(0).has_value());

    CHECK(manager.world_runtime(0) == nullptr);
    CHECK(manager.world_contexts()[0].residency == App::WorldSceneResidencyState::Free);
    CHECK_EQ(manager.world_contexts()[0].generation, generation_before + 1U);
  }

  TEST_CASE("Recycling a LoadedInactive slot destroys the old runtime and creates a new one") {
    const TempDirectory temp;
    write_bytes(temp.root() / "SCPTDATA" / "A.SCX", make_script_scx(1));
    write_bytes(temp.root() / "SCPTDATA" / "B.SCX", make_script_scx(1));
    write_bytes(temp.root() / "SCPTDATA" / "C.SCX", make_script_scx(1));
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    REQUIRE(manager.load_world_context(1, std::nullopt, "SCPTDATA/A.SCX").has_value());
    App::ScenarioRuntime* old_runtime{manager.world_runtime(1)};
    REQUIRE(old_runtime != nullptr);

    REQUIRE(manager.load_world_context(2, std::nullopt, "SCPTDATA/B.SCX").has_value());
    // Both contexts resident; loading C recycles the lowest-index inactive
    // context 0 (scene 1), destroying its runtime.
    REQUIRE(manager.load_world_context(3, std::nullopt, "SCPTDATA/C.SCX").has_value());

    CHECK(manager.world_runtime(1) == nullptr);
    CHECK(manager.world_runtime(3) != nullptr);
    CHECK(manager.world_runtime(3) != old_runtime);
  }

  TEST_CASE("Debugger target context handles unavailable targets and explicit selection changes") {
    App::Debug::DebugRuntimeContext context;

    CHECK(context.refresh(nullptr));
    CHECK_FALSE(context.resolved().available());
    const std::uint64_t initial_epoch{context.selection_epoch()};
    CHECK_FALSE(context.refresh(nullptr));

    App::ScenarioManager manager;
    CHECK(context.refresh(&manager));
    CHECK_FALSE(context.resolved().available());
    CHECK_FALSE(context.resolved().identity.role.has_value());

    context.set_selected_target(App::Debug::DebugRuntimeTarget::k_gameplay_mode);
    CHECK(context.refresh(&manager));
    CHECK(context.selection_epoch() >= initial_epoch + 2U);
    CHECK(context.resolved().identity.requested ==
          App::Debug::DebugRuntimeTarget::k_gameplay_mode);
  }

  TEST_CASE("Debugger resolves gameplay, active world, explicit slots and Free residency") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};
    App::ScenarioManager manager;
    initialize_grid_fixture(manager);

    const auto gameplay{App::Debug::DebugRuntimeContext::resolve(
        App::Debug::DebugRuntimeTarget::k_gameplay_mode, &manager)};
    CHECK(gameplay.available());
    CHECK(gameplay.identity.role == App::ScenarioRole::GameplayMode);
    CHECK(gameplay.gameplay_mode == App::GameplayMode::Adventure);
    CHECK_EQ(gameplay.scenario_path, "SCPTDATA/aventure.scx");

    const auto active{App::Debug::DebugRuntimeContext::resolve(
        App::Debug::DebugRuntimeTarget::k_active_world, &manager)};
    CHECK(active.available());
    CHECK(active.identity.role == App::ScenarioRole::WorldScene);
    CHECK_EQ(active.identity.slot.value_or(99U), 0U);
    CHECK_EQ(active.identity.scene_id, 0U);
    CHECK(active.residency == App::WorldSceneResidencyState::LoadedActive);

    const auto free_slot{App::Debug::DebugRuntimeContext::resolve(
        App::Debug::DebugRuntimeTarget::k_world_slot_1, &manager)};
    CHECK_FALSE(free_slot.available());
    CHECK_EQ(free_slot.identity.slot.value_or(99U), 1U);
    CHECK(free_slot.residency == App::WorldSceneResidencyState::Free);
  }

  TEST_CASE("Debugger epoch follows active-slot and generation identity changes") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    write_bytes(temp.root() / "SCPTDATA" / "B.SCX", make_script_scx(1));
    write_bytes(temp.root() / "SCPTDATA" / "C.SCX", make_script_scx(1));
    const ScopedGameDataRoot root{temp.root()};
    App::ScenarioManager manager;
    initialize_grid_fixture(manager);
    REQUIRE(manager.load_world_context(2, std::nullopt, "SCPTDATA/B.SCX").has_value());

    App::Debug::DebugRuntimeContext context;
    CHECK(context.refresh(&manager));
    const std::uint64_t slot_zero_epoch{context.selection_epoch()};
    CHECK_EQ(context.resolved().identity.slot.value_or(99U), 0U);

    REQUIRE(manager.deactivate_world_context(0).has_value());
    REQUIRE(manager.activate_world_context(2).has_value());
    CHECK(context.refresh(&manager));
    CHECK_EQ(context.resolved().identity.slot.value_or(99U), 1U);
    CHECK_EQ(context.selection_epoch(), slot_zero_epoch + 1U);

    context.set_selected_target(App::Debug::DebugRuntimeTarget::k_world_slot_0);
    CHECK(context.refresh(&manager));
    const std::uint64_t before_recycle_epoch{context.selection_epoch()};
    const std::uint32_t before_recycle_generation{context.resolved().identity.generation};
    REQUIRE(manager.unload_world_context(0).has_value());
    REQUIRE(manager.load_world_context(3, std::nullopt, "SCPTDATA/C.SCX").has_value());
    CHECK(context.refresh(&manager));
    CHECK(context.resolved().identity.generation > before_recycle_generation);
    CHECK_EQ(context.selection_epoch(), before_recycle_epoch + 1U);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

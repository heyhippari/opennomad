#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

#include <SDL3/SDL_stdinc.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Scenario/ScenarioEngine.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"
#include "OmikronTestBuffer.hpp"

namespace {

constexpr std::uint32_t K_SCX_MAGIC{0x00DEAD00U};
constexpr std::uint32_t K_END_TAG{0xDEADFFFFU};

using App::WorldSceneResidencyState;
using App::Script::AreaScriptState;

void write_u16(std::vector<std::byte>& data, const std::size_t offset, const std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u32(std::vector<std::byte>& data, const std::size_t offset, const std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_name(
    std::vector<std::byte>& data, const std::size_t offset, const std::string_view name) {
  for (std::size_t index{0}; index < 9U; ++index) {
    data[offset + index] = index < name.size() ? static_cast<std::byte>(name[index]) : std::byte{};
  }
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& data) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream{path, std::ios::binary};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  stream.write(
      reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

/// The confirmed area-118 startup prefix bytes, extended with the music 87
/// instruction that follows interface 29 (the milestone's resume target).
std::vector<std::byte> make_prefix() {
  Buffer bytes;
  bytes.u8(0x0D).u16(175);
  bytes.u8(0x0E).u16(170).u8(50);
  bytes.u8(0x38).u16(136);
  bytes.u8(0x4F).u16(0xFFFF);
  bytes.u8(0x68);
  bytes.u8(0x5C).u16(997);
  bytes.u8(0x83).u16(0).u16(1);
  bytes.u8(0x67).u16(109).u16(1).u16(1);
  bytes.u8(0x76).u32(0).u16(0).u16(0);
  bytes.u8(0x46).u16(29).u16(0xFFFF).u16(19);
  bytes.u8(0x67).u16(87).u16(1).u16(1);
  return bytes.data();
}

std::vector<std::byte> make_start() {
  std::vector<std::byte> data(0x58A, std::byte{});
  write_u32(data, 0x0C, 0x10);
  write_u16(data, 0x586, 118);
  write_u16(data, 0x588, 0xFFFF);  // -1
  return data;
}

std::vector<std::byte> make_area_archive(const std::vector<std::byte>& prefix) {
  constexpr std::size_t k_record_offset{0x800};
  constexpr std::uint32_t k_record_size{0x9C0};

  std::vector<std::byte> data(k_record_offset + k_record_size, std::byte{});
  const std::size_t entry{118U * 8U};
  write_u32(data, entry, static_cast<std::uint32_t>(k_record_offset));
  write_u32(data, entry + 4U, k_record_size);

  write_u32(data, k_record_offset + 0x04, 0x3FC);
  write_name(data, k_record_offset + 0x58, "GRID");
  write_name(data, k_record_offset + 0x61, "GRID");
  std::memcpy(data.data() + k_record_offset + 0x3FC, prefix.data(), prefix.size());
  return data;
}

std::vector<std::byte> make_minimal_scx() {
  Buffer bytes;
  bytes.u32(K_SCX_MAGIC).u32(5).u32(8).u32(4).u32(K_END_TAG);
  return bytes.data();
}

/// Minimal valid header-only .3DO (OD3X + version 4, all sections empty),
/// accepted by Model3DO::load without game data.
std::vector<std::byte> make_minimal_3do() {
  Buffer bytes;
  bytes.chars("OD3X", 4).u32(4).u32(0)
      .u32(372).u32(372).u32(372).u32(372).u32(372)
      .u32(0).u32(0).u32(372)
      .zeros(28).u32(0)
      .zeros(132).u32(0)
      .zeros(12)
      .u32(0).u32(0)
      .u32(0).u32(0).u32(0)
      .u64(0)
      .u32(0).u32(0).u32(0)
      .u32(0).u32(0).u32(0)
      .u32(0).u32(0).u32(0)
      .zeros(84);
  return bytes.data();
}

class TempDirectory {
 public:
  TempDirectory() : m_root{std::filesystem::temp_directory_path() / "opennomad-engine-test"} {
    std::filesystem::remove_all(m_root);
    std::filesystem::create_directories(m_root);
  }

  ~TempDirectory() {
    std::filesystem::remove_all(m_root);
  }

  TempDirectory(const TempDirectory&) = delete;
  TempDirectory(TempDirectory&&) = delete;
  TempDirectory& operator=(const TempDirectory&) = delete;
  TempDirectory& operator=(TempDirectory&&) = delete;

  [[nodiscard]] const std::filesystem::path& root() const {
    return m_root;
  }

 private:
  std::filesystem::path m_root;
};

class ScopedGameDataRoot {
 public:
  explicit ScopedGameDataRoot(const std::filesystem::path& root) {
    static_cast<void>(SDL_SetEnvironmentVariable(
        SDL_GetEnvironment(), "OPENNOMAD_GAME_DATA_ROOT", root.string().c_str(), true));
  }

  ~ScopedGameDataRoot() {
    static_cast<void>(
        SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "OPENNOMAD_GAME_DATA_ROOT"));
  }

  ScopedGameDataRoot(const ScopedGameDataRoot&) = delete;
  ScopedGameDataRoot(ScopedGameDataRoot&&) = delete;
  ScopedGameDataRoot& operator=(const ScopedGameDataRoot&) = delete;
  ScopedGameDataRoot& operator=(ScopedGameDataRoot&&) = delete;
};

void write_boot_fixtures(const TempDirectory& temp) {
  write_bytes(temp.root() / "IAM" / "START", make_start());
  write_bytes(temp.root() / "IAM" / "AREA", make_area_archive(make_prefix()));
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
}

std::optional<std::uint32_t> seq_of(
    const App::Startup::StartupTraceRecorder& recorder,
    const std::string_view name,
    const std::string_view detail = {}) {
  for (const auto& event : recorder.events()) {
    if (event.name == name && (detail.empty() || event.detail.find(detail) != std::string::npos)) {
      return event.sequence;
    }
  }
  return std::nullopt;
}

}  // namespace

TEST_SUITE("Core::Scenario::ScenarioEngine") {
  TEST_CASE("mode order reproduces the recovered startup sequence and reaches the menu") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    App::Startup::StartupTraceRecorder recorder;
    App::ScenarioManager manager;
    App::ScenarioEngine engine{manager, recorder};
    engine.dispatcher().set_interface_open_sink(
        [](const App::InterfaceOpenRequest& request)
            -> std::expected<App::InterfaceHandle, std::string> {
          return App::InterfaceHandle{.interface_id = request.interface_id, .generation = 1};
        });

    REQUIRE(engine.select_permanent_mode_script().has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_teardown, 0).has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_new_session, 0).has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_tick, 0).has_value());

    // State invariants.
    CHECK_EQ(engine.initial_area_id(), 118);
    CHECK_EQ(engine.linked_area_id(), -1);
    CHECK_EQ(engine.area_mapping(118), std::optional<std::int32_t>{-1});
    CHECK(engine.dispatcher().main_menu_active());
    REQUIRE(engine.area_script() != nullptr);
    CHECK(engine.area_script()->state() == AreaScriptState::k_waiting);
    CHECK_EQ(engine.area_script()->wait_state(), 6U);

    CHECK_EQ(manager.world_contexts()[0].scene_id, 0U);
    CHECK_EQ(manager.world_contexts()[0].residency, WorldSceneResidencyState::LoadedActive);
    CHECK_EQ(manager.world_contexts()[0].scenario_path, "SCPTDATA/GRID.SCX");
    CHECK(manager.world_contexts()[1].residency == WorldSceneResidencyState::Free);
    CHECK(manager.gameplay_mode_scx() != nullptr);

    // Ordered trace subsequence.
    const auto ordered = [&recorder](const std::string_view before,
                               const std::string_view before_detail,
                               const std::string_view after,
                               const std::string_view after_detail) {
      const std::optional<std::uint32_t> first{seq_of(recorder, before, before_detail)};
      const std::optional<std::uint32_t> second{seq_of(recorder, after, after_detail)};
      REQUIRE(first.has_value());
      REQUIRE(second.has_value());
      CHECK_LT(first.value(), second.value());
    };

    ordered("ModeScript.Aventure.Selected", {}, "PreliminaryInterface29.Closed", {});
    ordered("PreliminaryInterface29.Closed", {}, "ScenarioMode3.Complete", {});
    ordered("ScenarioMode3.Complete", {}, "ModeScript.Aventure.Reselected", {});
    ordered("ModeScript.Aventure.Reselected", {}, "ScenarioMode2.Begin", {});
    ordered("ScenarioState.Cleared", {}, "IAM_START.Loaded", {});
    ordered("IAM_START.Loaded", {}, "IAM_START.InitialArea", "id=118 linked=-1");
    ordered("IAM_START.InitialArea", {}, "IAM_AREA.RecordLoaded", "id=118");
    ordered("IAM_AREA.RecordLoaded", {}, "IAM_AREA.Parsed", "scriptOffset=0x3fc");
    ordered("AreaDependency.GRID_3DO.Failed", {}, "AreaDependency.GRID_SCX.Loaded", {});
    ordered("AreaDependency.GRID_SCX.Loaded", {}, "AreaContext.Created", "area=118");
    ordered("AreaContext.Created", {}, "AreaContext.EventQueued", "event=1");
    ordered("AreaContext.EventQueued", {}, "AreaContext.Activated", {});
    ordered("AreaContext.Activated", {}, "ScenarioMode2.Complete", {});
    ordered("ScenarioMode2.Complete", {}, "ScenarioMode1.Begin", {});
    ordered("AreaScript.EventStarted", {}, "AreaScript.VariableSet", "index=175 value=1");
    ordered("AreaScript.VariableSet", "index=175 value=1", "AreaScript.VariableSet",
        "index=170 value=50");
    ordered("AreaScript.VariableSet", "index=170 value=50", "AreaScript.BootstrapOpcode",
        "opcode=0x38");
    ordered("AreaScript.BootstrapOpcode", "opcode=0x76", "Interface.OpenRequested",
        "id=29 arg2=-1 arg3=19");
    ordered("Interface.OpenRequested", {}, "MainMenu.Active", {});
    ordered("MainMenu.Active", {}, "AreaContext.Waiting", "state=6");
    ordered("AreaContext.Waiting", {}, "ScenarioMode1.Complete", {});
  }

  TEST_CASE("a missing mandatory dependency stops mode 2 before area activation") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    // Remove GRID.SCX: the mandatory world dependency is now unavailable.
    std::filesystem::remove(temp.root() / "SCPTDATA" / "GRID.SCX");
    const ScopedGameDataRoot root{temp.root()};

    App::Startup::StartupTraceRecorder recorder;
    App::ScenarioManager manager;
    App::ScenarioEngine engine{manager, recorder};

    REQUIRE(engine.select_permanent_mode_script().has_value());
    const auto result{engine.enter_mode(App::ScenarioMode::k_new_session, 0)};
    REQUIRE_FALSE(result.has_value());
    CHECK_FALSE(seq_of(recorder, "AreaContext.Created").has_value());
    CHECK_FALSE(seq_of(recorder, "ScenarioMode2.Complete").has_value());
    CHECK_FALSE(engine.dispatcher().main_menu_active());
  }

  TEST_CASE("an unknown opcode pauses the area script without opening the menu") {
    const TempDirectory temp;
    write_bytes(temp.root() / "IAM" / "START", make_start());
    // A single unknown opcode byte as the area script prefix.
    std::vector<std::byte> unknown_prefix{std::byte{0xFF}};
    write_bytes(temp.root() / "IAM" / "AREA", make_area_archive(unknown_prefix));
    write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
    write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
    const ScopedGameDataRoot root{temp.root()};

    App::Startup::StartupTraceRecorder recorder;
    App::ScenarioManager manager;
    App::ScenarioEngine engine{manager, recorder};

    REQUIRE(engine.select_permanent_mode_script().has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_new_session, 0).has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_tick, 0).has_value());

    REQUIRE(engine.area_script() != nullptr);
    CHECK(engine.area_script()->state() == AreaScriptState::k_paused_unsupported);
    CHECK_FALSE(engine.dispatcher().main_menu_active());
  }

  TEST_CASE("GRID.3DO present is parsed before GRID.SCX and startup reaches the menu") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    write_bytes(temp.root() / "MESHES" / "DECORS" / "GRID.3DO", make_minimal_3do());
    const ScopedGameDataRoot root{temp.root()};

    App::Startup::StartupTraceRecorder recorder;
    App::ScenarioManager manager;
    App::ScenarioEngine engine{manager, recorder};
    engine.dispatcher().set_interface_open_sink(
        [](const App::InterfaceOpenRequest& request)
            -> std::expected<App::InterfaceHandle, std::string> {
          return App::InterfaceHandle{.interface_id = request.interface_id, .generation = 1};
        });

    REQUIRE(engine.select_permanent_mode_script().has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_teardown, 0).has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_new_session, 0).has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_tick, 0).has_value());

    CHECK(engine.dispatcher().main_menu_active());
    const std::optional<std::uint32_t> grid_3do{
        seq_of(recorder, "AreaDependency.GRID_3DO.Loaded")};
    const std::optional<std::uint32_t> grid_scx{
        seq_of(recorder, "AreaDependency.GRID_SCX.Loaded")};
    REQUIRE(grid_3do.has_value());
    REQUIRE(grid_scx.has_value());
    CHECK_LT(grid_3do.value(), grid_scx.value());
  }

  TEST_CASE("music 109 plays, interface 29 suspends the script, completion resumes 87") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    App::Startup::StartupTraceRecorder recorder;
    App::ScenarioManager manager;
    App::ScenarioEngine engine{manager, recorder};
    engine.dispatcher().set_interface_open_sink(
        [](const App::InterfaceOpenRequest& request)
            -> std::expected<App::InterfaceHandle, std::string> {
          return App::InterfaceHandle{.interface_id = request.interface_id, .generation = 1};
        });

    REQUIRE(engine.select_permanent_mode_script().has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_teardown, 0).has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_new_session, 0).has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_tick, 0).has_value());

    REQUIRE(engine.area_script() != nullptr);
    CHECK(engine.area_script()->state() == AreaScriptState::k_waiting);
    CHECK(engine.dispatcher().main_menu_active());

    // Track 109 was requested before the interface opened; no 87 yet.
    REQUIRE(seq_of(recorder, "Music.TrackRequested", "track=109").has_value());
    REQUIRE(seq_of(recorder, "Interface.OpenRequested", "id=29").has_value());
    CHECK_FALSE(seq_of(recorder, "Music.TrackRequested", "track=87").has_value());

    // Later per-frame updates while waiting keep the script suspended and do
    // not request 87.
    REQUIRE(engine.update().has_value());
    REQUIRE(engine.update().has_value());
    CHECK(engine.area_script()->state() == AreaScriptState::k_waiting);
    CHECK_FALSE(seq_of(recorder, "Music.TrackRequested", "track=87").has_value());

    // New Game completes interface 29 with the active handle.
    const std::optional<App::InterfaceHandle> handle{engine.dispatcher().active_handle()};
    REQUIRE(handle.has_value());
    engine.notify_interface_completion(App::InterfaceCompletion{.handle = *handle, .result = 0});
    CHECK_FALSE(engine.dispatcher().main_menu_active());

    // The next per-frame update resumes the script and requests 87.
    REQUIRE(engine.update().has_value());
    CHECK(seq_of(recorder, "Music.TrackRequested", "track=87").has_value());

    // GRID world context 0 stays resident and active throughout.
    CHECK_EQ(manager.world_contexts()[0].scene_id, 0U);
    CHECK_EQ(manager.world_contexts()[0].residency, WorldSceneResidencyState::LoadedActive);
    CHECK_EQ(manager.world_contexts()[0].scenario_path, "SCPTDATA/GRID.SCX");
    CHECK(manager.gameplay_mode_scx() != nullptr);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

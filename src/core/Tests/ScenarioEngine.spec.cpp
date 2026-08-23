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

std::vector<std::byte> make_transition_area_archive(const std::vector<std::byte>& source_prefix) {
  constexpr std::size_t k_source_offset{0x800};
  constexpr std::uint32_t k_source_size{0x9C0};
  constexpr std::size_t k_target_offset{0x1200};
  constexpr std::uint32_t k_target_size{0x300};

  std::vector<std::byte> data(k_target_offset + k_target_size, std::byte{});
  const std::size_t source_entry{118U * 8U};
  write_u32(data, source_entry, static_cast<std::uint32_t>(k_source_offset));
  write_u32(data, source_entry + 4U, k_source_size);
  write_u32(data, k_source_offset + 0x04, 0x3FC);
  write_name(data, k_source_offset + 0x58, "GRID");
  write_name(data, k_source_offset + 0x61, "GRID");
  std::memcpy(data.data() + k_source_offset + 0x3FC, source_prefix.data(), source_prefix.size());

  const std::size_t target_entry{222U * 8U};
  write_u32(data, target_entry, static_cast<std::uint32_t>(k_target_offset));
  write_u32(data, target_entry + 4U, k_target_size);
  write_u32(data, k_target_offset + 0x04, 0x100);
  write_name(data, k_target_offset + 0x58, "AIMPASSE");
  write_name(data, k_target_offset + 0x61, "IMPASSE");
  write_name(data, k_target_offset + 0x85, "ASKY");
  data.at(k_target_offset + 0x100) = std::byte{0x03};
  return data;
}

std::vector<std::byte> make_minimal_scx() {
  Buffer bytes;
  bytes.u32(K_SCX_MAGIC).u32(5).u32(8).u32(4).u32(K_END_TAG);
  return bytes.data();
}

/// Minimal IAM/DIALOG archive with record 0. When action_choice is true the
/// one visible response owns non-empty action bytecode, allowing the test to
/// drive DialogRuntime into a post-start failure without private hooks.
std::vector<std::byte> make_dialog_archive(const bool action_choice = false) {
  Buffer record;
  record.u16(310).u16(1).u16(0).u16(0);
  record.zeros(0x10U);  // Four condition offsets.
  if (action_choice) {
    record.u32(0x58U).zeros(0x0CU);
  } else {
    record.zeros(0x10U);
  }
  record.u32(0x48U);
  record.u16(0xFFFFU).u16(0xFFFFU).u16(0xFFFFU).u16(0xFFFFU);
  record.u16(0).chars("FACE", 10);
  record.u16(0xFFFFU).u16(0xFFFFU).u16(0xFFFFU).u16(0xFFFFU);
  if (action_choice) {
    record.chars("Line", 5).chars("Choice", 7).zeros(4U).u8(0x03);
  } else {
    record.chars("Session dialog", 15).zeros(5U);
  }

  Buffer archive;
  archive.u32(0x800U).u32(static_cast<std::uint32_t>(record.data().size())).zeros(0x7F8U);
  for (const std::byte byte : record.data()) {
    archive.u8(std::to_integer<std::uint8_t>(byte));
  }
  return archive.data();
}

/// Minimal valid header-only .3DO (OD3X + version 4, all sections empty),
/// accepted by Model3DO::load without game data.
std::vector<std::byte> make_minimal_3do() {
  Buffer bytes;
  bytes.chars("OD3X", 4).u32(4).u32(0x2C)
      .u32(416).u32(416).u32(416).u32(416).u32(416)
      .u32(0).u32(0).u32(416)
      .zeros(72).u32(0)
      .zeros(104).u32(0).zeros(24).u32(0)
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

void write_dialog_boot_fixtures(const TempDirectory& temp, const bool action_choice = false) {
  Buffer script;
  script.u8(0x3D).u16(0).u8(0x68).u8(0x03);
  write_bytes(temp.root() / "IAM" / "START", make_start());
  write_bytes(temp.root() / "IAM" / "AREA", make_area_archive(script.data()));
  write_bytes(temp.root() / "IAM" / "DIALOG", make_dialog_archive(action_choice));
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
}

void write_transition_boot_fixtures(const TempDirectory& temp, const bool include_target_scx) {
  Buffer script;
  script.u8(0x2F).u16(222).u16(0xFFFF).u16(0xFFFF);
  script.u8(0x47).u16(222).u16(55);
  write_bytes(temp.root() / "IAM" / "START", make_start());
  write_bytes(
      temp.root() / "IAM" / "AREA", make_transition_area_archive(script.data()));
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
  if (include_target_scx) {
    write_bytes(temp.root() / "SCPTDATA" / "IMPASSE.SCX", make_minimal_scx());
  }
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
    CHECK(engine.main_menu_active());
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
    ordered("AreaDependency.Decor.Failed", {}, "AreaDependency.Scenario.Loaded", {});
    ordered("AreaDependency.Scenario.Loaded", {}, "AreaContext.Created", "area=118");
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
    CHECK_FALSE(engine.main_menu_active());
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
    CHECK_FALSE(engine.main_menu_active());
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

    CHECK(engine.main_menu_active());
    const std::optional<std::uint32_t> decor{
        seq_of(recorder, "AreaDependency.Decor.Loaded")};
    const std::optional<std::uint32_t> scenario{
        seq_of(recorder, "AreaDependency.Scenario.Loaded")};
    REQUIRE(decor.has_value());
    REQUIRE(scenario.has_value());
    CHECK_LT(decor.value(), scenario.value());
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
    CHECK(engine.main_menu_active());

    // Track 109 was requested before the interface opened; no 87 yet.
    REQUIRE(seq_of(recorder, "Music.TrackRequested", "track=109").has_value());
    REQUIRE(seq_of(recorder, "Interface.OpenRequested", "id=29").has_value());
    CHECK_FALSE(seq_of(recorder, "Music.TrackRequested", "track=87").has_value());

    // Later per-frame updates while waiting keep the script suspended and do
    // not request 87.
    REQUIRE(engine.update(1.0F / 30.0F).has_value());
    REQUIRE(engine.update(1.0F / 30.0F).has_value());
    CHECK(engine.area_script()->state() == AreaScriptState::k_waiting);
    CHECK_FALSE(seq_of(recorder, "Music.TrackRequested", "track=87").has_value());

    // New Game completes interface 29 with the active handle.
    const std::optional<App::InterfaceHandle> handle{engine.active_handle()};
    REQUIRE(handle.has_value());
    engine.notify_interface_completion(App::InterfaceCompletion{.handle = *handle, .result = 0});
    CHECK_FALSE(engine.main_menu_active());

    // The next per-frame update resumes the script and requests 87.
    REQUIRE(engine.update(1.0F / 30.0F).has_value());
    CHECK(seq_of(recorder, "Music.TrackRequested", "track=87").has_value());

    // GRID world context 0 stays resident and active throughout.
    CHECK_EQ(manager.world_contexts()[0].scene_id, 0U);
    CHECK_EQ(manager.world_contexts()[0].residency, WorldSceneResidencyState::LoadedActive);
    CHECK_EQ(manager.world_contexts()[0].scenario_path, "SCPTDATA/GRID.SCX");
    CHECK(manager.gameplay_mode_scx() != nullptr);
  }

  TEST_CASE("the scheduler advances gameplay and active world runtimes scene-independently") {
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

    // No Scene is involved: the scheduler drives the AREA script and the
    // slot-owned runtimes directly through ScenarioManager.
    CHECK(manager.gameplay_runtime() != nullptr);
    CHECK(manager.world_runtime(0) != nullptr);
    CHECK(manager.world_runtime(1) == nullptr);  // Inactive world has no runtime.

    REQUIRE(engine.update(1.0F / 30.0F).has_value());
    REQUIRE(engine.update(1.0F / 30.0F).has_value());
    CHECK_EQ(manager.world_contexts()[0].residency, WorldSceneResidencyState::LoadedActive);
  }

  TEST_CASE("AREA-started dialog takeover gates both tick paths and resumes the advanced IP") {
    const TempDirectory temp;
    write_dialog_boot_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    App::Startup::StartupTraceRecorder recorder;
    App::ScenarioManager manager;
    App::ScenarioEngine engine{manager, recorder};

    REQUIRE(engine.select_permanent_mode_script().has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_new_session, 0).has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_tick, 0).has_value());

    REQUIRE(engine.area_script() != nullptr);
    CHECK(engine.dialog_takeover_active());
    CHECK_EQ(engine.dialog_takeover_id(), std::optional<std::int16_t>{0});
    CHECK(manager.dialog_runtime().active());
    CHECK(engine.area_script()->state() == AreaScriptState::k_running);
    CHECK_EQ(engine.area_script()->runtime_state(), 1U);
    CHECK_EQ(engine.area_script()->instruction_pointer(), 3U);
    CHECK(engine.area_script()->last_run_yielded());
    CHECK(engine.area_script()->wait_info().kind == App::Script::AreaWaitKind::k_none);

    // Both the explicit mode-1 route and the per-frame update route use the
    // same gate. Neither may execute the following 0x68 while dialog is active.
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_tick, 0).has_value());
    REQUIRE(engine.update(1.0F / 30.0F).has_value());
    REQUIRE(engine.update(1.0F / 30.0F).has_value());
    CHECK_EQ(engine.area_script()->instruction_pointer(), 3U);
    REQUIRE_EQ(engine.area_script()->trace().size(), 1U);

    REQUIRE(manager.dialog_runtime().acknowledge_line().has_value());
    CHECK(manager.dialog_runtime().completed());

    // Completion is consumed at the start of the next update. The same AREA
    // context resumes at +3, executes 0x68, then reaches its event terminator;
    // 0x3D is not dispatched a second time.
    REQUIRE(engine.update(1.0F / 30.0F).has_value());
    CHECK_FALSE(engine.dialog_takeover_active());
    CHECK_FALSE(manager.dialog_runtime().active());
    CHECK_FALSE(manager.dialog_runtime().completed());
    CHECK(engine.area_script()->state() == AreaScriptState::k_ready);
    CHECK_EQ(engine.area_script()->instruction_pointer(), 5U);
    REQUIRE_EQ(engine.area_script()->trace().size(), 3U);
    CHECK_EQ(engine.area_script()->trace().at(0).opcode, 0x3DU);
    CHECK_EQ(engine.area_script()->trace().at(1).opcode, 0x68U);
    CHECK_EQ(engine.area_script()->trace().at(2).opcode, 0x03U);
    CHECK(seq_of(recorder, "DialogTakeover.Entered", "id=0").has_value());
    CHECK(seq_of(recorder, "DialogTakeover.Completed", "id=0").has_value());
    CHECK(seq_of(recorder, "AreaScript.ResumedAfterDialog", "ip=0x3").has_value());
  }

  TEST_CASE("a dialog failure during takeover remains fatal and keeps AREA stopped") {
    const TempDirectory temp;
    write_dialog_boot_fixtures(temp, true);
    const ScopedGameDataRoot root{temp.root()};

    App::Startup::StartupTraceRecorder recorder;
    App::ScenarioManager manager;
    App::ScenarioEngine engine{manager, recorder};

    REQUIRE(engine.select_permanent_mode_script().has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_new_session, 0).has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_tick, 0).has_value());
    REQUIRE(engine.area_script() != nullptr);
    CHECK_EQ(engine.area_script()->instruction_pointer(), 3U);

    REQUIRE(manager.dialog_runtime().acknowledge_line().has_value());
    REQUIRE_FALSE(manager.dialog_runtime().select_choice(0).has_value());
    const auto update{engine.update(1.0F / 30.0F)};
    REQUIRE_FALSE(update.has_value());
    CHECK(update.error().find("dialog takeover failed") != std::string::npos);
    CHECK(update.error().find("action bytecode execution is unsupported") != std::string::npos);
    CHECK(engine.dialog_takeover_active());
    CHECK_EQ(engine.area_script()->instruction_pointer(), 3U);
    REQUIRE_EQ(engine.area_script()->trace().size(), 1U);
  }

  TEST_CASE("0x2F transaction commits the alternate AREA/world slot and resumes at 0x47") {
    const TempDirectory temp;
    write_transition_boot_fixtures(temp, true);
    const ScopedGameDataRoot root{temp.root()};

    App::Startup::StartupTraceRecorder recorder;
    App::ScenarioManager manager;
    App::ScenarioEngine engine{manager, recorder};
    REQUIRE(engine.select_permanent_mode_script().has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_new_session, 0).has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_tick, 0).has_value());

    REQUIRE(engine.area_script() != nullptr);
    CHECK(engine.area_transition_pending());
    CHECK_EQ(engine.active_area_slot(), 0U);
    CHECK_EQ(engine.active_area_id(), 118);
    CHECK(engine.area_script()->state() == AreaScriptState::k_waiting);
    CHECK_EQ(engine.area_script()->runtime_state(), 10U);
    CHECK_EQ(engine.area_script()->instruction_pointer(), 7U);
    CHECK(engine.area_script()->wait_info().kind == App::Script::AreaWaitKind::k_area_transition);
    REQUIRE(engine.runtime_area_slot(0) != nullptr);
    REQUIRE(engine.runtime_area_slot(1) != nullptr);
    CHECK_EQ(engine.runtime_area_slot(0)->primary_area_id, 118);
    CHECK_FALSE(engine.runtime_area_slot(1)->primary.has_value());
    CHECK(manager.world_contexts()[0].residency == WorldSceneResidencyState::LoadedActive);
    CHECK(manager.world_contexts()[1].residency == WorldSceneResidencyState::Free);

    // The next scheduler tick prepares the destination transactionally,
    // commits the residency swap, completes generation 1 exactly once, then
    // resumes the old AREA 118 context at its post-0x2F successor.
    REQUIRE(engine.update(1.0F / 30.0F).has_value());
    CHECK_FALSE(engine.area_transition_pending());
    CHECK_EQ(engine.active_area_slot(), 1U);
    CHECK_EQ(engine.active_area_id(), 222);
    REQUIRE(engine.runtime_area_slot(0)->primary.has_value());
    REQUIRE(engine.runtime_area_slot(1)->primary.has_value());
    CHECK_EQ(engine.runtime_area_slot(0)->primary_area_id, 118);
    CHECK_EQ(engine.runtime_area_slot(1)->primary_area_id, 222);
    CHECK_EQ(engine.runtime_area_slot(1)->primary->model3do_name(), "AIMPASSE");
    CHECK_EQ(engine.runtime_area_slot(1)->primary->scenario_scx_name(), "IMPASSE");
    CHECK_EQ(engine.runtime_area_slot(1)->primary->sky_3do_name(), "ASKY");
    CHECK(manager.world_contexts()[0].residency == WorldSceneResidencyState::LoadedInactive);
    CHECK_EQ(manager.world_contexts()[0].scenario_path, "SCPTDATA/GRID.SCX");
    CHECK(manager.world_contexts()[1].residency == WorldSceneResidencyState::LoadedActive);
    CHECK_EQ(manager.world_contexts()[1].scenario_path, "SCPTDATA/IMPASSE.SCX");
    REQUIRE(manager.active_world_context() != nullptr);
    CHECK_EQ(manager.active_world_context()->scene_id, 1U);

    CHECK(engine.area_script()->state() == AreaScriptState::k_paused_unsupported);
    CHECK_EQ(engine.area_script()->instruction_pointer(), 7U);
    CHECK_EQ(engine.area_script()->pause_info().opcode, 0x47U);
    CHECK(seq_of(recorder, "AreaTransition.Accepted", "target=222").has_value());
    CHECK(seq_of(recorder, "AreaTransition.TargetPrepared", "model='AIMPASSE'").has_value());
    CHECK(seq_of(recorder, "AreaTransition.Committed", "resumeIp=0x7").has_value());

    const std::size_t trace_size{engine.area_script()->trace().size()};
    REQUIRE(engine.update(1.0F / 30.0F).has_value());
    CHECK_EQ(engine.area_script()->trace().size(), trace_size);
  }

  TEST_CASE("failed 0x2F target preparation preserves the source AREA and active world") {
    const TempDirectory temp;
    write_transition_boot_fixtures(temp, false);
    const ScopedGameDataRoot root{temp.root()};

    App::Startup::StartupTraceRecorder recorder;
    App::ScenarioManager manager;
    App::ScenarioEngine engine{manager, recorder};
    REQUIRE(engine.select_permanent_mode_script().has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_new_session, 0).has_value());
    REQUIRE(engine.enter_mode(App::ScenarioMode::k_tick, 0).has_value());

    REQUIRE(engine.area_script() != nullptr);
    CHECK(engine.area_transition_pending());
    CHECK_EQ(engine.area_script()->instruction_pointer(), 7U);

    const auto update{engine.update(1.0F / 30.0F)};
    REQUIRE_FALSE(update.has_value());
    CHECK(update.error().find("AREA transition to 222 failed") != std::string::npos);
    CHECK(update.error().find("IMPASSE.SCX") != std::string::npos);
    CHECK(engine.area_transition_pending());
    CHECK_EQ(engine.active_area_slot(), 0U);
    CHECK_EQ(engine.active_area_id(), 118);
    REQUIRE(engine.runtime_area_slot(0) != nullptr);
    REQUIRE(engine.runtime_area_slot(1) != nullptr);
    CHECK_EQ(engine.runtime_area_slot(0)->primary_area_id, 118);
    CHECK_FALSE(engine.runtime_area_slot(1)->primary.has_value());
    CHECK(manager.world_contexts()[0].residency == WorldSceneResidencyState::LoadedActive);
    CHECK_EQ(manager.world_contexts()[0].scenario_path, "SCPTDATA/GRID.SCX");
    CHECK(manager.world_contexts()[1].residency == WorldSceneResidencyState::Free);
    CHECK(engine.area_script()->state() == AreaScriptState::k_waiting);
    CHECK_EQ(engine.area_script()->runtime_state(), 10U);
    CHECK_EQ(engine.area_script()->instruction_pointer(), 7U);

    // A failed coordinator is sticky: later ticks surface the same failure
    // without redispatching 0x2F or mutating the source.
    const auto retry{engine.update(1.0F / 30.0F)};
    REQUIRE_FALSE(retry.has_value());
    CHECK_EQ(retry.error(), update.error());
    REQUIRE_EQ(engine.area_script()->trace().size(), 1U);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

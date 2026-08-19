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

#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioStartupController.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
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

/// The confirmed area-118 startup prefix bytes.
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
  return bytes.data();
}

/// IAM/START selecting initial area 118 with linked area -1.
std::vector<std::byte> make_start() {
  std::vector<std::byte> data(0x58A, std::byte{});
  write_u32(data, 0x0C, 0x10);
  write_u16(data, 0x586, 118);
  write_u16(data, 0x588, 0xFFFF);  // -1
  return data;
}

/// IAM/AREA with record 118 at offset 0x800, size 0x9C0.
std::vector<std::byte> make_area_archive() {
  const std::vector<std::byte> prefix{make_prefix()};
  constexpr std::size_t k_record_offset{0x800};
  constexpr std::uint32_t k_record_size{0x9C0};

  std::vector<std::byte> data(k_record_offset + k_record_size, std::byte{});
  const std::size_t entry{118U * 8U};
  write_u32(data, entry, static_cast<std::uint32_t>(k_record_offset));
  write_u32(data, entry + 4U, k_record_size);

  write_u32(data, k_record_offset + 0x04, 0x3FC);  // script offset.
  write_name(data, k_record_offset + 0x58, "GRID");
  write_name(data, k_record_offset + 0x61, "GRID");
  std::memcpy(data.data() + k_record_offset + 0x3FC, prefix.data(), prefix.size());
  return data;
}

/// Minimal valid SCX container (empty descriptor, end tag only).
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

/// Scratch directory wiped on construction and destruction.
class TempDirectory {
 public:
  TempDirectory() : m_root{std::filesystem::temp_directory_path() / "opennomad-startup-test"} {
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

/// RAII override of the game-data root for the duration of a test.
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
  write_bytes(temp.root() / "IAM" / "AREA", make_area_archive());
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
  write_bytes(temp.root() / "MESHES" / "DECORS" / "GRID.3DO", make_minimal_3do());
}

}  // namespace

TEST_SUITE("Core::Scenario::ScenarioStartupController") {
  TEST_CASE("Startup reaches the main menu through START, AREA 118 and interface 29") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    App::ScenarioStartupController controller;

    REQUIRE(controller.initialize(manager).has_value());

    CHECK_EQ(controller.initial_area_id(), 118);
    CHECK_EQ(controller.linked_area_id(), -1);
    CHECK_EQ(controller.area_mapping(118), std::optional<std::int32_t>{-1});

    REQUIRE(controller.area_record() != nullptr);
    CHECK_EQ(controller.area_record()->record_size(), 0x9C0U);
    CHECK_EQ(controller.area_record()->script_offset(), 0x3FCU);
    CHECK_EQ(controller.area_record()->model3do_name(), "GRID");
    CHECK_EQ(controller.area_record()->scenario_scx_name(), "GRID");
    CHECK(controller.grid_3do_model() != nullptr);

    // GRID.SCX in world context 0, context 1 free, mode slot loaded.
    CHECK_EQ(manager.world_contexts()[0].scene_id, 0U);
    CHECK_EQ(manager.world_contexts()[0].residency, WorldSceneResidencyState::LoadedActive);
    CHECK_EQ(manager.world_contexts()[0].scenario_path, "SCPTDATA/GRID.SCX");
    CHECK(manager.world_contexts()[1].residency == WorldSceneResidencyState::Free);
    CHECK(manager.gameplay_mode_scx() != nullptr);

    // Loaded, queued and activated — but not executed until tick().
    REQUIRE(controller.area_script() != nullptr);
    CHECK(controller.area_script()->state() == AreaScriptState::k_ready);
    CHECK_FALSE(controller.dispatcher().main_menu_active());

    controller.dispatcher().set_interface_open_sink(
        [](std::uint16_t /*id*/) { return std::expected<void, std::string>{}; });

    REQUIRE(controller.tick().has_value());
    CHECK(controller.ticked());

    CHECK(controller.dispatcher().main_menu_active());
    CHECK(controller.area_script()->state() == AreaScriptState::k_waiting);
    CHECK_EQ(controller.area_script()->wait_state(), 6U);
  }

  TEST_CASE("A negative initial area ID fails startup cleanly") {
    const TempDirectory temp;
    write_boot_fixtures(temp);

    std::vector<std::byte> start{make_start()};
    write_u16(start, 0x586, 0xFFFF);  // -1.
    write_bytes(temp.root() / "IAM" / "START", start);

    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    App::ScenarioStartupController controller;
    const auto result{controller.initialize(manager)};
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("negative") != std::string::npos);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

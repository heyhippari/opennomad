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
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Scenario/ScenarioStartupController.hpp"
#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "OmikronTestBuffer.hpp"

namespace {

constexpr std::uint32_t K_SCX_MAGIC{0x00DEAD00U};
constexpr std::uint32_t K_SCRIPTS_TAG{0xDEAD0002U};
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

std::vector<std::byte> make_new_game_script() {
  Buffer bytes;
  const std::vector<std::byte> prefix{make_prefix()};
  for (const std::byte value : prefix) {
    bytes.u8(std::to_integer<std::uint8_t>(value));
  }
  bytes.u8(0x67).u16(87).u16(1).u16(1);
  bytes.u8(0x84);
  bytes.u8(0x07).u8(0);
  bytes.u8(0x0A).u16(19);
  bytes.u8(0x19);
  bytes.u8(0x06).u16(0x003B);
  bytes.u8(0x77).u32(0x00FFFFFFU).u16(30).u16(20);
  bytes.u8(0x5F).u16(2152).u16(0).u16(3);
  bytes.u8(0x39).u16(20).u16(0).u16(0);
  bytes.u8(0x5F).u16(2153).u16(0).u16(1);
  bytes.u8(0x5C).u16(753);
  bytes.u8(0x60).u16(2154).u16(100).u16(1);
  bytes.u8(0x76).u32(0x00FFFFFFU).u16(5).u16(0);
  bytes.u8(0x60).u16(2158).u16(25).u16(1);
  bytes.u8(0x04).u16(0x00A6);
  bytes.u8(0x77).u32(0).u16(30).u16(0);
  bytes.u8(0x5F).u16(2172).u16(0).u16(2);
  bytes.u8(0x5F).u16(2148).u16(130).u16(2);
  bytes.u8(0x4E).u16(310).u16(1);
  bytes.u8(0x3C).u16(310).u16(1).u16(0);
  bytes.zeros(0x11FU - bytes.data().size());
  bytes.u8(0x03);
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
  const std::vector<std::byte> script{make_new_game_script()};
  constexpr std::size_t k_record_offset{0x800};
  constexpr std::uint32_t k_record_size{0x9C0};

  std::vector<std::byte> data(k_record_offset + k_record_size, std::byte{});
  const std::size_t entry{118U * 8U};
  write_u32(data, entry, static_cast<std::uint32_t>(k_record_offset));
  write_u32(data, entry + 4U, k_record_size);

  write_u32(data, k_record_offset + 0x04, 0x3FC);  // script offset.
  write_name(data, k_record_offset + 0x58, "GRID");
  write_name(data, k_record_offset + 0x61, "GRID");

  // Retail-shaped AREA table-0 placement for character 310.
  constexpr std::size_t k_placement_offset{0x0B4};
  constexpr std::size_t k_definition_offset{0x1D4};
  write_u32(data, k_record_offset + 0x28, k_placement_offset);
  write_u16(data, k_record_offset + 0x48, 1);
  write_u16(data, k_record_offset + k_placement_offset + 0x00U, 0xFFFF);
  write_u16(data, k_record_offset + k_placement_offset + 0x02U, 310);
  const std::int32_t position_x{-2588};
  const std::int32_t position_y{-271};
  const std::int32_t position_z{-816};
  std::memcpy(data.data() + k_record_offset + k_placement_offset + 0x04U,
      &position_x,
      sizeof(position_x));
  std::memcpy(data.data() + k_record_offset + k_placement_offset + 0x08U,
      &position_y,
      sizeof(position_y));
  std::memcpy(data.data() + k_record_offset + k_placement_offset + 0x0CU,
      &position_z,
      sizeof(position_z));
  write_u16(data, k_record_offset + k_placement_offset + 0x10U, 4084);
  write_u16(data, k_record_offset + k_placement_offset + 0x12U, 468);

  // The authored body resolves by the character identity stored at +0x110.
  write_u32(data, k_record_offset + 0x28U + (4U * 4U), k_definition_offset);
  write_u16(data, k_record_offset + 0x48U + (4U * 2U), 1);
  constexpr std::string_view k_character_name{"KAY'L 669"};
  std::memcpy(data.data() + k_record_offset + k_definition_offset + 0x08U,
      k_character_name.data(),
      k_character_name.size());
  constexpr std::string_view k_model_name{"HO1_FNM"};
  std::memcpy(data.data() + k_record_offset + k_definition_offset + 0x90U,
      k_model_name.data(),
      k_model_name.size());
  write_u16(data, k_record_offset + k_definition_offset + 0x110U, 310);

  std::memcpy(data.data() + k_record_offset + 0x3FC, script.data(), script.size());
  return data;
}

/// Minimal valid SCX container (empty descriptor, end tag only).
std::vector<std::byte> make_minimal_scx() {
  Buffer bytes;
  bytes.u32(K_SCX_MAGIC).u32(5).u32(8).u32(4).u32(K_END_TAG);
  return bytes.data();
}

/// Minimal SCX with source index 0 carrying authored script ID 1. The command
/// intentionally omits arguments so this focused launch-lifecycle fixture
/// stops after proving the exact character-bound instance was dispatched.
std::vector<std::byte> make_kayl_arrives_scx() {
  Buffer descriptor;
  descriptor.u32(K_SCRIPTS_TAG).u32(1);
  descriptor.u32(0).chars("1KaylArrives", 22).u16(1).u16(0).u16(0);
  descriptor.u32(1).u32(0).u32(0);           // One root, current index, root placeholder.
  descriptor.u32(0).u32(0);                  // No linked commands, linked placeholder.
  descriptor.u32(0).u32(0);                  // field34 and runtime field38.
  descriptor.zeros(3U * 4U).zeros(3U * 4U);  // Binding-table header fields.
  descriptor.u32(0).u32(0).zeros(8);         // Related/runtime placeholders and tail.
  descriptor.u32(0);                         // Empty shared value pool.
  descriptor.u8(0);                          // No related script.
  descriptor.u32(0x0200002AU).u32(0).u32(0).u32(0xFFFFFFFFU).u32(1).u32(0);
  descriptor.u32(0).u32(0);  // Empty binding tables A and B.
  descriptor.u32(K_END_TAG);

  Buffer bytes;
  bytes.u32(K_SCX_MAGIC).u32(5).u32(8).u32(static_cast<std::uint32_t>(descriptor.data().size()));
  for (const std::byte value : descriptor.data()) {
    bytes.u8(std::to_integer<std::uint8_t>(value));
  }
  return bytes.data();
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
    CHECK(manager.world_contexts()[0].decor_model.has_value());

    // GRID.SCX in world context 0, context 1 free, mode slot loaded.
    CHECK_EQ(manager.world_contexts()[0].scene_id, 0U);
    CHECK_EQ(manager.world_contexts()[0].residency, WorldSceneResidencyState::LoadedActive);
    CHECK_EQ(manager.world_contexts()[0].scenario_path, "SCPTDATA/GRID.SCX");
    CHECK(manager.world_contexts()[1].residency == WorldSceneResidencyState::Free);
    CHECK(manager.gameplay_mode_scx() != nullptr);

    // Loaded, queued and activated — but not executed until tick().
    REQUIRE(controller.area_script() != nullptr);
    CHECK(controller.area_script()->state() == AreaScriptState::k_ready);
    CHECK_FALSE(controller.main_menu_active());

    controller.dispatcher().set_interface_open_sink(
        [](const App::InterfaceOpenRequest& request)
            -> std::expected<App::InterfaceHandle, std::string> {
          return App::InterfaceHandle{.interface_id = request.interface_id, .generation = 1};
        });

    REQUIRE(controller.tick().has_value());
    CHECK(controller.ticked());

    CHECK(controller.main_menu_active());
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

  TEST_CASE("New Game materializes character 310 before tracked script state 4") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_kayl_arrives_scx());
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    App::ScenarioStartupController controller;
    REQUIRE(controller.initialize(manager).has_value());
    controller.dispatcher().set_interface_open_sink(
        [](const App::InterfaceOpenRequest& request)
            -> std::expected<App::InterfaceHandle, std::string> {
          return App::InterfaceHandle{.interface_id = request.interface_id, .generation = 1};
        });

    App::WorldSceneContext* context{manager.active_world_context()};
    REQUIRE(context != nullptr);
    REQUIRE(context->runtime != nullptr);
    context->runtime->character_runtime().set_model_loader(
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          auto resource{std::make_shared<App::Character::ModelResource>()};
          resource->name = name;
          resource->groups.push_back(App::Omikron::MaterialGroup{});
          return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
        });

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller
                .complete_interface(App::InterfaceCompletion{
                    .handle = App::InterfaceHandle{.interface_id = 29, .generation = 1}, .result = 3})
                .has_value());
    REQUIRE(controller.tick().has_value());  // 0x77 yield.
    REQUIRE(controller.tick().has_value());  // Camera 2172 yield.
    REQUIRE(controller.tick().has_value());  // Camera 2148 yield.
    const auto launched{controller.tick()};  // 0x4E then tracked 0x3C.
    const std::string launch_error{launched ? std::string{} : launched.error()};
    CAPTURE(launch_error);
    REQUIRE(launched.has_value());

    const App::Script::AreaScriptRuntime* area_script{controller.area_script()};
    REQUIRE(area_script != nullptr);
    CHECK(area_script->state() == App::Script::AreaScriptState::k_waiting);
    CHECK_EQ(area_script->runtime_state(), 4U);
    CHECK_EQ(area_script->instruction_pointer(), 0x9CU);
    CHECK(area_script->wait_info().kind == App::Script::AreaWaitKind::k_character_script);
    REQUIRE(area_script->wait_info().character_script_instance.has_value());

    const App::Character::RuntimeCharacter* character{
        context->runtime->character_runtime().find(310)};
    REQUIRE(character != nullptr);
    CHECK(character->active);
    CHECK(character->area_present);
    CHECK_EQ(character->model_resource_name, "HO1_FNM");
    CHECK_EQ(character->transform.translation.x, -399.0F);
    CHECK_EQ(character->transform.translation.y, -42.0F);
    CHECK_EQ(character->transform.translation.z, -126.0F);

    App::Script::ScriptRuntime* script_runtime{context->runtime->script_runtime()};
    REQUIRE(script_runtime != nullptr);
    REQUIRE_EQ(script_runtime->instances().size(), 1U);
    const std::size_t instance_id{area_script->wait_info().character_script_instance.value()};
    const App::Script::ScriptInstance* instance{script_runtime->instance(instance_id)};
    REQUIRE(instance != nullptr);
    CHECK_EQ(instance->script_name, "1KaylArrives");
    CHECK_EQ(instance->launch_context.character_id, std::optional<std::int16_t>{310});
    CHECK_EQ(instance->launch_context.parameter, 0);

    context->runtime->tick(1.0F / 30.0F);
    instance = script_runtime->instance(instance_id);
    REQUIRE(instance != nullptr);
    CHECK(instance->paused);
    CHECK_FALSE(instance->completed);
    CHECK_EQ(instance->pause_info.opcode, 0x0200002AU);
    CHECK_EQ(instance->pause_info.opcode_name, "Script_SelectRelativeBodyAnimation");
    CHECK_EQ(instance->pause_info.character_id, std::optional<std::int16_t>{310});
    CHECK(instance->pause_info.reason ==
          App::Script::ScriptPauseReason::k_invalid_argument_count);

    // AREA remains on the exact malformed child and no duplicate launch was created.
    CHECK_EQ(area_script->runtime_state(), 4U);
    CHECK_EQ(area_script->instruction_pointer(), 0x9CU);
    CHECK_EQ(area_script->wait_info().character_script_instance,
        std::optional<std::size_t>{instance_id});
    CHECK_EQ(script_runtime->instances().size(), 1U);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

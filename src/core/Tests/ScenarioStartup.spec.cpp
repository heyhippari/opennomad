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

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/GameState.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Scenario/ScenarioStartupController.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "Core/Script/ScriptRuntime.hpp"
#include "IamStartTestData.hpp"
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
  // The trailing compact Scalar16 is presentation metadata, not an SCX
  // ScriptLaunchContext parameter.
  bytes.u8(0x3C).u16(310).u16(1).u16(123);
  bytes.zeros(0x11FU - bytes.data().size());
  bytes.u8(0x03);
  return bytes.data();
}

/// IAM/START selecting initial area 118 with linked area -1.
std::vector<std::byte> make_start() {
  return App::Tests::make_canonical_start();
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

  // AREA table-0 placements selected by the compact startup and New Game paths.
  constexpr std::size_t k_placement_offset{0x0B4};
  constexpr std::size_t k_definition_offset{0x1D4};
  constexpr std::size_t k_second_placement_offset{k_placement_offset + 0x14U};
  constexpr std::size_t k_second_definition_offset{k_definition_offset + 0x114U};
  write_u32(data, k_record_offset + 0x28, k_placement_offset);
  write_u16(data, k_record_offset + 0x48, 2);
  write_u16(data, k_record_offset + k_placement_offset + 0x00U, 0xFFFF);
  write_u16(data, k_record_offset + k_placement_offset + 0x02U, 310);
  const std::int32_t position_x{-2588};
  const std::int32_t position_y{-271};
  const std::int32_t position_z{-816};
  std::memcpy(
      data.data() + k_record_offset + k_placement_offset + 0x04U, &position_x, sizeof(position_x));
  std::memcpy(
      data.data() + k_record_offset + k_placement_offset + 0x08U, &position_y, sizeof(position_y));
  std::memcpy(
      data.data() + k_record_offset + k_placement_offset + 0x0CU, &position_z, sizeof(position_z));
  write_u16(data, k_record_offset + k_placement_offset + 0x10U, 4084);
  write_u16(data, k_record_offset + k_placement_offset + 0x12U, 468);
  write_u16(data, k_record_offset + k_second_placement_offset + 0x00U, 0xFFFF);
  write_u16(data, k_record_offset + k_second_placement_offset + 0x02U, 136);
  write_u32(data, k_record_offset + k_second_placement_offset + 0x04U, 100U);
  write_u32(data, k_record_offset + k_second_placement_offset + 0x08U, 200U);
  write_u32(data, k_record_offset + k_second_placement_offset + 0x0CU, 300U);
  write_u16(data, k_record_offset + k_second_placement_offset + 0x10U, 0);
  write_u16(data, k_record_offset + k_second_placement_offset + 0x12U, 469);

  // The authored body resolves by the character identity stored at +0x110.
  write_u32(data, k_record_offset + 0x28U + (4U * 4U), k_definition_offset);
  write_u16(data, k_record_offset + 0x48U + (4U * 2U), 2);
  constexpr std::string_view k_character_name{"KAY'L 669"};
  std::memcpy(data.data() + k_record_offset + k_definition_offset + 0x08U,
      k_character_name.data(),
      k_character_name.size());
  constexpr std::string_view k_model_name{"HO1_FNM"};
  std::memcpy(data.data() + k_record_offset + k_definition_offset + 0x90U,
      k_model_name.data(),
      k_model_name.size());
  write_u16(data, k_record_offset + k_definition_offset + 0x110U, 310);
  constexpr std::string_view k_current_character_name{"CURRENT CHARACTER"};
  std::memcpy(data.data() + k_record_offset + k_second_definition_offset + 0x08U,
      k_current_character_name.data(),
      k_current_character_name.size());
  std::memcpy(data.data() + k_record_offset + k_second_definition_offset + 0x90U,
      k_model_name.data(),
      k_model_name.size());
  write_u16(data, k_record_offset + k_second_definition_offset + 0x110U, 136);

  std::memcpy(data.data() + k_record_offset + 0x3FC, script.data(), script.size());
  return data;
}

std::vector<std::byte> make_handoff_area_archive() {
  Buffer handoff;
  handoff.u8(0x38).u16(136);
  handoff.u8(0x4F).u16(0xFFFF);
  handoff.u8(0x41).u16(6);
  handoff.u8(0x40).u16(5);
  handoff.u8(0x2F).u16(222).u16(0xFFFF).u16(0xFFFF);
  handoff.u8(0x60).u16(0).u16(1).u16(0);
  handoff.u8(0x47).u16(222).u16(0);
  handoff.u8(0x49).u16(654);
  handoff.u8(0x30).u16(118);
  handoff.u8(0x03);

  constexpr std::size_t k_source_offset{0x800};
  constexpr std::size_t k_target_offset{0xC00};
  constexpr std::size_t k_header_size{0xB4};
  constexpr std::size_t k_source_placement_offset{k_header_size};
  constexpr std::size_t k_source_definition_offset{k_source_placement_offset + 0x14U};
  constexpr std::size_t k_source_zone_offset{k_source_definition_offset + 0x114U};
  constexpr std::size_t k_source_script_offset{k_source_zone_offset + (2U * 0x44U)};
  const std::size_t source_record_size{k_source_script_offset + handoff.data().size()};
  constexpr std::size_t k_target_zone_offset{k_header_size};
  constexpr std::size_t k_target_address_offset{k_target_zone_offset + (2U * 0x44U)};
  constexpr std::size_t k_target_record_size{k_target_address_offset + 0x10U};
  std::vector<std::byte> data(k_target_offset + k_target_record_size, std::byte{});
  write_u32(data, 118U * 8U, static_cast<std::uint32_t>(k_source_offset));
  write_u32(data, (118U * 8U) + 4U, static_cast<std::uint32_t>(source_record_size));
  write_u32(data, k_source_offset + 0x04U, k_source_script_offset);
  write_name(data, k_source_offset + 0x61U, "GRID");
  write_u32(data, k_source_offset + 0x28U, k_source_placement_offset);
  write_u16(data, k_source_offset + 0x48U, 1);
  write_u16(data, k_source_offset + k_source_placement_offset + 0x00U, 0xFFFF);
  write_u16(data, k_source_offset + k_source_placement_offset + 0x02U, 136);
  write_u32(data, k_source_offset + k_source_placement_offset + 0x04U, 100U);
  write_u32(data, k_source_offset + k_source_placement_offset + 0x08U, 200U);
  write_u32(data, k_source_offset + k_source_placement_offset + 0x0CU, 300U);
  write_u16(data, k_source_offset + k_source_placement_offset + 0x10U, 0);
  write_u32(data, k_source_offset + 0x28U + (4U * 4U), k_source_definition_offset);
  write_u16(data, k_source_offset + 0x48U + (4U * 2U), 1);
  write_u32(data, k_source_offset + 0x28U + (2U * 4U), k_source_zone_offset);
  write_u16(data, k_source_offset + 0x48U + (2U * 2U), 2);
  constexpr std::string_view k_source_name{"CURRENT CHARACTER"};
  constexpr std::string_view k_source_model{"CURRENT_BODY"};
  std::memcpy(data.data() + k_source_offset + k_source_definition_offset + 0x08U,
      k_source_name.data(),
      k_source_name.size());
  std::memcpy(data.data() + k_source_offset + k_source_definition_offset + 0x90U,
      k_source_model.data(),
      k_source_model.size());
  write_u16(data, k_source_offset + k_source_definition_offset + 0x110U, 136);
  write_u32(data, k_source_offset + k_source_zone_offset + 0x00U, 0x10203040U);
  write_u16(data, k_source_offset + k_source_zone_offset + 0x40U, 5);
  write_u32(data, k_source_offset + k_source_zone_offset + 0x44U, 0x50607080U);
  write_u16(data, k_source_offset + k_source_zone_offset + 0x44U + 0x40U, 6);
  std::memcpy(data.data() + k_source_offset + k_source_script_offset,
      handoff.data().data(),
      handoff.data().size());

  write_u32(data, 222U * 8U, static_cast<std::uint32_t>(k_target_offset));
  write_u32(data, (222U * 8U) + 4U, k_target_record_size);
  write_u32(data, k_target_offset + 0x04U, k_target_record_size);
  write_name(data, k_target_offset + 0x61U, "DEST");
  write_u32(data, k_target_offset + 0x28U + (2U * 4U), k_target_zone_offset);
  write_u16(data, k_target_offset + 0x48U + (2U * 2U), 2);
  write_u32(data, k_target_offset + 0x28U + (5U * 4U), k_target_address_offset);
  write_u16(data, k_target_offset + 0x48U + (5U * 2U), 1);
  write_u32(data, k_target_offset + k_target_zone_offset + 0x00U, 0x50607080U);
  write_u16(data, k_target_offset + k_target_zone_offset + 0x40U, 5);
  write_u32(data, k_target_offset + k_target_zone_offset + 0x44U, 0x90A0B0C0U);
  write_u16(data, k_target_offset + k_target_zone_offset + 0x44U + 0x40U, 0x8005U);
  write_u32(data, k_target_offset + k_target_address_offset + 0x00U, 43922U);
  write_u32(data, k_target_offset + k_target_address_offset + 0x04U, 2592U);
  write_u32(data, k_target_offset + k_target_address_offset + 0x08U, 19656U);
  write_u16(data, k_target_offset + k_target_address_offset + 0x0CU, 0);
  write_u16(data, k_target_offset + k_target_address_offset + 0x0EU, 654);
  return data;
}

std::vector<std::byte> make_handoff_scene_archive() {
  Buffer script;
  script.u8(0x38).u16(57);
  script.u8(0x4F).u16(0xFFFF);
  script.u8(0x4E).u16(0xFFFF).u16(0);
  script.u8(0x2E).u16(221).u16(0);
  script.u8(0x03);

  constexpr std::size_t k_record_offset{0x800};
  constexpr std::size_t k_table0_offset{0x44};
  constexpr std::size_t k_table2_offset{k_table0_offset + 0x14U};
  constexpr std::size_t k_table4_offset{k_table2_offset + 0x44U};
  constexpr std::size_t k_script_offset{k_table4_offset + 0x114U};
  const std::size_t table6_offset{k_script_offset + script.data().size()};
  std::vector<std::byte> data(k_record_offset + table6_offset, std::byte{});
  write_u32(data, 0, static_cast<std::uint32_t>(k_record_offset));
  write_u32(data, 4, static_cast<std::uint32_t>(table6_offset));
  write_u32(data, k_record_offset + 0x04U, static_cast<std::uint32_t>(k_script_offset));
  write_u32(data, k_record_offset + 0x08U, static_cast<std::uint32_t>(k_table0_offset));
  write_u16(data, k_record_offset + 0x28U, 1);
  write_u32(data, k_record_offset + 0x08U + (1U * 4U), static_cast<std::uint32_t>(k_table2_offset));
  for (const std::size_t table_index : {3U, 4U}) {
    write_u32(data,
        k_record_offset + 0x08U + (table_index * 4U),
        static_cast<std::uint32_t>(k_table4_offset));
  }
  write_u32(data, k_record_offset + 0x08U + (2U * 4U), static_cast<std::uint32_t>(k_table2_offset));
  write_u16(data, k_record_offset + 0x28U + (2U * 2U), 1);
  write_u16(data, k_record_offset + 0x28U + (4U * 2U), 1);
  write_u32(data, k_record_offset + 0x08U + (6U * 4U), static_cast<std::uint32_t>(table6_offset));
  write_u32(data, k_record_offset + 0x08U + (7U * 4U), static_cast<std::uint32_t>(k_script_offset));

  write_u16(data, k_record_offset + k_table0_offset + 0x00U, 0xFFFF);
  write_u16(data, k_record_offset + k_table0_offset + 0x02U, 57);
  write_u32(data, k_record_offset + k_table0_offset + 0x04U, 49457U);
  write_u32(data, k_record_offset + k_table0_offset + 0x08U, static_cast<std::uint32_t>(-511));
  write_u32(data, k_record_offset + k_table0_offset + 0x0CU, 19386U);
  write_u16(data, k_record_offset + k_table0_offset + 0x10U, 4073);
  write_u32(
      data, k_record_offset + k_table2_offset + 0x00U, static_cast<std::uint32_t>(k_script_offset));
  write_u16(data, k_record_offset + k_table2_offset + 0x40U, 5);
  constexpr std::string_view k_name{"LOCAL CHARACTER"};
  constexpr std::string_view k_model{"DE1_FN"};
  std::memcpy(
      data.data() + k_record_offset + k_table4_offset + 0x08U, k_name.data(), k_name.size());
  std::memcpy(
      data.data() + k_record_offset + k_table4_offset + 0x90U, k_model.data(), k_model.size());
  write_u16(data, k_record_offset + k_table4_offset + 0x110U, 57);
  std::memcpy(
      data.data() + k_record_offset + k_script_offset, script.data().data(), script.data().size());
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
std::vector<std::byte> make_kayl_arrives_scx(const std::uint16_t script_id = 1) {
  Buffer descriptor;
  descriptor.u32(K_SCRIPTS_TAG).u32(1);
  descriptor.u32(0).chars("1KaylArrives", 22).u16(script_id).u16(0).u16(0);
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
  bytes.chars("OD3X", 4)
      .u32(4)
      .u32(0x2C)
      .u32(416)
      .u32(416)
      .u32(416)
      .u32(416)
      .u32(416)
      .u32(0)
      .u32(0)
      .u32(416)
      .zeros(72)
      .u32(0)
      .zeros(104)
      .u32(0)
      .zeros(24)
      .u32(0)
      .zeros(12)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .u64(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
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

void write_current_character_script_fixtures(const TempDirectory& temp) {
  Buffer script;
  script.u8(0x2E).u16(221).u16(0).u8(0x03);
  std::vector<std::byte> area{make_area_archive()};
  std::memcpy(area.data() + 0x800U + 0x3FCU, script.data().data(), script.data().size());

  write_bytes(temp.root() / "IAM" / "START", make_start());
  write_bytes(temp.root() / "IAM" / "AREA", area);
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_kayl_arrives_scx(221));
  write_bytes(temp.root() / "MESHES" / "DECORS" / "GRID.3DO", make_minimal_3do());
}

void write_character_value_fixtures(const TempDirectory& temp) {
  Buffer script;
  script.u8(0x38).u16(136);
  script.u8(0x56).u16(0xFFFF).u16(5).u16(61);
  script.u8(0x56).u16(310).u16(4).u16(63);
  script.u8(0x0C).u16(60);
  script.u8(0x5D).u16(0xFFFF).u16(5).u16(60);
  script.u8(0x0C).u16(37);
  script.u8(0x5D).u16(0xFFFF).u16(4).u16(37);
  script.u8(0x5D).u16(310).u16(4).u16(62);
  script.u8(0x56).u16(0xFFFF).u16(5).u16(64);
  script.u8(0x56).u16(310).u16(4).u16(65);
  script.u8(0x03);

  std::vector<std::byte> start{make_start()};
  write_u32(start, 0x058CU + (60U * sizeof(std::int32_t)), 1234U);
  write_u32(start, 0x058CU + (62U * sizeof(std::int32_t)), 321U);

  std::vector<std::byte> area{make_area_archive()};
  constexpr std::size_t k_record_offset{0x800};
  constexpr std::size_t k_definition_offset{0x1D4};
  constexpr std::size_t k_second_definition_offset{k_definition_offset + 0x114U};
  write_u16(area, k_record_offset + k_definition_offset + 0xACU, 444);
  write_u16(area, k_record_offset + k_second_definition_offset + 0xAEU, 77);
  std::memcpy(area.data() + k_record_offset + 0x3FCU, script.data().data(), script.data().size());

  write_bytes(temp.root() / "IAM" / "START", start);
  write_bytes(temp.root() / "IAM" / "AREA", area);
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
  write_bytes(temp.root() / "MESHES" / "DECORS" / "GRID.3DO", make_minimal_3do());
}

void write_handoff_fixtures(const TempDirectory& temp) {
  std::vector<std::byte> start{make_start()};
  start.at(0x13FCU) = std::byte{0x40};  // ZONE 6 starts persistently enabled.
  write_bytes(temp.root() / "IAM" / "START", start);
  write_bytes(temp.root() / "IAM" / "AREA", make_handoff_area_archive());
  write_bytes(temp.root() / "IAM" / "SCENE", make_handoff_scene_archive());
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "DEST.SCX", make_kayl_arrives_scx(221));
}

}  // namespace

TEST_SUITE("Core::Scenario::ScenarioStartupController") {
  TEST_CASE(
      "compact character values use current and explicit owner profiles without body reload") {
    const TempDirectory temp;
    write_character_value_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    App::ScenarioStartupController controller;
    REQUIRE(controller.initialize(manager).has_value());
    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);
    runtime->character_runtime().set_model_loader(
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          auto resource{std::make_shared<App::Character::ModelResource>()};
          resource->name = name;
          resource->groups.push_back(App::Omikron::MaterialGroup{});
          return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
        });

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.area_script() != nullptr);
    CHECK(controller.area_script()->state() == AreaScriptState::k_ready);
    CHECK_EQ(controller.current_controlled_character(), std::optional<std::int16_t>{136});
    REQUIRE(manager.game_state() != nullptr);
    CHECK_EQ(manager.game_state()->global_variable(60).value(), 0);
    CHECK_EQ(manager.game_state()->global_variable(37).value(), 0);
    CHECK_EQ(manager.game_state()->global_variable(61).value(), 77);
    CHECK_EQ(manager.game_state()->global_variable(63).value(), 444);
    CHECK_EQ(manager.game_state()->global_variable(64).value(), 0);
    CHECK_EQ(manager.game_state()->global_variable(65).value(), 321);
    CHECK_EQ(manager.game_state()->character_value(136, 5).value(), 0);
    CHECK_EQ(manager.game_state()->character_value(310, 4).value(), 321);
    REQUIRE(manager.game_state()->current_character().has_value());
    CHECK_EQ(manager.game_state()->current_character()->character_id, 136);
    CHECK_EQ(manager.game_state()->current_character()->values.rings, 0);
    CHECK_EQ(manager.game_state()->current_character()->values.seteks, 0U);
    CHECK(runtime->character_runtime().find(136) != nullptr);
    CHECK(runtime->character_runtime().find(310) == nullptr);
  }

  TEST_CASE("current-character script bridge rejects missing or foreign session targets") {
    SUBCASE("no current controlled character") {
      const TempDirectory temp;
      write_current_character_script_fixtures(temp);
      const ScopedGameDataRoot root{temp.root()};

      App::ScenarioManager manager;
      App::ScenarioStartupController controller;
      REQUIRE(controller.initialize(manager).has_value());

      const auto result{controller.tick()};
      REQUIRE_FALSE(result.has_value());
      CHECK(result.error().find("current controlled character is not established") !=
            std::string::npos);
      REQUIRE(controller.area_script() != nullptr);
      CHECK(controller.area_script()->state() == AreaScriptState::k_failed);
    }

    SUBCASE("current controlled character belongs to another world") {
      const TempDirectory temp;
      write_current_character_script_fixtures(temp);
      const ScopedGameDataRoot root{temp.root()};

      App::ScenarioManager manager;
      App::ScenarioStartupController controller;
      REQUIRE(controller.initialize(manager).has_value());
      manager.set_controlled_character(
          App::ControlledCharacterRef{.character_id = 49, .world_scene_id = 1});

      const auto result{controller.tick()};
      REQUIRE_FALSE(result.has_value());
      CHECK(result.error().find("belongs to world 1") != std::string::npos);
      CHECK(result.error().find("owner is world 0") != std::string::npos);
      REQUIRE(controller.area_script() != nullptr);
      CHECK(controller.area_script()->state() == AreaScriptState::k_failed);
    }
  }

  TEST_CASE("0x2F preserves the active source until 0x47 commits the attached SCENE") {
    const TempDirectory temp;
    write_handoff_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    App::ScenarioManager manager;
    App::ScenarioStartupController controller;
    REQUIRE(controller.initialize(manager).has_value());
    REQUIRE(manager.game_state() != nullptr);
    CHECK(manager.game_state()->zone_flag(6).value());
    REQUIRE_EQ(controller.active_zones().size(), 1U);
    CHECK_EQ(controller.active_zones()[0].zone.zone_id, 6);

    App::ScenarioRuntime* source_runtime{manager.world_runtime(0)};
    REQUIRE(source_runtime != nullptr);
    source_runtime->character_runtime().set_model_loader(
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          auto resource{std::make_shared<App::Character::ModelResource>()};
          resource->name = name;
          resource->groups.push_back(App::Omikron::MaterialGroup{});
          return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
        });

    REQUIRE(controller.tick().has_value());  // 0x38 selects, then 0x2F accepts and waits.
    const App::Character::RuntimeCharacter* initial_current{
        source_runtime->character_runtime().find(136)};
    REQUIRE(initial_current != nullptr);
    CHECK(initial_current->active);
    CHECK(initial_current->area_present);
    CHECK_EQ(initial_current->serialized_area_position.at(0), 100);
    CHECK_FALSE(initial_current->presentation_enabled);
    CHECK(manager.game_state()->zone_flag(5).value());
    CHECK_FALSE(manager.game_state()->zone_flag(6).value());
    REQUIRE_EQ(controller.active_zones().size(), 1U);
    CHECK_EQ(controller.active_zones()[0].resident_slot, 0U);
    CHECK(controller.active_zones()[0].source == App::ActiveZoneSource::k_area);
    CHECK_EQ(controller.active_zones()[0].zone.zone_id, 5);
    REQUIRE(manager.game_state()->set_character_value(136, 5, 91).has_value());
    REQUIRE(controller.tick().has_value());  // Target prepares, then camera wait.
    const App::RuntimeAreaSlot* source{controller.runtime_area_slot(0)};
    const App::RuntimeAreaSlot* destination{controller.runtime_area_slot(1)};
    REQUIRE(source != nullptr);
    REQUIRE(destination != nullptr);
    CHECK_EQ(source->primary_area_id, 118);
    CHECK_EQ(destination->primary_area_id, 222);
    CHECK_EQ(controller.active_area_slot(), 0U);
    REQUIRE_EQ(controller.active_zones().size(), 3U);
    CHECK_EQ(controller.active_zones()[0].resident_slot, 0U);
    CHECK_EQ(controller.active_zones()[1].resident_slot, 1U);
    CHECK_EQ(controller.active_zones()[2].resident_slot, 1U);
    CHECK_EQ(static_cast<std::uint16_t>(controller.active_zones()[2].zone.zone_id), 0x8005U);
    CHECK_EQ(manager.world_contexts()[0].residency, WorldSceneResidencyState::LoadedActive);
    CHECK_EQ(manager.world_contexts()[1].residency, WorldSceneResidencyState::LoadedInactive);

    App::ScenarioRuntime* destination_runtime{manager.world_runtime(1)};
    REQUIRE(destination_runtime != nullptr);
    destination_runtime->character_runtime().set_model_loader(
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          auto resource{std::make_shared<App::Character::ModelResource>()};
          resource->name = name;
          resource->groups.push_back(App::Omikron::MaterialGroup{});
          return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
        });
    const auto committed{controller.tick(1.0F)};  // 0x47, 0x49, 0x30, then SCENE selection.
    const std::string commit_error{committed.has_value() ? std::string{} : committed.error()};
    REQUIRE_MESSAGE(committed.has_value(), commit_error);
    source = controller.runtime_area_slot(0);
    destination = controller.runtime_area_slot(1);
    REQUIRE(source != nullptr);
    REQUIRE(destination != nullptr);
    CHECK_FALSE(source->primary.has_value());
    CHECK_EQ(destination->primary_area_id, 222);
    CHECK_EQ(destination->scene_id, 0);
    REQUIRE(destination->scene_script.has_value());
    CHECK(destination->scene_script->state() == AreaScriptState::k_waiting);
    CHECK(destination->scene_script->wait_info().kind ==
          App::Script::AreaWaitKind::k_character_script);
    REQUIRE(destination->scene_script->wait_info().character_script_instance.has_value());
    CHECK_EQ(controller.active_area_slot(), 1U);
    REQUIRE_EQ(controller.active_zones().size(), 3U);
    CHECK_EQ(controller.active_zones()[0].resident_slot, 1U);
    CHECK_EQ(controller.active_zones()[1].resident_slot, 1U);
    CHECK(controller.active_zones()[0].source == App::ActiveZoneSource::k_area);
    CHECK(controller.active_zones()[1].source == App::ActiveZoneSource::k_area);
    CHECK(controller.active_zones()[2].source == App::ActiveZoneSource::k_scene);
    CHECK_EQ(static_cast<std::uint16_t>(controller.active_zones()[1].zone.zone_id), 0x8005U);
    CHECK_EQ(manager.world_contexts()[0].residency, WorldSceneResidencyState::Free);
    CHECK_EQ(manager.world_contexts()[1].residency, WorldSceneResidencyState::LoadedActive);
    CHECK_EQ(controller.area_mapping(222), std::optional<std::int32_t>{0});
    CHECK_EQ(controller.area_mapping(118), std::optional<std::int32_t>{-1});
    REQUIRE(manager.game_state() != nullptr);
    CHECK_EQ(manager.game_state()->current_area(), 222);
    const std::optional<App::ControlledCharacterRef> current{manager.controlled_character()};
    REQUIRE(current.has_value());
    CHECK_EQ(current->character_id, 57);
    CHECK_EQ(current->world_scene_id, 1U);
    CHECK_EQ(controller.current_controlled_character(), std::optional<std::int16_t>{57});
    CHECK_EQ(manager.game_state()->character_value(136, 5).value(), 91);
    const App::Character::RuntimeCharacter* character{
        destination_runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    CHECK_EQ(character->serialized_area_position.at(0), 43922);
    CHECK_EQ(character->serialized_area_position.at(1), 2592);
    CHECK_EQ(character->serialized_area_position.at(2), 19656);
    CHECK_EQ(character->serialized_orientation_units, 0);
    CHECK_FALSE(character->presentation_enabled);
    CHECK_FALSE(character->renderable());
    const App::Character::RuntimeCharacter* destination_character{
        destination_runtime->character_runtime().find(57)};
    REQUIRE(destination_character != nullptr);
    CHECK_EQ(destination_character->serialized_area_position.at(0), 49457);
    CHECK(destination_character->presentation_enabled);
    CHECK(destination_character->renderable());
    App::Script::ScriptRuntime* const script_runtime{destination_runtime->script_runtime()};
    REQUIRE(script_runtime != nullptr);
    REQUIRE_EQ(script_runtime->instances().size(), 1U);
    const App::Script::ScriptInstance* const instance{script_runtime->instance(
        destination->scene_script->wait_info().character_script_instance.value())};
    REQUIRE(instance != nullptr);
    CHECK_EQ(instance->launch_context.character_id, std::optional<std::int16_t>{57});
    CHECK_EQ(instance->launch_context.parameter, 0);
  }

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
    REQUIRE(manager.game_state() != nullptr);
    CHECK_EQ(manager.game_state()->current_area(), 118);
    CHECK_EQ(manager.game_state()->linked_area(), -1);

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

    App::ScenarioRuntime* runtime{manager.world_runtime(0)};
    REQUIRE(runtime != nullptr);
    runtime->character_runtime().set_model_loader(
        [](const std::string_view name)
            -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
          auto resource{std::make_shared<App::Character::ModelResource>()};
          resource->name = name;
          resource->groups.push_back(App::Omikron::MaterialGroup{});
          return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
        });

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
    CHECK_EQ(controller.current_controlled_character(), std::optional<std::int16_t>{136});
    const App::Character::RuntimeCharacter* current{runtime->character_runtime().find(136)};
    REQUIRE(current != nullptr);
    CHECK_FALSE(current->presentation_enabled);
    CHECK_FALSE(current->renderable());
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
    REQUIRE(controller.tick().has_value());  // 0x84 intent, then 0x77 yield.
    REQUIRE_EQ(manager.world_presentation().pending_letterbox_count(), 1U);
    const auto letterbox{manager.world_presentation().take_letterbox()};
    REQUIRE(letterbox.has_value());
    CHECK_EQ(letterbox->scene_id, context->scene_id);
    CHECK_EQ(letterbox->scene_generation, context->generation);
    CHECK(letterbox->enabled);
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
    REQUIRE(area_script->last_character_script_request().has_value());
    CHECK(area_script->last_character_script_request()->target ==
          App::Script::AreaCharacterScriptTarget::k_explicit);
    CHECK_EQ(area_script->last_character_script_request()->character_id,
        std::optional<std::int16_t>{310});
    CHECK_EQ(area_script->last_character_script_request()->camera_duration_units, 123);

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
    CHECK(instance->pause_info.reason == App::Script::ScriptPauseReason::k_invalid_argument_count);

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

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
#include "Core/Omikron/CtlControlSet.hpp"
#include "Core/Omikron/IamArea.hpp"
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

void write_area_bytecode_pool_bounds(std::vector<std::byte>& data,
  const std::size_t record_offset,
  const std::size_t start,
  const std::size_t end) {
  write_u32(data,
    record_offset + App::Omikron::IamAreaRecord::k_offset_table_offsets +
      (6U * sizeof(std::uint32_t)),
    static_cast<std::uint32_t>(end));
  write_u32(data,
    record_offset + App::Omikron::IamAreaRecord::k_offset_table_offsets +
      (7U * sizeof(std::uint32_t)),
    static_cast<std::uint32_t>(start));
}

void write_name(
    std::vector<std::byte>& data, const std::size_t offset, const std::string_view name) {
  for (std::size_t index{0}; index < 9U; ++index) {
    data[offset + index] = index < name.size() ? static_cast<std::byte>(name[index]) : std::byte{};
  }
}

constexpr std::uint16_t K_NAMESPACE_CAMERA_ID{42};

void write_camera_record(std::vector<std::byte>& data,
    const std::size_t offset,
    const std::int32_t marker,
    const std::uint16_t camera_id = K_NAMESPACE_CAMERA_ID) {
  for (std::size_t axis{0}; axis < 3U; ++axis) {
    write_u32(data,
        offset + (axis * 4U),
        static_cast<std::uint32_t>(marker + static_cast<std::int32_t>(axis)));
    write_u32(data,
        offset + 0x0CU + (axis * 4U),
        static_cast<std::uint32_t>(-marker - static_cast<std::int32_t>(axis)));
  }
  write_u16(data, offset + 0x18U, camera_id);
  write_u16(data, offset + 0x1AU, 12U);
  write_u16(data, offset + 0x1CU, 0U);
  write_u16(data, offset + 0x1EU, static_cast<std::uint16_t>(800 + marker));
  write_u16(data, offset + 0x20U, 0U);
  write_u16(data, offset + 0x22U, 0U);
}

struct CameraNamespaceFixture {
  bool slot_0_area{false};
  bool slot_0_scene{false};
  bool slot_1_area{false};
  bool slot_1_scene{false};
  bool global{false};
  bool request_from_slot_1_scene{false};
  bool tracked{false};
};

void append_camera_request(
    Buffer& script, const bool tracked, const std::uint16_t camera_id = K_NAMESPACE_CAMERA_ID) {
  script.u8(tracked ? 0x60U : 0x5FU).u16(camera_id).u16(1).u16(0);
}

std::vector<std::byte> make_camera_namespace_global(const bool has_camera) {
  constexpr std::size_t k_header_size{0x20U};
  constexpr std::size_t k_camera_size{0x2CU};
  std::vector<std::byte> data(k_header_size + (has_camera ? k_camera_size : 0U), std::byte{});
  write_u32(data, 0x14U, k_header_size);
  write_u16(data, 0x1EU, has_camera ? 1U : 0U);
  if (has_camera) {
    write_camera_record(data, k_header_size, 500);
  }
  return data;
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
  bytes.u8(0x3B).u16(310).u16(6).u16(0);
  bytes.u8(0x77).u32(0xFFFFFFFFU).u16(45).u16(0);
  bytes.u8(0x3D).u16(272);
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

  write_u32(data, k_record_offset + 0x04, 0x3FC);  // Primary event.
  write_area_bytecode_pool_bounds(data, k_record_offset, 0x3FC, k_record_size);
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

std::vector<std::byte> make_object_activation_area_archive(const std::uint16_t object_id) {
  std::vector<std::byte> data{make_area_archive()};
  Buffer script;
  script.u8(0x5C).u16(object_id).u8(0x03);
  constexpr std::size_t k_record_offset{0x800U};
  constexpr std::size_t k_script_offset{0x3FCU};
  std::memcpy(
      data.data() + k_record_offset + k_script_offset, script.data().data(), script.data().size());
  return data;
}

std::vector<std::byte> make_object_archive(const std::uint16_t object_id,
    const std::uint16_t object_type,
    const std::string_view stem,
    const std::string_view subtitle) {
  constexpr std::size_t k_record_stride{0x800U};
  constexpr std::size_t k_record_size{0x518U};
  constexpr std::size_t k_type_offset{0x02U};
  constexpr std::size_t k_stem_offset{0x0EU};
  constexpr std::size_t k_subtitle_offset{0x118U};
  const std::size_t record_offset{static_cast<std::size_t>(object_id) * k_record_stride};
  std::vector<std::byte> data(record_offset + k_record_stride, std::byte{});
  write_u16(data, record_offset + k_type_offset, object_type);
  std::memcpy(data.data() + record_offset + k_stem_offset, stem.data(), stem.size());
  std::memcpy(data.data() + record_offset + k_subtitle_offset, subtitle.data(), subtitle.size());
  return data;
}

/// Synthetic CTL bank for zone-contact fixtures: move 7 is the flag-selected
/// default; the 0x3F zone event explicitly selects move 100. Keeping the two
/// distinct proves the opcode performs a real move switch.
std::vector<std::byte> make_stub_ctl_bank_bytes() {
  Buffer ctl;
  ctl.u32(0x30374543U).u32(0x101U).u32(0).u32(2).zeros(0x48U);
  ctl.u32(7).u32(1).u32(1).u32(0).u32(0).chars("Default", 12);
  ctl.u32(100).u32(1).u32(0).u32(0).u32(0).chars("Selected", 12);
  ctl.u32(71).u32(0).u32(0x8022U).zeros(0x58U - 12U);
  ctl.u32(101).u32(0).u32(0x8022U).zeros(0x58U - 12U);
  return ctl.data();
}

/// Installs a CTL bank loader answering every authored control set with the
/// stub bank, so current-character selection builds a real controller.
void install_stub_ctl_loader(App::ScenarioRuntime& runtime) {
  runtime.character_runtime().set_ctl_bank_loader(
      [](const std::string_view name)
          -> std::expected<std::shared_ptr<const App::Omikron::CtlControlSet>, std::string> {
        if (name.empty()) {
          return std::expected<std::shared_ptr<const App::Omikron::CtlControlSet>, std::string>{
              std::unexpect, "empty control set"};
        }
        const std::vector<std::byte> bytes{make_stub_ctl_bank_bytes()};
        auto parsed{App::Omikron::CtlControlSet::load(bytes)};
        if (!parsed) {
          return std::expected<std::shared_ptr<const App::Omikron::CtlControlSet>, std::string>{
              std::unexpect, parsed.error()};
        }
        return std::make_shared<const App::Omikron::CtlControlSet>(std::move(parsed).value());
      });
}

/// Synthetic AREA contact fixture. The top-level context selects a current
/// body and enables a zone; the record-relative zone event selects
/// a move, toggles the controller, self-disables, waits on a camera, then
/// toggles the controller off and ends.
std::vector<std::byte> make_zone_contact_area_archive(const bool starts_dialog = false,
    const bool self_disables = true,
  const bool enable_controller = true,
  const std::int16_t zone_id = 3795,
  const bool controller_off_before_wait = false,
  const bool place_before_activation = false,
  const std::uint32_t initial_xz = 50U,
  const std::int16_t orientation_center_units = 4090,
  const std::int16_t orientation_span_units = 0,
  const bool launch_fire_and_forget = false) {
  Buffer top_level;
  top_level.u8(0x38).u16(136);
  if (launch_fire_and_forget) {
    top_level.u8(0x5A).u16(221).u16(0);
  }
  if (enable_controller) {
    top_level.u8(0x68);
  }
  if (place_before_activation) {
    top_level.u8(0x49).u16(44);
  }
  top_level.u8(0x40).u16(static_cast<std::uint16_t>(zone_id));
  top_level.u8(0x03);

  Buffer zone_event;
  if (starts_dialog) {
    zone_event.u8(0x3D).u16(272);
  } else {
    zone_event.u8(0x3F).u16(100);
    if (self_disables) {
      zone_event.u8(0x68);
      zone_event.u8(0x41).u16(static_cast<std::uint16_t>(zone_id));
      if (controller_off_before_wait) {
        zone_event.u8(0x69);
      }
    }
    zone_event.u8(0x60).u16(42).u16(1).u16(3);
    if (self_disables && !controller_off_before_wait) {
      zone_event.u8(0x69);
    }
  }
  zone_event.u8(0x03);
  Buffer departure_event;
  departure_event.u8(0x03);

  constexpr std::size_t k_record_offset{0x800};
  constexpr std::size_t k_table0_offset{0x0B4};
  constexpr std::size_t k_table2_offset{k_table0_offset + 0x14U};
  constexpr std::size_t k_table4_offset{k_table2_offset + 0x44U};
  constexpr std::size_t k_table5_offset{k_table4_offset + 0x114U};
  const std::size_t k_script_offset{k_table5_offset + (place_before_activation ? 0x10U : 0U)};
  const std::size_t zone_event_offset{k_script_offset + top_level.data().size()};
  const std::size_t departure_event_offset{zone_event_offset + zone_event.data().size()};
  const std::size_t record_size{departure_event_offset + departure_event.data().size()};
  std::vector<std::byte> data(k_record_offset + record_size, std::byte{});
  write_u32(data, 118U * 8U, static_cast<std::uint32_t>(k_record_offset));
  write_u32(data, (118U * 8U) + 4U, static_cast<std::uint32_t>(record_size));
  write_u32(data, k_record_offset + 0x04U, k_script_offset);
  write_area_bytecode_pool_bounds(data, k_record_offset, k_script_offset, record_size);
  write_name(data, k_record_offset + 0x61U, "GRID");

  write_u32(data, k_record_offset + 0x28U, k_table0_offset);
  write_u16(data, k_record_offset + 0x48U, 1);
  write_u16(data, k_record_offset + k_table0_offset + 0x00U, 0xFFFF);
  write_u16(data, k_record_offset + k_table0_offset + 0x02U, 136);
  write_u32(data, k_record_offset + k_table0_offset + 0x04U, initial_xz);
  write_u32(data, k_record_offset + k_table0_offset + 0x08U, 999);
  write_u32(data, k_record_offset + k_table0_offset + 0x0CU, initial_xz);
  write_u16(data, k_record_offset + k_table0_offset + 0x10U, 4090);
  write_u16(data, k_record_offset + k_table0_offset + 0x12U, 136);

  write_u32(data, k_record_offset + 0x28U + (2U * 4U), k_table2_offset);
  write_u16(data, k_record_offset + 0x48U + (2U * 2U), 1);
  write_u32(data, k_record_offset + k_table2_offset, static_cast<std::uint32_t>(zone_event_offset));
  write_u32(data,
      k_record_offset + k_table2_offset + 0x08U,
      static_cast<std::uint32_t>(departure_event_offset));
  // Four X/Y/Z vertices; the Y slab contains the zero-radius synthetic actor.
  constexpr std::array<std::array<std::uint32_t, 3>, 4> k_vertices = {
      {{0U, 900U, 0U}, {100U, 1100U, 0U}, {100U, 1100U, 100U}, {0U, 900U, 100U}}};
  for (std::size_t index{0}; index < k_vertices.size(); ++index) {
    for (std::size_t coordinate{0}; coordinate < k_vertices.at(index).size(); ++coordinate) {
      write_u32(data,
          k_record_offset + k_table2_offset + 0x0CU + ((index * 3U + coordinate) * 4U),
          k_vertices.at(index).at(coordinate));
    }
  }
    write_u16(data,
      k_record_offset + k_table2_offset + 0x3CU,
      static_cast<std::uint16_t>(orientation_center_units));
    write_u16(data,
      k_record_offset + k_table2_offset + 0x3EU,
      static_cast<std::uint16_t>(orientation_span_units));
  write_u16(data,
      k_record_offset + k_table2_offset + 0x40U,
      static_cast<std::uint16_t>(zone_id));
  write_u16(data, k_record_offset + k_table2_offset + 0x42U, 0xFFFF);

  write_u32(data, k_record_offset + 0x28U + (4U * 4U), k_table4_offset);
  write_u16(data, k_record_offset + 0x48U + (4U * 2U), 1);
  constexpr std::string_view k_character_name{"CURRENT CHARACTER"};
  constexpr std::string_view k_model_name{"CURRENT_BODY"};
  constexpr std::string_view k_control_set{"TESTCTL"};
  std::memcpy(data.data() + k_record_offset + k_table4_offset + 0x08U,
      k_character_name.data(),
      k_character_name.size());
  std::memcpy(data.data() + k_record_offset + k_table4_offset + 0x48U,
      k_control_set.data(),
      k_control_set.size());
  std::memcpy(data.data() + k_record_offset + k_table4_offset + 0x90U,
      k_model_name.data(),
      k_model_name.size());
  write_u16(data, k_record_offset + k_table4_offset + 0x110U, 136);
  write_u32(data, k_record_offset + 0x28U + (5U * 4U), static_cast<std::uint32_t>(k_table5_offset));
  write_u16(data, k_record_offset + 0x48U + (5U * 2U), place_before_activation ? 1U : 0U);
  if (place_before_activation) {
    write_u32(data, k_record_offset + k_table5_offset + 0x00U, 500U);
    write_u32(data, k_record_offset + k_table5_offset + 0x04U, 999U);
    write_u32(data, k_record_offset + k_table5_offset + 0x08U, 500U);
    write_u16(data, k_record_offset + k_table5_offset + 0x0CU, 0U);
    write_u16(data, k_record_offset + k_table5_offset + 0x0EU, 44U);
  }
  std::memcpy(data.data() + k_record_offset + k_script_offset,
      top_level.data().data(),
      top_level.data().size());
  std::memcpy(data.data() + k_record_offset + zone_event_offset,
      zone_event.data().data(),
      zone_event.data().size());
    std::memcpy(data.data() + k_record_offset + departure_event_offset,
      departure_event.data().data(),
      departure_event.data().size());
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
  write_area_bytecode_pool_bounds(
      data, k_source_offset, k_source_script_offset, source_record_size);
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
  write_u16(data, k_source_offset + k_source_zone_offset + 0x40U, 5);
  write_u16(data, k_source_offset + k_source_zone_offset + 0x44U + 0x40U, 6);
  std::memcpy(data.data() + k_source_offset + k_source_script_offset,
      handoff.data().data(),
      handoff.data().size());

  write_u32(data, 222U * 8U, static_cast<std::uint32_t>(k_target_offset));
  write_u32(data, (222U * 8U) + 4U, k_target_record_size);
  write_area_bytecode_pool_bounds(
      data, k_target_offset, k_target_record_size, k_target_record_size);
  write_name(data, k_target_offset + 0x61U, "DEST");
  write_u32(data, k_target_offset + 0x28U + (2U * 4U), k_target_zone_offset);
  write_u16(data, k_target_offset + 0x48U + (2U * 2U), 2);
  write_u32(data, k_target_offset + 0x28U + (5U * 4U), k_target_address_offset);
  write_u16(data, k_target_offset + 0x48U + (5U * 2U), 1);
  write_u16(data, k_target_offset + k_target_zone_offset + 0x40U, 5);
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

std::vector<std::byte> make_camera_namespace_area_archive(const CameraNamespaceFixture& fixture) {
  Buffer script;
  script.u8(0x47).u16(118).u16(0);
  script.u8(0x2F).u16(222).u16(0xFFFF).u16(0xFFFF);
  script.u8(0x47).u16(222).u16(1);
  if (!fixture.request_from_slot_1_scene) {
    append_camera_request(script, fixture.tracked);
  }
  script.u8(0x03);

  constexpr std::size_t k_source_offset{0x800U};
  constexpr std::size_t k_target_offset{0xC00U};
  constexpr std::size_t k_header_size{0xB4U};
  constexpr std::size_t k_camera_size{0x2CU};
    constexpr std::size_t k_source_script_offset{k_header_size};
    const std::size_t source_camera_offset{k_source_script_offset + script.data().size()};
    const std::size_t source_size{
      source_camera_offset + (fixture.slot_0_area ? k_camera_size : 0U)};
    constexpr std::size_t k_target_camera_offset{k_header_size};
  const std::size_t target_size{k_header_size + (fixture.slot_1_area ? k_camera_size : 0U)};
  std::vector<std::byte> data(k_target_offset + target_size, std::byte{});

  write_u32(data, 118U * 8U, k_source_offset);
  write_u32(data, (118U * 8U) + 4U, static_cast<std::uint32_t>(source_size));
  write_u32(data, k_source_offset + 0x04U, static_cast<std::uint32_t>(k_source_script_offset));
  write_area_bytecode_pool_bounds(
      data, k_source_offset, k_source_script_offset, source_camera_offset);
  write_name(data, k_source_offset + 0x61U, "GRID");
  write_u16(data, k_source_offset + 0x48U + (6U * 2U), fixture.slot_0_area ? 1U : 0U);
  if (fixture.slot_0_area) {
    write_camera_record(data, k_source_offset + source_camera_offset, 100);
  }
  std::memcpy(data.data() + k_source_offset + k_source_script_offset,
      script.data().data(),
      script.data().size());

  write_u32(data, 222U * 8U, k_target_offset);
  write_u32(data, (222U * 8U) + 4U, static_cast<std::uint32_t>(target_size));
  write_area_bytecode_pool_bounds(
      data, k_target_offset, k_target_camera_offset, k_target_camera_offset);
  write_name(data, k_target_offset + 0x61U, "DEST");
  write_u16(data, k_target_offset + 0x48U + (6U * 2U), fixture.slot_1_area ? 1U : 0U);
  if (fixture.slot_1_area) {
    write_camera_record(data, k_target_offset + k_target_camera_offset, 300);
  }
  return data;
}

std::vector<std::byte> make_camera_namespace_scene_record(const bool has_camera,
    const std::int32_t marker,
    const bool request_camera,
    const bool tracked) {
  Buffer script;
  if (request_camera) {
    append_camera_request(script, tracked);
  }
  script.u8(0x03);

  constexpr std::size_t k_header_size{0x44U};
  constexpr std::size_t k_camera_size{0x2CU};
  const std::size_t camera_offset{k_header_size + script.data().size()};
  std::vector<std::byte> data(camera_offset + (has_camera ? k_camera_size : 0U), std::byte{});
  write_u32(data, 0x04U, k_header_size);
  write_u32(data, 0x08U + (6U * 4U), static_cast<std::uint32_t>(camera_offset));
  write_u16(data, 0x28U + (6U * 2U), has_camera ? 1U : 0U);
  std::memcpy(data.data() + k_header_size, script.data().data(), script.data().size());
  if (has_camera) {
    write_camera_record(data, camera_offset, marker);
  }
  return data;
}

std::vector<std::byte> make_camera_namespace_scene_archive(const CameraNamespaceFixture& fixture) {
  const std::vector<std::byte> source{
      make_camera_namespace_scene_record(fixture.slot_0_scene, 200, false, false)};
  const std::vector<std::byte> target{make_camera_namespace_scene_record(
      fixture.slot_1_scene, 400, fixture.request_from_slot_1_scene, fixture.tracked)};
  constexpr std::size_t k_source_offset{0x800U};
  constexpr std::size_t k_target_offset{0xA00U};
  std::vector<std::byte> data(k_target_offset + target.size(), std::byte{});
  write_u32(data, 0U, k_source_offset);
  write_u32(data, 4U, static_cast<std::uint32_t>(source.size()));
  write_u32(data, 8U, k_target_offset);
  write_u32(data, 12U, static_cast<std::uint32_t>(target.size()));
  std::memcpy(data.data() + k_source_offset, source.data(), source.size());
  std::memcpy(data.data() + k_target_offset, target.data(), target.size());
  return data;
}

/// Minimal valid SCX container (empty descriptor, end tag only).
std::vector<std::byte> make_minimal_scx() {
  Buffer bytes;
  bytes.u32(K_SCX_MAGIC).u32(5).u32(8).u32(4).u32(K_END_TAG);
  return bytes.data();
}

/// Minimal SCX with one immediately completable structured wait command.
std::vector<std::byte> make_kayl_arrives_scx(const std::uint16_t script_id = 1) {
  Buffer descriptor;
  descriptor.u32(K_SCRIPTS_TAG).u32(1);
  descriptor.u32(0).chars("1KaylArrives", 22).u16(script_id).u16(0).u16(0);
  descriptor.u32(1).u32(0).u32(0);           // One root, current index, root placeholder.
  descriptor.u32(0).u32(0);                  // No linked commands, linked placeholder.
  descriptor.u32(0).u32(0);                  // field34 and runtime field38.
  descriptor.zeros(3U * 4U).zeros(3U * 4U);  // Binding-table header fields.
  descriptor.u32(0).u32(0).zeros(8);         // Related/runtime placeholders and tail.
  descriptor.u32(2).f32(0.0F).f32(0.0F);    // Wait duration and elapsed time.
  descriptor.u8(0);                          // No related script.
  descriptor.u32(0x06000017U).u32(2).u32(0).u32(0xFFFFFFFFU).u32(1).u32(0);
  descriptor.u32(0).u32(0);  // Empty binding tables A and B.
  descriptor.u32(K_END_TAG);

  Buffer bytes;
  bytes.u32(K_SCX_MAGIC).u32(5).u32(8).u32(static_cast<std::uint32_t>(descriptor.data().size()));
  for (const std::byte value : descriptor.data()) {
    bytes.u8(std::to_integer<std::uint8_t>(value));
  }
  return bytes.data();
}

/// One fixed 0x64-byte SCX source-script record.
Buffer& append_scx_script_record(Buffer& descriptor,
    const std::string_view name,
    const std::uint16_t script_id,
    const std::uint32_t root_count) {
  descriptor.u32(0)
      .chars(name, 22)
      .u16(script_id)
      .u16(0)
      .u16(0)
      .u32(root_count)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .i32(1)
      .u32(0)
      .zeros(3U * 4U)
      .zeros(3U * 4U)
      .u32(0)
      .u32(0)
      .zeros(8);
  return descriptor;
}

/// Synthetic, fully executable shape of Grid's 1KaylArrives. It deliberately
/// keeps the authored names/command shape but uses tiny in-memory resources so
/// the tracked-child regression has no retail-data dependency.
std::vector<std::byte> make_complete_kayl_arrives_scx() {
  constexpr std::uint32_t k_section0_tag{0xDEAD0000U};
  constexpr std::uint32_t k_animations_tag{0xDEAD0001U};
  constexpr std::uint32_t k_select_relative_body_animation{0x0200002AU};

  Buffer descriptor;
  descriptor.u32(k_section0_tag).u32(1);
  descriptor.chars("Grid_pb.3dp", 24).u32(0).u32(1);
  descriptor.u32(k_animations_tag).u32(1);
  descriptor.chars("INTRO1.3DA", 24).u32(0).u32(0).u32(77);
  descriptor.u32(K_SCRIPTS_TAG).u32(2);
  append_scx_script_record(descriptor, "1KaylArrives", 1, 1);
  append_scx_script_record(descriptor, "KaylContinues", 6, 0);
  descriptor.u32(12);
  descriptor
      .u32(0)     // binding table A index
      .u32(0)     // animation index
      .f32(0.0F)  // mutable previous frame
      .f32(1.0F)  // mutable current frame
      .f32(0.0F)
      .f32(0.0F)
      .f32(0.0F)
      .u32(0)  // path index
      .u32(0)  // subpath index
      .f32(0.0F)
      .f32(0.0F)
      .f32(0.0F);
  descriptor.u8(0);  // 1KaylArrives has no related script.
  descriptor.u32(k_select_relative_body_animation).u32(12).u32(0).i32(-1).u32(1).u32(0);
  descriptor.u32(1).u64(0).chars("UBassin", 21);  // binding table A
  descriptor.u32(0);                              // binding table B
  descriptor.u8(0);                               // script 6 has no related script.
  descriptor.u32(0).u32(0);                       // empty binding tables
  descriptor.u32(K_END_TAG);

  Buffer path;
  path.u32(1).chars("UBas.p1", 20).u32(2).u32(3);
  for (std::uint32_t key{0}; key < 3U; ++key) {
    path.u32(key).f32(10.0F).f32(20.0F).f32(30.0F).f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
  }

  constexpr std::uint32_t k_animation_descriptor_end{8U + 0x28U};
  constexpr std::uint32_t k_rotation_offset{k_animation_descriptor_end + (3U * 12U)};
  Buffer animation;
  animation.u32(2).u32(1);
  animation.u32(1)
      .chars("UBassin", 20)
      .u32(3)
      .u32(k_animation_descriptor_end)
      .u32(3)
      .u32(k_rotation_offset);
  for (std::uint32_t frame{0}; frame < 3U; ++frame) {
    animation.f32(0.0F).f32(0.0F).f32(0.0F);
  }
  for (std::uint32_t frame{0}; frame < 3U; ++frame) {
    animation.f32(1.0F).f32(0.0F).f32(0.0F).f32(0.0F);
  }

  Buffer header;
  header.u32(K_SCX_MAGIC).u32(5).u32(8).u32(static_cast<std::uint32_t>(descriptor.data().size()));
  std::vector<std::byte> result{header.data()};
  result.insert(result.end(), descriptor.data().begin(), descriptor.data().end());
  const auto append_resource = [&result](const std::vector<std::byte>& payload) {
    const std::size_t resource_offset{result.size()};
    result.resize(resource_offset + 8U, std::byte{});
    write_u32(result, resource_offset, static_cast<std::uint32_t>(resource_offset));
    write_u32(result, resource_offset + 4U, static_cast<std::uint32_t>(payload.size()));
    result.insert(result.end(), payload.begin(), payload.end());
  };
  append_resource(path.data());
  append_resource(animation.data());
  return result;
}

std::vector<std::byte> make_dialog_archive(const std::uint16_t dialog_id) {
  Buffer record;
  record.u16(310).u16(1).u16(0).u16(0);
  record.zeros(0x10U);  // Four condition offsets.
  record.zeros(0x10U);  // Four action offsets.
  record.u32(0x48U);
  record.u16(0xFFFFU).u16(0xFFFFU).u16(0xFFFFU).u16(0xFFFFU);
  record.u16(0).chars("FACE", 10);
  record.u16(0xFFFFU).u16(0xFFFFU).u16(0xFFFFU).u16(0xFFFFU);
  record.chars("Portal dialog", 15).zeros(5);

  const std::size_t entry{(static_cast<std::size_t>(dialog_id >> 8U) * 0x800U) +
                          (static_cast<std::size_t>(dialog_id & 0xFFU) * 8U)};
  constexpr std::size_t k_record_offset{0x1000U};
  std::vector<std::byte> archive(k_record_offset + record.data().size(), std::byte{});
  write_u32(archive, entry, k_record_offset);
  write_u32(archive, entry + 4U, static_cast<std::uint32_t>(record.data().size()));
  std::memcpy(archive.data() + k_record_offset, record.data().data(), record.data().size());
  return archive;
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
  write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
  write_bytes(temp.root() / "IAM" / "AREA", make_area_archive());
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
  write_bytes(temp.root() / "MESHES" / "DECORS" / "GRID.3DO", make_minimal_3do());
}

void write_zone_contact_fixtures(const TempDirectory& temp,
  const bool enable_controller = true,
  const std::int16_t zone_id = 3795,
  const bool controller_off_before_wait = false,
  const bool place_before_activation = false,
  const std::uint32_t initial_xz = 50U,
  const std::int16_t orientation_center_units = 4090,
  const std::int16_t orientation_span_units = 0,
  const bool launch_fire_and_forget = false) {
  write_bytes(temp.root() / "IAM" / "START", make_start());
  write_bytes(temp.root() / "IAM" / "GLOBAL", make_camera_namespace_global(true));
    write_bytes(temp.root() / "IAM" / "AREA",
      make_zone_contact_area_archive(
        false,
        true,
        enable_controller,
        zone_id,
        controller_off_before_wait,
        place_before_activation,
        initial_xz,
        orientation_center_units,
          orientation_span_units,
          launch_fire_and_forget));
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
        write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX",
        launch_fire_and_forget ? make_kayl_arrives_scx(221) : make_minimal_scx());
}

void write_live_zone_contact_fixtures(const TempDirectory& temp) {
  write_bytes(temp.root() / "IAM" / "START", make_start());
  write_bytes(temp.root() / "IAM" / "GLOBAL", make_camera_namespace_global(true));
  write_bytes(temp.root() / "IAM" / "AREA", make_zone_contact_area_archive(false, false));
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
}

void write_zone_dialog_fixtures(const TempDirectory& temp) {
  write_bytes(temp.root() / "IAM" / "START", make_start());
  write_bytes(temp.root() / "IAM" / "GLOBAL", make_camera_namespace_global(true));
  write_bytes(temp.root() / "IAM" / "AREA", make_zone_contact_area_archive(true));
  write_bytes(temp.root() / "IAM" / "DIALOG", make_dialog_archive(272));
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
}

void write_current_character_script_fixtures(const TempDirectory& temp) {
  Buffer script;
  script.u8(0x2E).u16(221).u16(0).u8(0x03);
  std::vector<std::byte> area{make_area_archive()};
  std::memcpy(area.data() + 0x800U + 0x3FCU, script.data().data(), script.data().size());

  write_bytes(temp.root() / "IAM" / "START", make_start());
  write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
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
  write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
  write_bytes(temp.root() / "IAM" / "AREA", area);
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
  write_bytes(temp.root() / "MESHES" / "DECORS" / "GRID.3DO", make_minimal_3do());
}

void write_handoff_fixtures(const TempDirectory& temp) {
  std::vector<std::byte> start{make_start()};
  start.at(0x13FCU) = std::byte{0x40};  // ZONE 6 starts persistently enabled.
  write_bytes(temp.root() / "IAM" / "START", start);
  write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
  write_bytes(temp.root() / "IAM" / "AREA", make_handoff_area_archive());
  write_bytes(temp.root() / "IAM" / "SCENE", make_handoff_scene_archive());
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "DEST.SCX", make_kayl_arrives_scx(221));
}

void write_camera_namespace_fixtures(
    const TempDirectory& temp, const CameraNamespaceFixture& fixture) {
  write_bytes(temp.root() / "IAM" / "START", make_start());
  write_bytes(temp.root() / "IAM" / "GLOBAL", make_camera_namespace_global(fixture.global));
  write_bytes(temp.root() / "IAM" / "AREA", make_camera_namespace_area_archive(fixture));
  write_bytes(temp.root() / "IAM" / "SCENE", make_camera_namespace_scene_archive(fixture));
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "DEST.SCX", make_minimal_scx());
}

}  // namespace

TEST_SUITE("Core::Scenario::ScenarioStartupController") {
  TEST_CASE("new session requires a valid IAM/GLOBAL before compact execution") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    const ScopedGameDataRoot root{temp.root()};

    SUBCASE("missing GLOBAL") {
      std::filesystem::remove(temp.root() / "IAM" / "GLOBAL");
      App::ScenarioManager manager;
      App::ScenarioStartupController controller;
      const auto result{controller.initialize(manager)};
      REQUIRE_FALSE(result.has_value());
      CHECK(result.error().find("IAM/GLOBAL") != std::string::npos);
    }

    SUBCASE("malformed GLOBAL") {
      write_bytes(temp.root() / "IAM" / "GLOBAL", std::vector<std::byte>(0x1FU, std::byte{}));
      App::ScenarioManager manager;
      App::ScenarioStartupController controller;
      const auto result{controller.initialize(manager)};
      REQUIRE_FALSE(result.has_value());
      CHECK(result.error().find("IAM/GLOBAL: too small") != std::string::npos);
    }
  }

  TEST_CASE("compact cameras use resident slot order then GLOBAL regardless of active slot") {
    struct LookupCase {
      CameraNamespaceFixture fixture;
      std::int32_t expected_marker{0};
    };
    const std::array cases{LookupCase{.fixture = {.slot_0_area = true,
                                          .slot_0_scene = true,
                                          .slot_1_area = true,
                                          .slot_1_scene = true,
                                          .global = true},
                               .expected_marker = 100},
        LookupCase{.fixture = {.slot_0_scene = true,
                       .slot_1_area = true,
                       .slot_1_scene = true,
                       .global = true},
            .expected_marker = 200},
        LookupCase{.fixture = {.slot_1_area = true, .slot_1_scene = true, .global = true},
            .expected_marker = 300},
        LookupCase{.fixture = {.slot_1_scene = true, .global = true}, .expected_marker = 400},
        LookupCase{.fixture = {.global = true}, .expected_marker = 500}};

    for (const LookupCase& lookup : cases) {
      const TempDirectory temp;
      write_camera_namespace_fixtures(temp, lookup.fixture);
      const ScopedGameDataRoot root{temp.root()};
      App::ScenarioManager manager;
      App::ScenarioStartupController controller;
      REQUIRE(controller.initialize(manager).has_value());
      REQUIRE(controller.tick().has_value());
      REQUIRE(controller.tick().has_value());
      CHECK_EQ(controller.active_area_slot(), 1U);
      REQUIRE_EQ(manager.world_presentation().pending_camera_count(), 1U);
      const auto command{manager.world_presentation().take_camera()};
      REQUIRE(command.has_value());
      CHECK_EQ(command->serialized_eye.at(0), lookup.expected_marker);
      CHECK_EQ(command->horizontal_fov_units, 800 + lookup.expected_marker);
    }
  }

  TEST_CASE("slot1 SCENE caller does not reorder definitions and retains owner metadata") {
    SUBCASE("slot0 AREA shadows the caller's slot1 SCENE") {
      const TempDirectory temp;
      write_camera_namespace_fixtures(temp,
          CameraNamespaceFixture{
              .slot_0_area = true, .slot_1_scene = true, .request_from_slot_1_scene = true});
      const ScopedGameDataRoot root{temp.root()};
      App::ScenarioManager manager;
      App::ScenarioStartupController controller;
      REQUIRE(controller.initialize(manager).has_value());
      REQUIRE(controller.tick().has_value());
      REQUIRE(controller.tick().has_value());
      const auto command{manager.world_presentation().take_camera()};
      REQUIRE(command.has_value());
      CHECK_EQ(command->serialized_eye.at(0), 100);
      const App::RuntimeAreaSlot* owner{controller.runtime_area_slot(1)};
      REQUIRE(owner != nullptr);
      const auto contexts{manager.world_contexts()};
      const App::WorldSceneContext& context{contexts[owner->world_scene_id]};
      CHECK_EQ(command->scene_id, context.scene_id);
      CHECK_EQ(command->scene_generation, context.generation);
      CHECK_EQ(command->source_area_id, 222);
    }

    SUBCASE("GLOBAL supplies fields without becoming the owner world") {
      const TempDirectory temp;
      write_camera_namespace_fixtures(
          temp, CameraNamespaceFixture{.global = true, .request_from_slot_1_scene = true});
      const ScopedGameDataRoot root{temp.root()};
      App::ScenarioManager manager;
      App::ScenarioStartupController controller;
      REQUIRE(controller.initialize(manager).has_value());
      REQUIRE(controller.tick().has_value());
      REQUIRE(controller.tick().has_value());
      const auto command{manager.world_presentation().take_camera()};
      REQUIRE(command.has_value());
      CHECK_EQ(command->serialized_eye.at(0), 500);
      const App::RuntimeAreaSlot* owner{controller.runtime_area_slot(1)};
      REQUIRE(owner != nullptr);
      const auto contexts{manager.world_contexts()};
      const App::WorldSceneContext& context{contexts[owner->world_scene_id]};
      CHECK_EQ(command->scene_id, context.scene_id);
      CHECK_EQ(command->scene_generation, context.generation);
      CHECK_EQ(command->source_area_id, 222);
    }
  }

  TEST_CASE("missing camera is a no-op without emitting a command or blocking") {
    const TempDirectory temp;
    write_camera_namespace_fixtures(temp, CameraNamespaceFixture{.tracked = true});
    const ScopedGameDataRoot root{temp.root()};
    App::ScenarioManager manager;
    App::ScenarioStartupController controller;
    REQUIRE(controller.initialize(manager).has_value());
    REQUIRE(controller.tick().has_value());
    const auto result{controller.tick()};
    REQUIRE(result.has_value());
    CHECK_EQ(manager.world_presentation().pending_camera_count(), 0U);
  }

  TEST_CASE("tracked GLOBAL camera completion resumes the exact slot1 SCENE context") {
    const TempDirectory temp;
    write_camera_namespace_fixtures(temp,
        CameraNamespaceFixture{.global = true, .request_from_slot_1_scene = true, .tracked = true});
    const ScopedGameDataRoot root{temp.root()};
    App::ScenarioManager manager;
    App::ScenarioStartupController controller;
    REQUIRE(controller.initialize(manager).has_value());
    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick().has_value());
    const auto command{manager.world_presentation().take_camera()};
    REQUIRE(command.has_value());
    REQUIRE(command->operation_generation.has_value());
    const App::RuntimeAreaSlot* owner{controller.runtime_area_slot(1)};
    REQUIRE(owner != nullptr);
    REQUIRE(owner->scene_script.has_value());
    CHECK(owner->scene_script->state() == AreaScriptState::k_waiting);
    CHECK_EQ(owner->scene_script->wait_state(), 7);
    manager.world_presentation().enqueue_camera_completion(App::WorldCameraOperationCompletion{
        .operation_generation = command->operation_generation.value(),
        .scene_id = command->scene_id,
        .scene_generation = command->scene_generation,
        .source_area_id = command->source_area_id,
        .camera_id = command->camera_id});
    REQUIRE(controller.tick().has_value());
    owner = controller.runtime_area_slot(1);
    REQUIRE(owner != nullptr);
    REQUIRE(owner->scene_script.has_value());
    CHECK(owner->scene_script->state() == AreaScriptState::k_ready);
  }

  TEST_CASE("AREA-shaped zone contact runs record-relative event one through self-deactivation") {
    const TempDirectory temp;
    write_zone_contact_fixtures(temp);
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
    install_stub_ctl_loader(*runtime);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(manager.game_state() != nullptr);
    CHECK_FALSE(manager.game_state()->zone_flag(3795).value());
    CHECK_EQ(controller.zone_contact_count(), 1U);
    const App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    CHECK_EQ(character->current_move_id(), std::optional<std::int16_t>{100});
    CHECK(character->controller_enabled);

    // 0x41 refreshed the persistent zone table, but the context must remain
    // alive long enough to complete its state-7 camera wait and EndEvent.
    const auto camera{manager.world_presentation().take_camera()};
    REQUIRE(camera.has_value());
    REQUIRE(camera->operation_generation.has_value());
    manager.world_presentation().enqueue_camera_completion(App::WorldCameraOperationCompletion{
        .operation_generation = camera->operation_generation.value(),
        .scene_id = camera->scene_id,
        .scene_generation = camera->scene_generation,
        .source_area_id = camera->source_area_id,
        .camera_id = camera->camera_id});
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 0U);
    CHECK_FALSE(character->controller_enabled);

    // The disabled zone is not recreated while the actor remains inside it.
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 0U);
  }

  TEST_CASE("registered proxy permits fresh contact while controller is disabled") {
    constexpr std::int16_t k_zone_id{12};
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, k_zone_id);
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
    install_stub_ctl_loader(*runtime);

    REQUIRE(manager.game_state() != nullptr);
    CHECK_FALSE(manager.game_state()->zone_flag(k_zone_id).value());
    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    REQUIRE(character->ctl_controller.has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    const App::CurrentCharacterTriggerProxy proxy{
      controller.current_character_trigger_proxy().value()};
    CHECK(proxy.registered);
    CHECK_EQ(proxy.owner.character_id, 136);
    CHECK_EQ(proxy.owner.world_scene_id, 0U);
    CHECK_EQ(controller.zone_contact_count(), 1U);
    CHECK_EQ(character->current_move_id(), std::optional<std::int16_t>{100});
    CHECK(character->controller_enabled);
    CHECK(character->ctl_controller->direct_control_active());
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x, proxy.position.x);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.y, proxy.position.y);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.z, proxy.position.z);
  }

  TEST_CASE("zone contact in progress survives controller-off inside its event") {
    constexpr std::int16_t k_zone_id{13};
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, true, k_zone_id, true);
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
    install_stub_ctl_loader(*runtime);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    REQUIRE(character->ctl_controller.has_value());
    CHECK_EQ(character->current_move_id(), std::optional<std::int16_t>{100});
    CHECK_FALSE(character->controller_enabled);
    CHECK_FALSE(character->ctl_controller->direct_control_active());
    CHECK_EQ(controller.zone_contact_count(), 1U);
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK(controller.current_character_trigger_proxy()->registered);

    const App::ZoneContactContext* contact{controller.zone_contact(0)};
    REQUIRE(contact != nullptr);
    REQUIRE(contact->script != nullptr);
    CHECK(contact->script->state() == AreaScriptState::k_waiting);
    CHECK(contact->script->wait_info().kind == App::Script::AreaWaitKind::k_camera);
  }

  TEST_CASE("compact address placement does not synchronize the trigger proxy") {
    constexpr std::int16_t k_zone_id{14};
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, k_zone_id, false, true);
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
    install_stub_ctl_loader(*runtime);

    REQUIRE(controller.tick().has_value());
    const App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK_EQ(character->serialized_area_position.at(0), 500);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x,
        App::Runtime::area_position_to_inches(50));
    CHECK(controller.current_character_trigger_proxy()->synchronization_suspended);
    CHECK_EQ(controller.zone_contact_count(), 0U);
  }

  TEST_CASE("ordinary proxy synchronization may create contact while controller is disabled") {
    constexpr std::int16_t k_zone_id{15};
    const TempDirectory temp;
    write_zone_contact_fixtures(temp, false, k_zone_id, false, false, 500U);
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
    install_stub_ctl_loader(*runtime);

    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    CHECK_FALSE(character->controller_enabled);
    CHECK_EQ(controller.zone_contact_count(), 0U);
    character->transform.translation =
        App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{50, 999, 50});

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 1U);
    CHECK_EQ(character->current_move_id(), std::optional<std::int16_t>{100});
    CHECK(character->controller_enabled);
  }

  TEST_CASE("proxy qualification uses live degree heading instead of placement yaw") {
    constexpr std::int16_t k_zone_id{16};
    constexpr std::int16_t k_quarter_turn_units{1024};
    const TempDirectory temp;
    write_zone_contact_fixtures(
        temp, false, k_zone_id, false, false, 500U, k_quarter_turn_units, 100);
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
    install_stub_ctl_loader(*runtime);

    REQUIRE(controller.tick().has_value());
    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    CHECK_EQ(character->serialized_orientation_units, 4090);
    character->transform.translation =
        App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{50, 999, 50});
    character->set_principal_orientation(App::Runtime::Vec3{.x = 0.0F, .y = 90.0F, .z = 0.0F});

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK_EQ(controller.current_character_trigger_proxy()->heading_degrees, 90.0F);
    CHECK_EQ(controller.zone_contact_count(), 1U);
  }

  TEST_CASE("fire-and-forget current-character script freezes proxy synchronization") {
    constexpr std::int16_t k_zone_id{17};
    const TempDirectory temp;
    write_zone_contact_fixtures(
        temp, false, k_zone_id, false, false, 50U, 4090, 0, true);
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
    install_stub_ctl_loader(*runtime);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.current_character_trigger_proxy().has_value());
    CHECK(controller.current_character_trigger_proxy()->synchronization_suspended);
    CHECK_FALSE(controller.current_character_trigger_proxy()->contact_ready);
    CHECK_EQ(controller.zone_contact_count(), 0U);
    const App::Script::ScriptRuntime* scripts{runtime->script_runtime()};
    REQUIRE(scripts != nullptr);
    REQUIRE_EQ(scripts->instances().size(), 1U);
    CHECK_FALSE(scripts->instances().front().completed);
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK(controller.current_character_trigger_proxy()->synchronization_suspended);
    CHECK_EQ(controller.current_character_trigger_proxy()->suspension_reason,
      "current-character structured script active");
    CHECK_FALSE(controller.current_character_trigger_proxy()->contact_ready);
    CHECK_EQ(controller.zone_contact_count(), 0U);
  }

  TEST_CASE("zone contact follows live runtime position instead of the AREA placement snapshot") {
    const TempDirectory temp;
    write_live_zone_contact_fixtures(temp);
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
    install_stub_ctl_loader(*runtime);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 1U);
    REQUIRE(manager.game_state() != nullptr);
    CHECK(manager.game_state()->zone_flag(3795).value());

    App::Character::RuntimeCharacter* character{runtime->character_runtime().find(136)};
    REQUIRE(character != nullptr);
    CHECK_EQ(character->serialized_area_position.at(0), 50);
    CHECK_EQ(character->serialized_area_position.at(2), 50);

    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 1U);

    const auto camera{manager.world_presentation().take_camera()};
    REQUIRE(camera.has_value());
    REQUIRE(camera->operation_generation.has_value());
    manager.world_presentation().enqueue_camera_completion(App::WorldCameraOperationCompletion{
        .operation_generation = camera->operation_generation.value(),
        .scene_id = camera->scene_id,
        .scene_generation = camera->scene_generation,
        .source_area_id = camera->source_area_id,
        .camera_id = camera->camera_id});

    // Leave X/Z inside but move the live actor above the zone's Y slab. The
    // proxy's 3D broadphase must reject the contact before the polygon test.
    character->transform.translation.y = 1000.0F;
    REQUIRE(controller.tick(1.0F).has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 0U);
    CHECK(manager.game_state()->zone_flag(3795).value());

    // Now make the serialized snapshot explicitly outside while putting the
    // continuous live transform back inside all three proxy dimensions.
    character->serialized_area_position = {10000, 999, 10000};
    character->transform.translation =
        App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{50, 999, 50});
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 1U);
    CHECK_EQ(character->current_move_id(), std::optional<std::int16_t>{100});
    CHECK(character->controller_enabled);
  }

  TEST_CASE("zone-owned StartDialog enters the shared global dialog takeover") {
    const TempDirectory temp;
    write_zone_dialog_fixtures(temp);
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
    install_stub_ctl_loader(*runtime);

    REQUIRE(controller.tick().has_value());
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.zone_contact_count(), 1U);
    CHECK(controller.dialog_takeover_active());
    CHECK_EQ(controller.dialog_takeover_id(), std::optional<std::int16_t>{272});
    CHECK(manager.dialog_runtime().active());
  }

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

    REQUIRE(controller.current_character_trigger_proxy().has_value());
    const App::Runtime::Vec3 frozen_position{
      controller.current_character_trigger_proxy()->position};
    CHECK(controller.current_character_trigger_proxy()->synchronization_suspended);
    App::Character::RuntimeCharacter* mutable_destination_character{
      destination_runtime->character_runtime().find(57)};
    REQUIRE(mutable_destination_character != nullptr);
    mutable_destination_character->transform.translation.x += 100.0F;

    // Compact servicing observes the still-active structured child and leaves
    // the proxy frozen despite visible/logical actor movement.
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x, frozen_position.x);
    CHECK(controller.current_character_trigger_proxy()->synchronization_suspended);

    // The child completes in the later world-runtime stage. There is no
    // same-update proxy callback; the following ordinary scenario tick catches up.
    destination_runtime->tick(1.0F / 30.0F);
    CHECK(instance->completed);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x, frozen_position.x);
    REQUIRE(controller.tick(1.0F / 30.0F).has_value());
    CHECK_FALSE(controller.current_character_trigger_proxy()->synchronization_suspended);
    CHECK_EQ(controller.current_character_trigger_proxy()->position.x,
      mutable_destination_character->transform.translation.x);
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
    CHECK_EQ(controller.area_record()->primary_event_offset(), 0x3FCU);
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
    CHECK_FALSE(controller.main_menu_active());
    REQUIRE(controller.tick().has_value());

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

  TEST_CASE("New Game tracked 1KaylArrives completes and reaches dialog 272") {
    const TempDirectory temp;
    write_boot_fixtures(temp);
    write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_complete_kayl_arrives_scx());
    write_bytes(temp.root() / "IAM" / "DIALOG", make_dialog_archive(272));
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
          resource->model.materials.push_back(App::Omikron::Material{});
          resource->model.meshes.push_back(App::Omikron::MeshDescriptor{.mesh_id = 1U,
              .script_id = 1U,
              .name = "UBassin",
              .parent_id = -1,
              .first_child_id = -1,
              .next_sibling_id = -1});
            resource->model.polygons.resize(1U);
          resource->model.root_mesh_index = 0;
          resource->actor_object_index = 0U;
          resource->model.hierarchy_parent_index = {-1};
          resource->model.hierarchy_first_child_index = {-1};
          resource->model.hierarchy_next_sibling_index = {-1};
          resource->model.hierarchy_reachable = {1U};
          resource->model.skin_parent_index = {-1};
          resource->model.runtime_objects = {App::Omikron::Model3DOData::RuntimeObjectState{}};
          resource->groups.push_back(App::Omikron::MaterialGroup{});
          return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
        });

    REQUIRE(controller.tick().has_value());  // Unresolved 0x5F yields before interface 29.
    REQUIRE(controller.tick().has_value());
    REQUIRE(controller
            .complete_interface(App::InterfaceCompletion{
                .handle = App::InterfaceHandle{.interface_id = 29, .generation = 1}, .result = 3})
            .has_value());
    REQUIRE_EQ(manager.world_presentation().pending_fade_count(), 1U);
    const auto bootstrap_fade{manager.world_presentation().take_fade()};
    REQUIRE(bootstrap_fade.has_value());
    CHECK_EQ(bootstrap_fade->mode, 1U);
    CHECK_EQ(bootstrap_fade->color, 0U);
    CHECK_EQ(bootstrap_fade->duration_units, 0);
    CHECK_EQ(bootstrap_fade->delay_units, 0);
    REQUIRE(controller.tick().has_value());  // 0x84 intent, then 0x77 yield.
    REQUIRE_EQ(manager.world_presentation().pending_letterbox_count(), 1U);
    const auto letterbox{manager.world_presentation().take_letterbox()};
    REQUIRE(letterbox.has_value());
    CHECK(letterbox->enabled);
    REQUIRE_EQ(manager.world_presentation().pending_fade_count(), 1U);
    const auto fade{manager.world_presentation().take_fade()};
    REQUIRE(fade.has_value());
    CHECK_EQ(fade->mode, 2U);
    CHECK_EQ(fade->color, 0U);
    CHECK_EQ(fade->duration_units, 30);
    CHECK_EQ(fade->delay_units, 0);
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
    CHECK_EQ(area_script->instruction_pointer(), 0x498U);
    CHECK(area_script->wait_info().kind == App::Script::AreaWaitKind::k_character_script);
    REQUIRE(area_script->wait_info().character_script_instance.has_value());
    REQUIRE(area_script->last_character_script_request().has_value());
    CHECK(area_script->last_character_script_request()->target ==
          App::Script::AreaCharacterScriptTarget::k_explicit);
    CHECK_EQ(area_script->last_character_script_request()->character_id,
        std::optional<std::int16_t>{310});
    CHECK_EQ(area_script->last_character_script_request()->camera_duration_units, 0);

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
    CHECK_FALSE(instance->paused);
    CHECK_FALSE(instance->completed);
    context->runtime->tick(1.0F / 30.0F);
    CHECK(instance->completed);

    // Startup polls the exact completed child, resumes at record offset 0x498, launches
    // script 310/6 once, yields for presentation, then enters DIALOG 272.
    REQUIRE(controller.tick().has_value());
    CHECK_EQ(area_script->instruction_pointer(), 0x4A8U);
    REQUIRE_EQ(script_runtime->instances().size(), 2U);
    REQUIRE(controller.tick().has_value());
    CHECK(controller.dialog_takeover_active());
    CHECK_EQ(controller.dialog_takeover_id(), std::optional<std::int16_t>{272});
    CHECK(manager.dialog_runtime().active());
    CHECK_EQ(area_script->runtime_state(), 1U);
    CHECK_EQ(area_script->instruction_pointer(), 0x4ABU);
    CHECK(area_script->wait_info().kind == App::Script::AreaWaitKind::k_none);
  }

  TEST_CASE("OBJECTS submit authored Runtime world text with raw-byte lifetime") {
    const TempDirectory temp;
    const std::string authored_text{"{fD}You have been the victim of a violent attack..."};
    write_bytes(temp.root() / "IAM" / "START", make_start());
    write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
    write_bytes(temp.root() / "IAM" / "AREA", make_object_activation_area_archive(141U));
    write_bytes(temp.root() / "IAM" / "OBJECT",
        make_object_archive(141U, 7U, "ZVO M010 Agression", authored_text));
    write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
    write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
    const ScopedGameDataRoot root{temp.root()};
    App::ScenarioManager manager;
    App::ScenarioStartupController controller;
    REQUIRE(controller.initialize(manager).has_value());
    REQUIRE(controller.tick().has_value());

    const auto voice{manager.world_presentation().take_voice_over()};
    REQUIRE(voice.has_value());
    CHECK_EQ(voice->object_id, 141);
    CHECK_EQ(voice->audio_path, "VOICEOFF/ZVO M010 Agression.ADP");
    const auto world_text{manager.world_presentation().take_world_text()};
    REQUIRE(world_text.has_value());
    CHECK_EQ(world_text->document.authored_bytes(), authored_text);
    CHECK_EQ(App::Interface::runtime_text_plain_bytes(world_text->document),
        "You have been the victim of a violent attack...");
    CHECK_EQ(world_text->duration_ms, static_cast<std::uint32_t>(authored_text.size()) * 80U);
    CHECK_EQ(world_text->provenance.source_kind, App::TextSourceKind::k_iam_object);
    CHECK_EQ(world_text->provenance.object_id, 141);
    CHECK_EQ(world_text->provenance.audio_resource, "VOICEOFF/ZVO M010 Agression.ADP");
    CHECK_EQ(world_text->provenance.role, App::TextPresentationRole::k_unknown);
    CHECK_EQ(
        world_text->provenance.modernization_policy, App::TextModernizationPolicy::k_faithful_only);
    const App::Script::AreaScriptRuntime* runtime{controller.area_script()};
    REQUIRE(runtime != nullptr);
    CHECK(runtime->last_run_yielded());
    CHECK(runtime->wait_info().kind == App::Script::AreaWaitKind::k_none);
  }

  TEST_CASE("OBJECTS ZVOT substitution, -1, and type 0x10 remain safe") {
    SUBCASE("ZVOT uses the recovered JINGOFF3 replacement") {
      const TempDirectory temp;
      write_bytes(temp.root() / "IAM" / "START", make_start());
      write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
      write_bytes(temp.root() / "IAM" / "AREA", make_object_activation_area_archive(141U));
      write_bytes(
          temp.root() / "IAM" / "OBJECT", make_object_archive(141U, 0U, "ZVOT Intro", "short"));
      write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
      write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
      const ScopedGameDataRoot root{temp.root()};
      App::ScenarioManager manager;
      App::ScenarioStartupController controller;
      REQUIRE(controller.initialize(manager).has_value());
      REQUIRE(controller.tick().has_value());
      const auto voice{manager.world_presentation().take_voice_over()};
      REQUIRE(voice.has_value());
      CHECK_EQ(voice->audio_path, "VOICEOFF/JINGOFF3.ADP");
      const auto world_text{manager.world_presentation().take_world_text()};
      REQUIRE(world_text.has_value());
      CHECK_EQ(world_text->duration_ms, 2000U);
    }

    SUBCASE("-1 does not require IAM/OBJECT") {
      const TempDirectory temp;
      write_bytes(temp.root() / "IAM" / "START", make_start());
      write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
      write_bytes(temp.root() / "IAM" / "AREA", make_object_activation_area_archive(0xFFFFU));
      write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
      write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
      const ScopedGameDataRoot root{temp.root()};
      App::ScenarioManager manager;
      App::ScenarioStartupController controller;
      REQUIRE(controller.initialize(manager).has_value());
      REQUIRE(controller.tick().has_value());
      CHECK_EQ(manager.world_presentation().pending_voice_over_count(), 0U);
      CHECK_EQ(manager.world_presentation().pending_world_text_count(), 0U);
    }

    SUBCASE("type 0x10 is not treated as voice-over") {
      const TempDirectory temp;
      write_bytes(temp.root() / "IAM" / "START", make_start());
      write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
      write_bytes(temp.root() / "IAM" / "AREA", make_object_activation_area_archive(141U));
      write_bytes(temp.root() / "IAM" / "OBJECT",
          make_object_archive(141U, 0x10U, "NOT_A_VOICE", "not speech"));
      write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
      write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
      const ScopedGameDataRoot root{temp.root()};
      App::ScenarioManager manager;
      App::ScenarioStartupController controller;
      REQUIRE(controller.initialize(manager).has_value());
      REQUIRE(controller.tick().has_value());
      CHECK_EQ(manager.world_presentation().pending_voice_over_count(), 0U);
      CHECK_EQ(manager.world_presentation().pending_world_text_count(), 0U);
    }
  }

  TEST_CASE("0x47 maps a nonresident AREA without loading its SCENE") {
    const TempDirectory temp;
    write_bytes(temp.root() / "IAM" / "START", make_start());
    write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());

    std::vector<std::byte> area_archive{make_area_archive()};
    Buffer script;
    script.u8(0x47).u16(237).u16(57).u8(0x03);
    constexpr std::size_t k_record_offset{0x800U};
    constexpr std::size_t k_script_offset{0x3FCU};
    std::memcpy(area_archive.data() + k_record_offset + k_script_offset,
        script.data().data(),
        script.data().size());
    write_bytes(temp.root() / "IAM" / "AREA", area_archive);

    // Deliberately do not provide IAM/SCENE. Runtime's nonresident 0x47 path
    // updates only the AREA -> SCENE mapping and cannot require SCENE bytes.
    write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
    write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());

    const ScopedGameDataRoot root{temp.root()};
    App::ScenarioManager manager;
    App::ScenarioStartupController controller;
    REQUIRE(controller.initialize(manager).has_value());
    REQUIRE(controller.tick().has_value());

    CHECK_EQ(controller.area_mapping(237), std::optional<std::int32_t>{57});
    REQUIRE(manager.game_state() != nullptr);
    CHECK_EQ(manager.game_state()->current_area(), 118);
    CHECK_EQ(controller.active_area_slot(), 0U);

    const App::RuntimeAreaSlot* const source{controller.runtime_area_slot(0)};
    const App::RuntimeAreaSlot* const alternate{controller.runtime_area_slot(1)};
    REQUIRE(source != nullptr);
    REQUIRE(alternate != nullptr);
    CHECK_EQ(source->primary_area_id, 118);
    CHECK_FALSE(alternate->primary.has_value());
    CHECK_EQ(alternate->primary_area_id, -1);

    REQUIRE_EQ(manager.world_contexts().size(), 2U);
    CHECK(manager.world_contexts()[0].residency == WorldSceneResidencyState::LoadedActive);
    CHECK(manager.world_contexts()[1].residency == WorldSceneResidencyState::Free);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

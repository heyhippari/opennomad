#pragma once

// Shared generated-fixture support for session-level specs (ScenarioStartup, AreaTransitionService):
// synthetic IAM/START, IAM/AREA, IAM/SCENE and SCX archives, a temporary game-data root, and
// StartedSession, which starts a new session with loaders injected before the first AREA preload.
// Helpers live in an anonymous namespace so each spec keeps its own copy; not every spec uses
// every helper.

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

#include <doctest/doctest.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <initializer_list>
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
#include "Core/Startup/StartupTraceRecorder.hpp"
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

/// START whose persistent CHARACTERS bit is set, so the AREA loader materializes the matching
/// table-0 placement as an initially active body (native behavior: the bit only controls the
/// initial active/present state of the resident actor).
std::vector<std::byte> make_start_with_active_character_bits(
    const std::initializer_list<std::uint16_t> state_bit_indices) {
  constexpr std::size_t k_character_flags_offset{0x1314U};
  std::vector<std::byte> start{make_start()};
  for (const std::uint16_t state_bit_index : state_bit_indices) {
    const std::size_t byte_index{k_character_flags_offset + (state_bit_index / 8U)};
    start.at(byte_index) = static_cast<std::byte>(
        std::to_integer<unsigned>(start.at(byte_index)) | (1U << (state_bit_index % 8U)));
  }
  return start;
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

void install_resident_model_loader(
    App::Character::Runtime& runtime, const App::Character::Runtime::ModelLoader& model_loader) {
  runtime.set_model_loader(model_loader);
  for (const App::Character::RuntimeCharacter& resident : runtime.characters()) {
    App::Character::RuntimeCharacter* const character{runtime.find_body(resident.body_identity)};
    REQUIRE(character != nullptr);
    auto resource{model_loader(character->model_resource_name)};
    REQUIRE(resource.has_value());
    character->model_resource = std::move(resource).value();
    character->runtime_objects = character->model_resource->model.runtime_objects;
    character->object_poses.assign(
        character->runtime_objects.size(), App::Character::BodyAnimationObjectPose{});
    character->posed_groups = character->model_resource->groups;
    character->body_animation = App::Character::BodyAnimationPlayback{};
    character->dialog_performance.reset();
    character->pose_owner = App::Character::PoseOwner::k_model_defaults;
    App::Character::PhysicalMotionService::synchronize(*character);
    character->pose_revision += 1U;
  }
}

App::Omikron::Model3DOData make_collision_decor(const float wall_x, const float floor_y) {
  App::Omikron::Model3DOData decor;
  decor.meshes.push_back(App::Omikron::MeshDescriptor{.vertex_count = 3});
  decor.polygons.push_back(App::Omikron::MeshPolygons{
      .triangles = {App::Omikron::Triangle{
          .vertices = {{{.index = 0}, {.index = 1}, {.index = 2}}}, .face_normal = {.y = -1.0F}}}});
  decor.vertices = {{.position = {.x = -1000.0F, .y = floor_y, .z = -1000.0F}},
      {.position = {.x = 1000.0F, .y = floor_y, .z = -1000.0F}},
      {.position = {.x = 0.0F, .y = floor_y, .z = 1000.0F}}};
  decor.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{});

  decor.meshes.push_back(App::Omikron::MeshDescriptor{.vertex_count = 4, .vertex_base = 3});
  decor.polygons.push_back(
      App::Omikron::MeshPolygons{.rectangles = {App::Omikron::Rectangle{
                                     .vertices = {0, 1, 2, 3}, .face_normal = {.x = -1.0F}}}});
  decor.vertices.insert(decor.vertices.end(),
      {{.position = {.x = wall_x, .y = 100.0F, .z = -100.0F}},
          {.position = {.x = wall_x, .y = 100.0F, .z = 100.0F}},
          {.position = {.x = wall_x, .y = 200.0F, .z = 100.0F}},
          {.position = {.x = wall_x, .y = 200.0F, .z = -100.0F}}});
  decor.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{});
  return decor;
}

void add_collision_ceiling(App::Omikron::Model3DOData& decor, const float ceiling_y) {
  const std::uint32_t vertex_base{static_cast<std::uint32_t>(decor.vertices.size())};
  decor.meshes.push_back(
      App::Omikron::MeshDescriptor{.vertex_count = 4, .vertex_base = vertex_base});
  decor.polygons.push_back(
      App::Omikron::MeshPolygons{.rectangles = {App::Omikron::Rectangle{
                                     .vertices = {0, 1, 2, 3}, .face_normal = {.y = 1.0F}}}});
  decor.vertices.insert(decor.vertices.end(),
      {{.position = {.x = -1000.0F, .y = ceiling_y, .z = -1000.0F}},
          {.position = {.x = -1000.0F, .y = ceiling_y, .z = 1000.0F}},
          {.position = {.x = 1000.0F, .y = ceiling_y, .z = 1000.0F}},
          {.position = {.x = 1000.0F, .y = ceiling_y, .z = -1000.0F}}});
  decor.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{});
}

App::Omikron::Model3DOData make_class2_attachment_decor() {
  App::Omikron::Model3DOData decor;
  decor.meshes.push_back(
      App::Omikron::MeshDescriptor{.flags = 0x00080000U, .vertex_count = 4, .vertex_base = 0});
  decor.polygons.push_back(
      App::Omikron::MeshPolygons{.rectangles = {App::Omikron::Rectangle{
                                     .vertices = {0, 1, 2, 3}, .face_normal = {.y = -1.0F}}}});
  decor.vertices = {{.position = {.x = 80.0F, .y = 60.0F, .z = 32.0F}},
      {.position = {.x = 104.0F, .y = 60.0F, .z = 32.0F}},
      {.position = {.x = 104.0F, .y = 60.0F, .z = 56.0F}},
      {.position = {.x = 80.0F, .y = 60.0F, .z = 56.0F}}};
  decor.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{});

  const std::uint32_t vertex_base{static_cast<std::uint32_t>(decor.vertices.size())};
  decor.meshes.push_back(
      App::Omikron::MeshDescriptor{.vertex_count = 3, .vertex_base = vertex_base});
  decor.polygons.push_back(App::Omikron::MeshPolygons{
      .triangles = {App::Omikron::Triangle{
          .vertices = {{{.index = 0}, {.index = 1}, {.index = 2}}}, .face_normal = {.y = -1.0F}}}});
  decor.vertices.insert(decor.vertices.end(),
      {{.position = {.x = -1000.0F, .y = 100.0F, .z = -1000.0F}},
          {.position = {.x = 1000.0F, .y = 100.0F, .z = -1000.0F}},
          {.position = {.x = 0.0F, .y = 100.0F, .z = 1000.0F}}});
  decor.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{});
  return decor;
}

/// Synthetic AREA contact fixture. The top-level context selects a current
/// body and enables a zone; the record-relative zone event selects a move,
/// suppresses player control, waits on a camera, restores player control,
/// self-disables, and ends.
std::vector<std::byte> make_zone_contact_area_archive(const bool starts_dialog = false,
    const bool self_disables = true,
    const bool enable_controller = true,
    const std::int16_t zone_id = 3795,
    const bool release_control_before_wait = false,
    const bool place_before_activation = false,
    const std::uint32_t initial_xz = 50U,
    const std::int16_t orientation_center_units = 4090,
    const std::int16_t orientation_span_units = 0,
    const bool launch_fire_and_forget = false,
    const bool launch_from_zone_event = false,
    const std::size_t zone_count = 1U,
    const std::optional<std::array<std::int16_t, 2>> crossing_scripts = std::nullopt) {
  Buffer top_level;
  top_level.u8(0x38).u16(136);
  if (launch_fire_and_forget) {
    top_level.u8(0x5A).u16(221).u16(0);
  }
  if (enable_controller) {
    top_level.u8(0x69);
  }
  if (place_before_activation) {
    top_level.u8(0x49).u16(44);
    top_level.u8(0x3F).u16(100);
    top_level.u8(0x69);
  }
  for (std::size_t index{0}; index < zone_count; ++index) {
    top_level.u8(0x40).u16(static_cast<std::uint16_t>(zone_id + index));
  }
  top_level.u8(0x03);

  Buffer zone_event;
  if (launch_from_zone_event) {
    zone_event.u8(0x5A).u16(221).u16(0);
  }
  if (crossing_scripts.has_value()) {
    // Rich zone transition to AREA 222 with pre/post-crossing scripts from GRID.SCX.
    zone_event.u8(0x2F)
        .u16(222)
        .u16(static_cast<std::uint16_t>(crossing_scripts->at(0)))
        .u16(static_cast<std::uint16_t>(crossing_scripts->at(1)));
  } else if (starts_dialog) {
    zone_event.u8(0x3D).u16(272);
  } else {
    zone_event.u8(0x3F).u16(100);
    if (self_disables) {
      zone_event.u8(0x68);
      if (release_control_before_wait) {
        zone_event.u8(0x69);
      }
    }
    zone_event.u8(0x60).u16(42).u16(1).u16(3);
    if (self_disables) {
      if (!release_control_before_wait) {
        zone_event.u8(0x69);
      }
      zone_event.u8(0x41).u16(static_cast<std::uint16_t>(zone_id));
    }
  }
  zone_event.u8(0x03);
  Buffer departure_event;
  departure_event.u8(0x03);

  constexpr std::size_t k_record_offset{0x800};
  constexpr std::size_t k_table0_offset{0x0B4};
  constexpr std::size_t k_table2_offset{k_table0_offset + 0x14U};
  const std::size_t k_table4_offset{k_table2_offset + (zone_count * 0x44U)};
  const std::size_t k_table5_offset{k_table4_offset + 0x114U};
  const std::size_t k_script_offset{k_table5_offset + (place_before_activation ? 0x10U : 0U)};
  const std::size_t zone_event_offset{k_script_offset + top_level.data().size()};
  const std::size_t departure_event_offset{zone_event_offset + zone_event.data().size()};
  const std::size_t record_size{departure_event_offset + departure_event.data().size()};
  constexpr std::size_t k_destination_offset{0x1000U};
  constexpr std::size_t k_destination_size{0xB4U};
  std::vector<std::byte> data(crossing_scripts.has_value()
                                  ? k_destination_offset + k_destination_size
                                  : k_record_offset + record_size,
      std::byte{});
  if (crossing_scripts.has_value()) {
    write_u32(data, 222U * 8U, static_cast<std::uint32_t>(k_destination_offset));
    write_u32(data, (222U * 8U) + 4U, static_cast<std::uint32_t>(k_destination_size));
    write_area_bytecode_pool_bounds(
        data, k_destination_offset, k_destination_size, k_destination_size);
    write_name(data, k_destination_offset + 0x61U, "DEST");
  }
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
  write_u16(data, k_record_offset + 0x48U + (2U * 2U), static_cast<std::uint16_t>(zone_count));
  // Four X/Y/Z vertices; the Y slab contains the zero-radius synthetic actor.
  constexpr std::array<std::array<std::uint32_t, 3>, 4> k_vertices = {
      {{0U, 900U, 0U}, {100U, 1100U, 0U}, {100U, 1100U, 100U}, {0U, 900U, 100U}}};
  for (std::size_t zone_index{0}; zone_index < zone_count; ++zone_index) {
    const std::size_t zone_offset{k_table2_offset + (zone_index * 0x44U)};
    write_u32(data, k_record_offset + zone_offset, static_cast<std::uint32_t>(zone_event_offset));
    write_u32(data,
        k_record_offset + zone_offset + 0x08U,
        static_cast<std::uint32_t>(departure_event_offset));
    for (std::size_t index{0}; index < k_vertices.size(); ++index) {
      for (std::size_t coordinate{0}; coordinate < k_vertices.at(index).size(); ++coordinate) {
        write_u32(data,
            k_record_offset + zone_offset + 0x0CU + ((index * 3U + coordinate) * 4U),
            k_vertices.at(index).at(coordinate));
      }
    }
    write_u16(data,
        k_record_offset + zone_offset + 0x3CU,
        static_cast<std::uint16_t>(orientation_center_units));
    write_u16(data,
        k_record_offset + zone_offset + 0x3EU,
        static_cast<std::uint16_t>(orientation_span_units));
    write_u16(data,
        k_record_offset + zone_offset + 0x40U,
        static_cast<std::uint16_t>(zone_id + zone_index));
    write_u16(data, k_record_offset + zone_offset + 0x42U, 0xFFFF);
  }

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

std::vector<std::byte> make_handoff_area_archive(const std::int16_t release_area_id = 118,
    const bool target_requests_area_142 = false,
    const bool target_selects_character = false) {
  Buffer handoff;
  handoff.u8(0x38).u16(136);
  handoff.u8(0x4F).u16(0xFFFF);
  handoff.u8(0x41).u16(6);
  handoff.u8(0x40).u16(5);
  handoff.u8(0x2F).u16(222).u16(0xFFFF).u16(0xFFFF);
  if (!target_selects_character) {
    handoff.u8(0x47).u16(222).u16(55);
    handoff.u8(0x49).u16(654);
    handoff.u8(0x30).u16(static_cast<std::uint16_t>(release_area_id));
  }
  handoff.u8(0x03);

  constexpr std::size_t k_source_offset{0x800};
  constexpr std::size_t k_target_offset{0xC00};
  constexpr std::size_t k_header_size{0xB4};
  constexpr std::size_t k_source_placement_offset{k_header_size};
  constexpr std::size_t k_source_definition_offset{k_source_placement_offset + 0x14U};
  constexpr std::size_t k_source_zone_offset{k_source_definition_offset + 0x114U};
  constexpr std::size_t k_source_script_offset{k_source_zone_offset + (2U * 0x44U)};
  const std::size_t source_camera_offset{k_source_script_offset + handoff.data().size()};
  const std::size_t source_record_size{source_camera_offset + 0x2CU};
  const std::size_t target_placement_offset{k_header_size};
  const std::size_t target_definition_offset{
      target_placement_offset + (target_selects_character ? 0x14U : 0U)};
  const std::size_t k_target_zone_offset{
      target_definition_offset + (target_selects_character ? 0x114U : 0U)};
  const std::size_t k_target_address_offset{k_target_zone_offset + (2U * 0x44U)};
  const std::size_t k_target_script_offset{k_target_address_offset + 0x10U};
  Buffer target_script;
  if (target_selects_character) {
    target_script.u8(0x38).u16(310).u8(0x03);
  } else if (target_requests_area_142) {
    target_script.u8(0x2F).u16(142).u16(0xFFFF).u16(0xFFFF).u8(0x03);
  }
  const std::size_t target_record_size{k_target_script_offset + target_script.data().size()};
  constexpr std::size_t k_third_offset{0x1000U};
  constexpr std::size_t k_third_record_size{k_header_size};
  const std::size_t archive_size{target_requests_area_142 ? k_third_offset + k_third_record_size
                                                          : k_target_offset + target_record_size};
  std::vector<std::byte> data(archive_size, std::byte{});
  write_u32(data, 118U * 8U, static_cast<std::uint32_t>(k_source_offset));
  write_u32(data, (118U * 8U) + 4U, static_cast<std::uint32_t>(source_record_size));
  write_u32(data, k_source_offset + 0x04U, k_source_script_offset);
  write_area_bytecode_pool_bounds(
      data, k_source_offset, k_source_script_offset, source_camera_offset);
  write_name(data, k_source_offset + 0x61U, "GRID");
  write_u32(data, k_source_offset + 0x28U, k_source_placement_offset);
  write_u16(data, k_source_offset + 0x48U, 1);
  write_u16(data, k_source_offset + k_source_placement_offset + 0x00U, 0xFFFF);
  write_u16(data, k_source_offset + k_source_placement_offset + 0x02U, 136);
  write_u32(data, k_source_offset + k_source_placement_offset + 0x04U, 100U);
  write_u32(data, k_source_offset + k_source_placement_offset + 0x08U, 200U);
  write_u32(data, k_source_offset + k_source_placement_offset + 0x0CU, 300U);
  write_u16(data, k_source_offset + k_source_placement_offset + 0x10U, 0);
  write_u16(data, k_source_offset + k_source_placement_offset + 0x12U, 136);
  write_u32(data, k_source_offset + 0x28U + (4U * 4U), k_source_definition_offset);
  write_u16(data, k_source_offset + 0x48U + (4U * 2U), 1);
  write_u32(data, k_source_offset + 0x28U + (2U * 4U), k_source_zone_offset);
  write_u16(data, k_source_offset + 0x48U + (2U * 2U), 2);
  write_u16(data, k_source_offset + 0x48U + (6U * 2U), 1);
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
  write_camera_record(data, k_source_offset + source_camera_offset, 0, 0);
  std::memcpy(data.data() + k_source_offset + k_source_script_offset,
      handoff.data().data(),
      handoff.data().size());

  write_u32(data, 222U * 8U, static_cast<std::uint32_t>(k_target_offset));
  write_u32(data, (222U * 8U) + 4U, static_cast<std::uint32_t>(target_record_size));
  if (target_requests_area_142 || target_selects_character) {
    write_u32(data, k_target_offset + 0x04U, k_target_script_offset);
  }
  write_area_bytecode_pool_bounds(data,
      k_target_offset,
      target_requests_area_142 || target_selects_character ? k_target_script_offset
                                                           : target_record_size,
      target_record_size);
  write_name(data, k_target_offset + 0x61U, "DEST");
  if (target_selects_character) {
    write_u32(data, k_target_offset + 0x28U, static_cast<std::uint32_t>(target_placement_offset));
    write_u16(data, k_target_offset + 0x48U, 1);
    write_u16(data, k_target_offset + target_placement_offset + 0x00U, 0xFFFF);
    write_u16(data, k_target_offset + target_placement_offset + 0x02U, 310);
    write_u32(data, k_target_offset + target_placement_offset + 0x04U, 43922U);
    write_u32(data, k_target_offset + target_placement_offset + 0x08U, 2592U);
    write_u32(data, k_target_offset + target_placement_offset + 0x0CU, 19656U);
    write_u16(data, k_target_offset + target_placement_offset + 0x12U, 310);
    write_u32(data,
        k_target_offset + 0x28U + (4U * 4U),
        static_cast<std::uint32_t>(target_definition_offset));
    write_u16(data, k_target_offset + 0x48U + (4U * 2U), 1);
    write_name(data, k_target_offset + target_definition_offset + 0x08U, "TARGET CHARACTER");
    write_name(data, k_target_offset + target_definition_offset + 0x90U, "TARGET");
    write_u16(data, k_target_offset + target_definition_offset + 0x110U, 310);
  }
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
  if (target_requests_area_142 || target_selects_character) {
    std::memcpy(data.data() + k_target_offset + k_target_script_offset,
        target_script.data().data(),
        target_script.data().size());
  }
  if (target_requests_area_142) {
    write_u32(data, 142U * 8U, k_third_offset);
    write_u32(data, (142U * 8U) + 4U, k_third_record_size);
    write_area_bytecode_pool_bounds(data, k_third_offset, k_third_record_size, k_third_record_size);
    write_name(data, k_third_offset + 0x61U, "THIRD");
  }
  return data;
}

std::vector<std::byte> make_handoff_scene_archive(const bool detach_own_area_scene = false) {
  Buffer script;
  if (detach_own_area_scene) {
    script.u8(0x48).u16(222);  // The SCENE's own event detaches the SCENE it runs from.
  }
  script.u8(0x03);

  constexpr std::size_t k_record_offset{0x800};
  constexpr std::size_t k_script_offset{0x44};
  const std::size_t table6_offset{k_script_offset + script.data().size()};
  std::vector<std::byte> data(k_record_offset + table6_offset, std::byte{});
  write_u32(data, 55U * 8U, static_cast<std::uint32_t>(k_record_offset));
  write_u32(data, (55U * 8U) + 4U, static_cast<std::uint32_t>(table6_offset));
  write_u32(data, k_record_offset + 0x04U, static_cast<std::uint32_t>(k_script_offset));
  write_u32(data, k_record_offset + 0x08U + (6U * 4U), static_cast<std::uint32_t>(table6_offset));
  write_u32(data, k_record_offset + 0x08U + (7U * 4U), static_cast<std::uint32_t>(k_script_offset));
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
  const std::size_t source_size{source_camera_offset + (fixture.slot_0_area ? k_camera_size : 0U)};
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
  descriptor.u32(2).f32(0.0F).f32(0.0F);     // Wait duration and elapsed time.
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

/// Two SCX scripts with one zero-duration wait each, completing on their first structured tick.
std::vector<std::byte> make_crossing_scripts_scx(
    const std::uint16_t pre_script_id, const std::uint16_t post_script_id) {
  Buffer descriptor;
  descriptor.u32(K_SCRIPTS_TAG).u32(2);
  append_scx_script_record(descriptor, "PreCrossing", pre_script_id, 1);
  append_scx_script_record(descriptor, "PostCrossing", post_script_id, 1);
  descriptor.u32(4).f32(0.0F).f32(0.0F).f32(0.0F).f32(0.0F);  // Wait duration/elapsed pairs.
  for (const std::uint32_t first_value : {0U, 2U}) {
    descriptor.u8(0);  // No related script.
    descriptor.u32(0x06000017U).u32(2).u32(first_value).u32(0xFFFFFFFFU).u32(1).u32(0);
    descriptor.u32(0).u32(0);  // Empty binding tables A and B.
  }
  descriptor.u32(K_END_TAG);

  Buffer bytes;
  bytes.u32(K_SCX_MAGIC).u32(5).u32(8).u32(static_cast<std::uint32_t>(descriptor.data().size()));
  for (const std::byte value : descriptor.data()) {
    bytes.u8(std::to_integer<std::uint8_t>(value));
  }
  return bytes.data();
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

std::vector<std::byte> make_minimal_character_3do() {
  constexpr std::uint32_t k_root_offset{0x2CU};
  constexpr std::uint32_t k_material_offset{0x174U};
  constexpr std::uint32_t k_mesh_offset{k_material_offset + 80U};
  constexpr std::uint32_t k_lights_offset{k_mesh_offset + 140U};
  Buffer bytes;
  bytes.chars("OD3X", 4)
      .u32(4)
      .u32(k_root_offset)
      .u32(k_material_offset)
      .u32(k_mesh_offset)
      .u32(k_mesh_offset)
      .u32(k_mesh_offset)
      .u32(k_mesh_offset)
      .u32(0)
      .u32(0)
      .u32(k_lights_offset)
      .zeros(72)
      .u32(0)
      .zeros(104)
      .u32(1)
      .f32(1.0F)
      .u32(0)
      .u32(0)
      .u32(0)
      .u64(0)
      .u32(1)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(1)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .zeros(80)
      .chars("MATERIAL", 20)
      .chars("", 20)
      .chars("", 20)
      .u32(1)
      .u64(0)
      .u32(0)
      .u16(1)
      .u16(1)
      .u32(0)
      .u32(0)
      .u32(1)
      .u32(0)
      .chars("UBassin", 20)
      .f32(0.0F)
      .f32(0.0F)
      .f32(0.0F)
      .i32(-1)
      .i32(-1)
      .i32(-1)
      .u32(0)
      .u32(0)
      .u32(0)
      .u32(0)
      .zeros(64);
  return bytes.data();
}

/// Scratch directory wiped on construction and destruction.
class TempDirectory {
 public:
  TempDirectory() : m_root{std::filesystem::temp_directory_path() / "opennomad-startup-test"} {
    std::filesystem::remove_all(m_root);
    std::filesystem::create_directories(m_root);
    for (const std::string_view model : {"HO1_FNM", "CURRENT_BO"}) {
      write_bytes(m_root / "MESHES" / "PERSOS" / (std::string{model} + ".3DO"),
          make_minimal_character_3do());
      write_bytes(m_root / "MESHES" / "PERSOS" / (std::string{model} + ".3DT"),
          {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}});
    }
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

/// Options for a session started with loaders injected before the first AREA preload.
struct SessionOptions {
  App::Character::Runtime::ModelLoader model_loader;
  bool stub_ctl_bank{false};
};

struct StubModelOptions {
  float bounds_radius{0.0F};
  std::optional<App::Omikron::CollisionSphere> collision_sphere;
  /// Used instead of `collision_sphere` when a model needs several authored spheres.
  std::vector<App::Omikron::CollisionSphere> collision_spheres;
  /// Builds the ActorRoot/DetailedChild hierarchy used by the representative-object specs.
  bool actor_meshes{false};
  /// Incremented on every loader invocation (the Runtime caches by model name).
  std::size_t* load_counter{nullptr};
};

App::Character::Runtime::ModelLoader make_stub_model_loader(const StubModelOptions options = {}) {
  return [options](const std::string_view name)
             -> std::expected<std::shared_ptr<const App::Character::ModelResource>, std::string> {
    if (options.load_counter != nullptr) {
      ++*options.load_counter;
    }
    auto resource{std::make_shared<App::Character::ModelResource>()};
    resource->name = name;
    resource->bounds_radius = options.bounds_radius;
    resource->groups.push_back(App::Omikron::MaterialGroup{});
    if (options.collision_sphere.has_value()) {
      resource->model.header.collision_sphere_count = 1U;
      resource->model.header.collision_sphere_slots.at(0) = options.collision_sphere.value();
    }
    if (!options.collision_spheres.empty()) {
      resource->model.header.collision_sphere_count =
          static_cast<std::uint32_t>(options.collision_spheres.size());
      for (std::size_t index{0}; index < options.collision_spheres.size(); ++index) {
        resource->model.header.collision_sphere_slots.at(index) =
            options.collision_spheres.at(index);
      }
    }
    if (options.actor_meshes) {
      resource->model.root_mesh_index = 0;
      resource->model.meshes.resize(2U);
      resource->model.meshes.at(0).name = "ActorRoot";
      resource->model.meshes.at(0).bounding_radius = 42.5F;
      resource->model.meshes.at(0).triangle_count = 26U;
      resource->model.meshes.at(1).name = "DetailedChild";
      resource->model.meshes.at(1).bounding_radius = 5.8F;
      resource->model.meshes.at(1).triangle_count = 116U;
      resource->model.hierarchy_parent_index = {-1, 0};
      resource->model.hierarchy_first_child_index = {1, -1};
      resource->model.hierarchy_next_sibling_index = {-1, -1};
      resource->actor_object_index = App::Character::actor_object_index(resource->model);
    }
    return std::shared_ptr<const App::Character::ModelResource>{std::move(resource)};
  };
}

/// A fully initialized new session whose world runtimes receive the requested loaders before the
/// AREA loader preloads any body (see ScenarioManager::set_world_runtime_configurator).
struct StartedSession {
  ScopedGameDataRoot root;
  App::ScenarioManager manager;
  App::ScenarioStartupController controller;

  explicit StartedSession(const TempDirectory& temp, SessionOptions options = {})
      : root{temp.root()} {
    manager.set_world_runtime_configurator([options](App::ScenarioRuntime& runtime) {
      if (options.model_loader) {
        runtime.character_runtime().set_model_loader(options.model_loader);
      }
      if (options.stub_ctl_bank) {
        install_stub_ctl_loader(runtime);
      }
    });
    const auto initialized{controller.initialize(manager)};
    const std::string failure{initialized.has_value() ? std::string{} : initialized.error()};
    REQUIRE_MESSAGE(initialized.has_value(), failure);
  }

  StartedSession(const StartedSession&) = delete;
  StartedSession(StartedSession&&) = delete;
  StartedSession& operator=(const StartedSession&) = delete;
  StartedSession& operator=(StartedSession&&) = delete;
  ~StartedSession() = default;

  [[nodiscard]] App::ScenarioRuntime& world(const std::uint32_t scene_id = 0U) {
    App::ScenarioRuntime* const runtime{manager.world_runtime(scene_id)};
    REQUIRE(runtime != nullptr);
    return *runtime;
  }
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
    const bool release_control_before_wait = false,
    const bool place_before_activation = false,
    const std::uint32_t initial_xz = 50U,
    const std::int16_t orientation_center_units = 4090,
    const std::int16_t orientation_span_units = 0,
    const bool launch_fire_and_forget = false,
    const bool launch_from_zone_event = false,
    const bool self_disables = true,
    const std::size_t zone_count = 1U) {
  write_bytes(temp.root() / "IAM" / "START", make_start_with_active_character_bits({136}));
  write_bytes(temp.root() / "IAM" / "GLOBAL", make_camera_namespace_global(true));
  write_bytes(temp.root() / "IAM" / "AREA",
      make_zone_contact_area_archive(false,
          self_disables,
          enable_controller,
          zone_id,
          release_control_before_wait,
          place_before_activation,
          initial_xz,
          orientation_center_units,
          orientation_span_units,
          launch_fire_and_forget,
          launch_from_zone_event,
          zone_count));
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX",
      launch_fire_and_forget || launch_from_zone_event ? make_kayl_arrives_scx(221)
                                                       : make_minimal_scx());
}

/// A zone whose event 1 requests the rich transition 0x2F(222, pre, post). The controlled body
/// starts outside the zone, with its controller disabled.
void write_rich_transition_fixtures(const TempDirectory& temp,
    const std::int16_t zone_id,
    const std::uint16_t pre_script_id,
    const std::uint16_t post_script_id) {
  write_bytes(temp.root() / "IAM" / "START", make_start_with_active_character_bits({136}));
  write_bytes(temp.root() / "IAM" / "GLOBAL", make_camera_namespace_global(true));
  write_bytes(temp.root() / "IAM" / "AREA",
      make_zone_contact_area_archive(false,
          false,
          false,
          zone_id,
          false,
          false,
          500U,
          4090,
          0,
          false,
          false,
          1U,
          std::array<std::int16_t, 2>{static_cast<std::int16_t>(pre_script_id),
              static_cast<std::int16_t>(post_script_id)}));
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX",
      make_crossing_scripts_scx(pre_script_id, post_script_id));
  write_bytes(temp.root() / "SCPTDATA" / "DEST.SCX", make_minimal_scx());
}

void write_live_zone_contact_fixtures(const TempDirectory& temp) {
  write_bytes(temp.root() / "IAM" / "START", make_start_with_active_character_bits({136}));
  write_bytes(temp.root() / "IAM" / "GLOBAL", make_camera_namespace_global(true));
  write_bytes(temp.root() / "IAM" / "AREA", make_zone_contact_area_archive(false, false));
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
}

void write_zone_dialog_fixtures(const TempDirectory& temp) {
  write_bytes(temp.root() / "IAM" / "START", make_start_with_active_character_bits({136}));
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

/// Fixture for structured-child completion testing: Sets current character first, then
/// launches a tracked character script.
void write_structured_child_completion_fixtures(const TempDirectory& temp) {
  Buffer script;
  script.u8(0x38).u16(136);                  // SetCurrentCharacter 136
  script.u8(0x2E).u16(221).u16(0).u8(0x03);  // StartCurrentCharacterScriptTracked, wait, EndEvent
  std::vector<std::byte> area{make_area_archive()};
  std::memcpy(area.data() + 0x800U + 0x3FCU, script.data().data(), script.data().size());

  write_bytes(temp.root() / "IAM" / "START", make_start_with_active_character_bits({469}));
  write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
  write_bytes(temp.root() / "IAM" / "AREA", area);
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_kayl_arrives_scx(221));
  write_bytes(temp.root() / "MESHES" / "DECORS" / "GRID.3DO", make_minimal_3do());
}

void write_chained_structured_child_fixtures(const TempDirectory& temp) {
  Buffer script;
  script.u8(0x38).u16(136);
  script.u8(0x2E).u16(221).u16(0);
  script.u8(0x2E).u16(221).u16(0).u8(0x03);
  std::vector<std::byte> area{make_area_archive()};
  std::memcpy(area.data() + 0x800U + 0x3FCU, script.data().data(), script.data().size());

  write_bytes(temp.root() / "IAM" / "START", make_start_with_active_character_bits({469}));
  write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
  write_bytes(temp.root() / "IAM" / "AREA", area);
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_kayl_arrives_scx(221));
  write_bytes(temp.root() / "MESHES" / "DECORS" / "GRID.3DO", make_minimal_3do());
}

void write_possession_cycle_fixtures(const TempDirectory& temp) {
  Buffer script;
  script.u8(0x38).u16(310);
  script.u8(0x2E).u16(221).u16(0);
  script.u8(0x38).u16(136);
  script.u8(0x2E).u16(221).u16(0);
  script.u8(0x38).u16(310);
  script.u8(0x38).u16(310).u8(0x03);
  std::vector<std::byte> area{make_area_archive()};
  std::memcpy(area.data() + 0x800U + 0x3FCU, script.data().data(), script.data().size());

  write_bytes(temp.root() / "IAM" / "START", make_start_with_active_character_bits({468, 469}));
  write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
  write_bytes(temp.root() / "IAM" / "AREA", area);
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_kayl_arrives_scx(221));
  write_bytes(temp.root() / "MESHES" / "DECORS" / "GRID.3DO", make_minimal_3do());
}

void write_activation_alias_fixtures(const TempDirectory& temp) {
  Buffer script;
  script.u8(0x38).u16(310);
  script.u8(0x2E).u16(221).u16(0);
  script.u8(0x38).u16(136);
  script.u8(0x2E).u16(221).u16(0);
  script.u8(0x4F).u16(310);
  script.u8(0x2E).u16(221).u16(0);
  script.u8(0x4E).u16(310).u16(0).u8(0x03);
  std::vector<std::byte> area{make_area_archive()};
  std::memcpy(area.data() + 0x800U + 0x3FCU, script.data().data(), script.data().size());

  write_bytes(temp.root() / "IAM" / "START", make_start());
  write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
  write_bytes(temp.root() / "IAM" / "AREA", area);
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_kayl_arrives_scx(221));
  write_bytes(temp.root() / "SCPTDATA" / "B.SCX", make_minimal_scx());
  write_bytes(temp.root() / "MESHES" / "DECORS" / "GRID.3DO", make_minimal_3do());
}

void write_explicitly_unbound_activation_fixtures(const TempDirectory& temp) {
  Buffer script;
  script.u8(0x38).u16(310);
  script.u8(0x4E).u16(310).u16(0).u8(0x03);
  std::vector<std::byte> area{make_area_archive()};
  std::memcpy(area.data() + 0x800U + 0x3FCU, script.data().data(), script.data().size());
  write_bytes(temp.root() / "IAM" / "START", make_start());
  write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
  write_bytes(temp.root() / "IAM" / "AREA", area);
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
  write_bytes(temp.root() / "MESHES" / "DECORS" / "GRID.3DO", make_minimal_3do());
}

void write_inactive_character_script_fixtures(const TempDirectory& temp, const bool tracked) {
  Buffer script;
  script.u8(tracked ? 0x3CU : 0x3BU).u16(310).u16(221).u16(0).u8(0x03);
  std::vector<std::byte> area{make_area_archive()};
  std::memcpy(area.data() + 0x800U + 0x3FCU, script.data().data(), script.data().size());
  write_bytes(temp.root() / "IAM" / "START", make_start());
  write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
  write_bytes(temp.root() / "IAM" / "AREA", area);
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_kayl_arrives_scx(221));
  write_bytes(temp.root() / "MESHES" / "DECORS" / "GRID.3DO", make_minimal_3do());
}

void write_character_activation_fixtures(
    const TempDirectory& temp, const bool apply_area_transform) {
  Buffer script;
  script.u8(0x4E).u16(310).u16(apply_area_transform ? 1U : 0U).u8(0x03);
  std::vector<std::byte> area{make_area_archive()};
  std::memcpy(area.data() + 0x800U + 0x3FCU, script.data().data(), script.data().size());
  write_bytes(temp.root() / "IAM" / "START", make_start());
  write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
  write_bytes(temp.root() / "IAM" / "AREA", area);
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
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

void write_handoff_fixtures(const TempDirectory& temp,
    const std::int16_t release_area_id = 118,
    const bool target_requests_area_142 = false,
    const bool target_selects_character = false,
    const bool scene_detaches_itself = false) {
  std::vector<std::byte> start{target_selects_character
                                    ? make_start_with_active_character_bits({136, 310})
                                    : make_start_with_active_character_bits({136})};
  start.at(0x13FCU) = std::byte{0x40};  // ZONE 6 starts persistently enabled.
  write_bytes(temp.root() / "IAM" / "START", start);
  write_bytes(temp.root() / "IAM" / "GLOBAL", App::Tests::make_empty_iam_global());
  write_bytes(temp.root() / "IAM" / "AREA",
      make_handoff_area_archive(
          release_area_id, target_requests_area_142, target_selects_character));
  write_bytes(temp.root() / "IAM" / "SCENE", make_handoff_scene_archive(scene_detaches_itself));
  write_bytes(temp.root() / "SCPTDATA" / "aventure.scx", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "GRID.SCX", make_minimal_scx());
  write_bytes(temp.root() / "SCPTDATA" / "DEST.SCX", make_minimal_scx());
  if (target_requests_area_142) {
    write_bytes(temp.root() / "SCPTDATA" / "THIRD.SCX", make_minimal_scx());
  }
  if (target_selects_character) {
    write_bytes(temp.root() / "MESHES" / "PERSOS" / "TARGET.3DO", make_minimal_character_3do());
    write_bytes(temp.root() / "MESHES" / "PERSOS" / "TARGET.3DT",
        {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}});
  }
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

#pragma GCC diagnostic pop

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

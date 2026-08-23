#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/Omikron/IamArchive.hpp"
#include "Core/Omikron/IamDialog.hpp"

namespace {

using App::Dialog::DialogRuntime;
using App::Dialog::DialogState;
using App::Omikron::IamDialogNode;
using App::Omikron::IamDialogRecord;

struct NodeFixture {
  std::string main_line{"Main line"};
  std::array<std::string, 4> responses{"Response", "", "", ""};
  std::string automatic_line;
  std::array<std::int16_t, 4> targets{-1, -1, -1, -1};
  std::array<bool, 4> conditions{};
  std::array<bool, 4> actions{};
  std::string face_motion{"FACE"};
  std::array<std::int16_t, 2> response_cameras{-1, -1};
  std::array<std::int16_t, 2> line_cameras{-1, -1};
};

void write_i16(std::vector<std::byte>& data, const std::size_t offset, const std::int16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u16(std::vector<std::byte>& data, const std::size_t offset, const std::uint16_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_i32(std::vector<std::byte>& data, const std::size_t offset, const std::int32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

void write_u32(std::vector<std::byte>& data, const std::size_t offset, const std::uint32_t value) {
  std::memcpy(data.data() + offset, &value, sizeof(value));
}

std::uint32_t append_string(std::vector<std::byte>& data, const std::string_view text) {
  const auto offset{static_cast<std::uint32_t>(data.size())};
  for (const char character : text) {
    data.push_back(static_cast<std::byte>(character));
  }
  data.push_back(std::byte{});
  return offset;
}

std::vector<std::byte> make_dialog(
    const std::vector<NodeFixture>& nodes, const bool include_camera = false) {
  const std::size_t camera_count{include_camera ? 1U : 0U};
  const std::size_t fixed_end{IamDialogRecord::k_header_size +
                              (nodes.size() * IamDialogNode::k_serialized_size) +
                              (camera_count * App::Omikron::IamCameraRecord::k_serialized_size)};
  std::vector<std::byte> data(fixed_end, std::byte{});
  write_i16(data, 0x00U, 310);
  write_i16(data, 0x02U, static_cast<std::int16_t>(nodes.size()));
  write_i16(data, 0x04U, static_cast<std::int16_t>(camera_count));
  write_i16(data, 0x06U, static_cast<std::int16_t>(camera_count));

  if (include_camera) {
    const std::size_t camera_offset{
        IamDialogRecord::k_header_size + (nodes.size() * IamDialogNode::k_serialized_size)};
    write_i32(data, camera_offset + 0x00U, -3206);
    write_i32(data, camera_offset + 0x0CU, -3165);
    write_i16(data, camera_offset + 0x18U, 2159);
    write_u16(data, camera_offset + 0x1AU, 12);
    write_i16(data, camera_offset + 0x1EU, 853);
  }

  for (std::size_t index{0}; index < nodes.size(); ++index) {
    const NodeFixture& fixture{nodes.at(index)};
    const std::size_t node_offset{
        IamDialogRecord::k_header_size + (index * IamDialogNode::k_serialized_size)};
    const std::uint32_t strings_offset{static_cast<std::uint32_t>(data.size())};
    append_string(data, fixture.main_line);
    for (const std::string& response : fixture.responses) {
      append_string(data, response);
    }
    append_string(data, fixture.automatic_line);
    write_u32(data, node_offset + 0x20U, strings_offset);
    for (std::size_t slot{0}; slot < 4U; ++slot) {
      write_i16(data, node_offset + 0x24U + (slot * 2U), fixture.targets.at(slot));
      if (fixture.conditions.at(slot)) {
        write_u32(data, node_offset + (slot * 4U), static_cast<std::uint32_t>(data.size()));
        data.push_back(std::byte{0x03});
      }
      if (fixture.actions.at(slot)) {
        write_u32(data, node_offset + 0x10U + (slot * 4U), static_cast<std::uint32_t>(data.size()));
        data.push_back(std::byte{0x03});
      }
    }
    write_i16(data, node_offset + 0x2CU, static_cast<std::int16_t>(index));
    std::memcpy(data.data() + node_offset + 0x2EU,
        fixture.face_motion.data(),
        std::min<std::size_t>(fixture.face_motion.size(), 10U));
    write_i16(data, node_offset + 0x38U, fixture.response_cameras.at(0));
    write_i16(data, node_offset + 0x3AU, fixture.response_cameras.at(1));
    write_i16(data, node_offset + 0x3CU, fixture.line_cameras.at(0));
    write_i16(data, node_offset + 0x3EU, fixture.line_cameras.at(1));
  }
  return data;
}

IamDialogRecord parse(const std::vector<std::byte>& bytes) {
  auto result{IamDialogRecord::load(bytes)};
  REQUIRE(result.has_value());
  return std::move(result).value();
}

}  // namespace

TEST_SUITE("Core::Omikron::IamDialogRecord") {
  TEST_CASE("Rejects zero and truncated records") {
    CHECK_FALSE(IamDialogRecord::load(std::span<const std::byte>{}).has_value());
    const std::array<std::byte, 7> truncated{};
    CHECK_FALSE(IamDialogRecord::load(truncated).has_value());
  }

  TEST_CASE("Parses header, node strings, fixed face motion and shared camera records") {
    NodeFixture node;
    node.automatic_line = "Automatic";
    node.face_motion = "1234567890";
    node.line_cameras = {2159, -1};
    const IamDialogRecord record{parse(make_dialog({node}, true))};
    CHECK_EQ(record.character_id(), 310);
    CHECK_EQ(record.node_count(), 1);
    CHECK_EQ(record.camera_count(), 1);
    const auto parsed_node{record.node_by_id(0)};
    REQUIRE(parsed_node.has_value());
    CHECK_EQ(record.main_line(parsed_node.value()), "Main line");
    CHECK_EQ(record.response_text(parsed_node.value(), 0), "Response");
    CHECK_EQ(record.automatic_player_line(parsed_node.value()), "Automatic");
    CHECK_EQ(parsed_node->face_motion_base, "1234567890");
    const auto camera{record.camera_by_id(2159)};
    REQUIRE(camera.has_value());
    CHECK_EQ(camera->serialized_eye.at(0), -3206);
    CHECK_EQ(camera->camera_type, 12U);
  }

  TEST_CASE("Archive lookup uses the high and low bytes of a uint16 dialog ID") {
    const std::vector<std::byte> record{make_dialog({NodeFixture{}})};
    constexpr std::uint16_t k_dialog_id{0x0110};
    constexpr std::size_t k_record_offset{0x1000};
    std::vector<std::byte> archive(k_record_offset + record.size(), std::byte{});
    constexpr std::size_t k_entry_offset{0x800U + (0x10U * 8U)};
    write_u32(archive, k_entry_offset, k_record_offset);
    write_u32(archive, k_entry_offset + 4U, static_cast<std::uint32_t>(record.size()));
    std::memcpy(archive.data() + k_record_offset, record.data(), record.size());
    const auto parsed{IamDialogRecord::load_from_archive(archive, k_dialog_id)};
    REQUIRE(parsed.has_value());
    CHECK_EQ(parsed->character_id(), 310);
  }

  TEST_CASE("Rejects invalid counts, mirror mismatch and table overflows") {
    std::vector<std::byte> data{make_dialog({NodeFixture{}})};
    write_i16(data, 0x02U, -1);
    CHECK(IamDialogRecord::load(data).error().find("node count") != std::string::npos);
    data = make_dialog({NodeFixture{}});
    write_i16(data, 0x04U, 2);
    write_i16(data, 0x06U, 1);
    CHECK(IamDialogRecord::load(data).error().find("mirror") != std::string::npos);
    data = make_dialog({NodeFixture{}});
    write_i16(data, 0x02U, 100);
    CHECK(IamDialogRecord::load(data).error().find("node table") != std::string::npos);
    data = make_dialog({NodeFixture{}});
    write_i16(data, 0x04U, 100);
    write_i16(data, 0x06U, 100);
    CHECK(IamDialogRecord::load(data).error().find("camera table") != std::string::npos);
  }

  TEST_CASE("Rejects each invalid record-relative node offset") {
    for (std::size_t field{0}; field < 9U; ++field) {
      std::vector<std::byte> data{make_dialog({NodeFixture{}})};
      write_u32(data,
          IamDialogRecord::k_header_size + (field * 4U),
          static_cast<std::uint32_t>(data.size()));
      const auto parsed{IamDialogRecord::load(data)};
      CHECK_FALSE(parsed.has_value());
      CHECK(parsed.error().find("offset") != std::string::npos);
    }
  }

  TEST_CASE("Rejects unterminated strings, invalid targets, node IDs and cameras") {
    std::vector<std::byte> data{make_dialog({NodeFixture{}})};
    data.back() = std::byte{'X'};
    CHECK(IamDialogRecord::load(data).error().find("NUL-terminated") != std::string::npos);
    data = make_dialog({NodeFixture{}});
    write_i16(data, IamDialogRecord::k_header_size + 0x24U, 1);
    CHECK(IamDialogRecord::load(data).error().find("target node") != std::string::npos);
    data = make_dialog({NodeFixture{}});
    write_i16(data, IamDialogRecord::k_header_size + 0x2CU, 2);
    CHECK(IamDialogRecord::load(data).error().find("node ID") != std::string::npos);
    data = make_dialog({NodeFixture{}}, true);
    write_i16(data, IamDialogRecord::k_header_size + 0x3CU, 999);
    CHECK(IamDialogRecord::load(data).error().find("camera ID") != std::string::npos);
  }
}

TEST_SUITE("Core::Dialog::DialogRuntime") {
  TEST_CASE("Presentation exposes face-motion and both authored camera pairs") {
    NodeFixture node;
    node.line_cameras = {2159, -1};
    node.response_cameras = {-1, 2159};
    DialogRuntime runtime;
    REQUIRE(runtime.start(parse(make_dialog({node}, true))).has_value());
    const auto presentation{runtime.presentation()};
    REQUIRE(presentation.has_value());
    CHECK_EQ(presentation->face_motion_resource, "FACE.3dm");
    CHECK_EQ(presentation->line_cameras.authored_ids.at(0), 2159);
    CHECK(presentation->line_cameras.cameras.at(0).has_value());
    CHECK_FALSE(presentation->line_cameras.cameras.at(1).has_value());
    CHECK_EQ(presentation->response_cameras.authored_ids.at(1), 2159);
    CHECK(presentation->response_cameras.cameras.at(1).has_value());
  }

  TEST_CASE("Filters conditions and exposes only visible available responses") {
    NodeFixture node;
    node.responses = {"Always", "False", "True", ""};
    node.conditions = {false, true, true, false};
    std::size_t evaluations{0};
    DialogRuntime runtime{
        [&evaluations](std::span<const std::byte>) -> std::expected<bool, std::string> {
          ++evaluations;
          return evaluations == 2U;
        }};
    REQUIRE(runtime.start(parse(make_dialog({node}))).has_value());
    const auto presentation{runtime.presentation()};
    REQUIRE(presentation.has_value());
    REQUIRE_EQ(presentation->choices.size(), 2U);
    CHECK_EQ(presentation->choices.at(0).slot, 0U);
    CHECK_EQ(presentation->choices.at(1).slot, 2U);
  }

  TEST_CASE("Follows exact targets and executes only the selected visible action") {
    NodeFixture first;
    first.responses = {"Next", "Finish", "", ""};
    first.targets = {1, -1, -1, -1};
    first.actions = {true, true, false, false};
    NodeFixture second;
    second.main_line = "Second";
    second.responses = {"", "", "", ""};
    std::size_t actions{0};
    DialogRuntime runtime{
        {}, [&actions](std::span<const std::byte>) -> std::expected<void, std::string> {
          ++actions;
          return {};
        }};
    REQUIRE(runtime.start(parse(make_dialog({first, second}))).has_value());
    REQUIRE(runtime.acknowledge_line().has_value());
    CHECK_EQ(runtime.state(), DialogState::k_waiting_for_choice);
    REQUIRE(runtime.select_choice(0).has_value());
    CHECK_EQ(actions, 1U);
    REQUIRE(runtime.presentation().has_value());
    CHECK_EQ(runtime.presentation()->main_line, "Second");
    REQUIRE(runtime.acknowledge_line().has_value());
    CHECK(runtime.completed());
  }

  TEST_CASE("Presents automatic player line between the main line and choices") {
    NodeFixture node;
    node.automatic_line = "Automatic";
    DialogRuntime runtime;
    REQUIRE(runtime.start(parse(make_dialog({node}))).has_value());
    CHECK_EQ(runtime.presentation()->displayed_line, "Main line");
    REQUIRE(runtime.acknowledge_line().has_value());
    CHECK_EQ(runtime.state(), DialogState::k_presenting_automatic_player_line);
    CHECK_EQ(runtime.presentation()->displayed_line, "Automatic");
    REQUIRE(runtime.acknowledge_line().has_value());
    CHECK_EQ(runtime.state(), DialogState::k_waiting_for_choice);
  }

  TEST_CASE("Terminal node remains visible until its line is acknowledged") {
    NodeFixture node;
    node.responses = {"", "", "", ""};
    DialogRuntime runtime;
    REQUIRE(runtime.start(parse(make_dialog({node}))).has_value());
    CHECK(runtime.active());
    CHECK_FALSE(runtime.completed());
    REQUIRE(runtime.acknowledge_line().has_value());
    CHECK(runtime.completed());
    CHECK(runtime.take_completion());
    CHECK_EQ(runtime.state(), DialogState::k_inactive);
  }

  TEST_CASE("A selected negative target completes the dialog") {
    DialogRuntime runtime;
    REQUIRE(runtime.start(parse(make_dialog({NodeFixture{}}))).has_value());
    REQUIRE(runtime.acknowledge_line().has_value());
    REQUIRE(runtime.select_choice(0).has_value());
    CHECK(runtime.completed());
  }

  TEST_CASE("An action on an invisible response is never executed on terminal completion") {
    NodeFixture node;
    node.responses = {"", "", "", ""};
    node.actions = {true, false, false, false};
    std::size_t actions{0};
    DialogRuntime runtime{
        {}, [&actions](std::span<const std::byte>) -> std::expected<void, std::string> {
          ++actions;
          return {};
        }};
    REQUIRE(runtime.start(parse(make_dialog({node}))).has_value());
    REQUIRE(runtime.acknowledge_line().has_value());
    CHECK(runtime.completed());
    CHECK_EQ(actions, 0U);
  }

  TEST_CASE("Missing bytecode hooks fail explicitly") {
    NodeFixture conditioned;
    conditioned.conditions.at(0) = true;
    DialogRuntime runtime;
    const auto start{runtime.start(parse(make_dialog({conditioned})))};
    REQUIRE_FALSE(start.has_value());
    CHECK(start.error().find("unsupported") != std::string::npos);

    NodeFixture actioned;
    actioned.actions.at(0) = true;
    DialogRuntime action_runtime;
    REQUIRE(action_runtime.start(parse(make_dialog({actioned}))).has_value());
    REQUIRE(action_runtime.acknowledge_line().has_value());
    const auto selected{action_runtime.select_choice(0)};
    REQUIRE_FALSE(selected.has_value());
    CHECK(selected.error().find("unsupported") != std::string::npos);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
// cppcoreguidelines-pro-bounds-pointer-arithmetic,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,
// readability-suspicious-call-argument)

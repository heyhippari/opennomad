#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>

#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/IamDialog.hpp"

TEST_CASE("Retail IAM/DIALOG record 272 matches the recovered graph") {
  const auto file{App::load_game_file("IAM/DIALOG")};
  REQUIRE(file.has_value());
  auto record{App::Omikron::IamDialogRecord::load_from_archive(file->bytes, 272)};
  REQUIRE(record.has_value());
  CHECK_EQ(record->record_size(), 0x6ABU);
  CHECK_EQ(record->character_id(), 310);
  CHECK_EQ(record->node_count(), 3);
  CHECK_EQ(record->camera_count(), 5);

  const auto node0{record->node_by_id(0)};
  const auto node1{record->node_by_id(1)};
  const auto node2{record->node_by_id(2)};
  REQUIRE(node0.has_value());
  REQUIRE(node1.has_value());
  REQUIRE(node2.has_value());
  const App::Omikron::IamDialogNode parsed_node0{node0.value_or({})};
  const App::Omikron::IamDialogNode parsed_node1{node1.value_or({})};
  const App::Omikron::IamDialogNode parsed_node2{node2.value_or({})};
  CHECK_EQ(record->response_text(parsed_node0, 0), "I accept.");
  CHECK_EQ(parsed_node0.target_node_ids.at(0), 1);
  CHECK_EQ(record->response_text(parsed_node1, 0), "OK. I understand.");
  CHECK_EQ(parsed_node1.target_node_ids.at(0), 2);
  CHECK(record->response_text(parsed_node2, 0).empty());
  CHECK_EQ(parsed_node0.face_motion_base, "125338");
  CHECK_EQ(parsed_node1.face_motion_base, "125339");
  CHECK_EQ(parsed_node2.face_motion_base, "12533A");
  CHECK_EQ(parsed_node0.line_camera_ids, std::array<std::int16_t, 2>{2159, 2165});
  CHECK_EQ(parsed_node1.line_camera_ids, std::array<std::int16_t, 2>{2165, 2160});
  CHECK_EQ(parsed_node1.response_camera_ids, std::array<std::int16_t, 2>{2160, 2167});
  CHECK_EQ(parsed_node2.line_camera_ids, std::array<std::int16_t, 2>{2167, 2166});

  std::size_t actions{0};
  App::Dialog::DialogRuntime runtime{
      {}, [&actions](std::span<const std::byte>) -> std::expected<void, std::string> {
        ++actions;
        return {};
      }};
  REQUIRE(runtime.start(std::move(record).value()).has_value());
  REQUIRE(runtime.acknowledge_line().has_value());
  REQUIRE(runtime.select_choice(0).has_value());
  REQUIRE(runtime.acknowledge_line().has_value());
  REQUIRE(runtime.select_choice(0).has_value());
  const auto presentation{runtime.presentation()};
  REQUIRE(presentation.has_value());
  CHECK_EQ(presentation.value_or({}).node_id, 2);
  REQUIRE(runtime.acknowledge_line().has_value());
  CHECK(runtime.completed());
  CHECK_EQ(actions, 0U);
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

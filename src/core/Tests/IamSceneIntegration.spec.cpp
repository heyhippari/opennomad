#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include <cstdint>

#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/IamArchive.hpp"
#include "Core/Omikron/IamScene.hpp"

TEST_CASE("[RETAIL] all 71 IAM/SCENE records satisfy recovered geometry") {
  const auto file{App::load_game_file("IAM/SCENE")};
  REQUIRE_MESSAGE(file.has_value(), file.error());
  const App::Omikron::IamIndexedArchive archive{file->bytes};

  for (std::uint32_t id{0}; id <= 70U; ++id) {
    CAPTURE(id);
    const auto record_bytes{archive.read_record(id)};
    REQUIRE_MESSAGE(record_bytes.has_value(), record_bytes.error());
    const auto scene{App::Omikron::IamSceneRecord::load(record_bytes.value())};
    REQUIRE_MESSAGE(scene.has_value(), scene.error());
    if (scene->script_offset() != 0U) {
      CHECK_LT(scene->script_offset(), scene->table_offset(6U));
      CHECK_EQ(scene->script_bytes().size(), scene->table_offset(6U) - scene->script_offset());
    }
  }
  CHECK_FALSE(archive.read_record(71U).has_value());
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)
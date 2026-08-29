#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include <cstddef>
#include <cstdint>

#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/IamArchive.hpp"
#include "Core/Omikron/IamScene.hpp"

TEST_CASE("[RETAIL] all 71 IAM/SCENE records satisfy recovered geometry") {
  const auto file{App::load_game_file("IAM/SCENE")};
  REQUIRE_MESSAGE(file.has_value(), file.error());
  const App::Omikron::IamIndexedArchive archive{file->bytes};

  std::size_t primary_zero_count{0};
  std::size_t primary_at_pool_start_count{0};
  std::size_t primary_after_pool_start_count{0};
  std::size_t entries_before_primary_count{0};

  for (std::uint32_t id{0}; id <= 70U; ++id) {
    CAPTURE(id);
    const auto record_bytes{archive.read_record(id)};
    REQUIRE_MESSAGE(record_bytes.has_value(), record_bytes.error());
    const auto scene{App::Omikron::IamSceneRecord::load(record_bytes.value())};
    REQUIRE_MESSAGE(scene.has_value(), scene.error());

    const std::uint32_t pool_start{scene->bytecode_pool_offset()};
    const std::uint32_t pool_end{scene->table_offset(6U)};
    CHECK_EQ(pool_start,
        scene->table_offset(7U) + (static_cast<std::uint32_t>(scene->table_count(7U)) * 0x08U));
    CHECK_EQ(scene->bytecode_pool().size(), pool_end - pool_start);
    CHECK_LE(pool_start, pool_end);

    const auto check_entry = [&](const std::uint32_t entry) {
      if (entry == 0U) {
        return;
      }
      CHECK_GE(entry, pool_start);
      CHECK_LT(entry, pool_end);
      if (scene->primary_event_offset() != 0U && entry < scene->primary_event_offset()) {
        ++entries_before_primary_count;
      }
    };

    const std::uint32_t primary{scene->primary_event_offset()};
    check_entry(primary);
    if (primary == 0U) {
      ++primary_zero_count;
    } else if (primary == pool_start) {
      ++primary_at_pool_start_count;
    } else {
      CHECK_GT(primary, pool_start);
      ++primary_after_pool_start_count;
    }
    for (const App::Omikron::IamSceneZoneRecord& zone : scene->zones()) {
      for (const std::uint32_t entry : zone.event_offsets) {
        check_entry(entry);
      }
    }
    for (const App::Omikron::IamSceneScriptLinkRecord& link : scene->script_links()) {
      check_entry(link.program_offset);
    }
  }
  CHECK_FALSE(archive.read_record(71U).has_value());
  MESSAGE("IAM/SCENE corpus: records=71 primaryZero=",
      primary_zero_count,
      " primaryAtPoolStart=",
      primary_at_pool_start_count,
      " primaryAfterPoolStart=",
      primary_after_pool_start_count,
      " entriesBeforePrimary=",
      entries_before_primary_count);
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)
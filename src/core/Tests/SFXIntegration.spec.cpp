#include <doctest/doctest.h>

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, cert-err33-c)

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Omikron/SFX.hpp"

TEST_CASE("GRID retail SFX matches the recovered package and authored sprite mappings") {
  const auto sfx_file{App::load_game_file("SCPTDATA/GRID.SFX")};
  if (!sfx_file) {
    FAIL(sfx_file.error());
  }
  REQUIRE(sfx_file.has_value());
  const auto sfx{App::Omikron::SFX::load(std::span<const std::byte>{sfx_file->bytes})};
  if (!sfx) {
    FAIL(sfx.error());
  }
  REQUIRE(sfx.has_value());
  CHECK(sfx->definitions.size() == 10U);
  CHECK(sfx->section_d.size() == 1U);
  CHECK(sfx->nodes.size() == 11U);
  CHECK(sfx->tracks.size() == 5U);
  REQUIRE(sfx->tracks.size() == 5U);
  CHECK(sfx->tracks.at(0).points.size() == 2U);
  CHECK(sfx->tracks.at(1).points.size() == 27U);
  CHECK(sfx->tracks.at(2).points.size() == 3U);
  CHECK(sfx->tracks.at(3).points.size() == 2U);
  CHECK(sfx->tracks.at(4).points.size() == 5U);

  std::size_t automatic{0U};
  std::size_t script_1{0U};
  std::size_t script_8{0U};
  std::size_t script_20{0U};
  for (const App::Omikron::SfxNode& node : sfx->nodes) {
    automatic += node.trigger_type == 1 && node.trigger_id == -1 ? 1U : 0U;
    script_1 += node.trigger_type == 0 && node.trigger_id == 1 ? 1U : 0U;
    script_8 += node.trigger_type == 0 && node.trigger_id == 8 ? 1U : 0U;
    script_20 += node.trigger_type == 0 && node.trigger_id == 20 ? 1U : 0U;
  }
  CHECK(automatic == 4U);
  CHECK(script_1 == 1U);
  CHECK(script_8 == 1U);
  CHECK(script_20 == 5U);
  REQUIRE(sfx->definitions.size() >= 5U);
  CHECK(sfx->definitions.at(3).sprite_render_mode == 6U);
  CHECK(sfx->definitions.at(1).sprite_render_mode == 4U);
  CHECK(sfx->definitions.at(2).sprite_render_mode == 4U);
  CHECK(sfx->definitions.at(4).sprite_render_mode == 4U);

  const auto scx_file{App::load_game_file("SCPTDATA/GRID.SCX")};
  if (!scx_file) {
    FAIL(scx_file.error());
  }
  REQUIRE(scx_file.has_value());
  const auto scx{App::Omikron::SCX::load(std::span<const std::byte>{scx_file->bytes})};
  if (!scx) {
    FAIL(scx.error());
  }
  REQUIRE(scx.has_value());
  const auto sprite_name = [&scx](const std::uint32_t authored_id) -> std::string {
    for (const App::Omikron::ScxSpriteEntry& sprite : scx->sprites) {
      if (sprite.sprite_id == authored_id) {
        return sprite.name;
      }
    }
    return {};
  };
  CHECK(sprite_name(9U).find("EFFECTS2_SMOKE1") != std::string::npos);
  CHECK(sprite_name(10U).find("EFFECTS1_IMPACT2") != std::string::npos);
  CHECK(sprite_name(11U).find("EFFECTS1_IMPACT1") != std::string::npos);
  CHECK(sprite_name(12U).find("EFFECTS3_SMOKB") != std::string::npos);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, cert-err33-c)

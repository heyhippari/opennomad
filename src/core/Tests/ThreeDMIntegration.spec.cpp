#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/ThreeDM.hpp"

namespace {

struct RetailExpectation {
  std::string_view basename;
  std::size_t file_size;
  std::uint32_t field_08;
  std::size_t visual_frames;
  std::size_t audio_frames;
};

}  // namespace

TEST_CASE("[RETAIL] dialog 272 3DM streams match recovered physical facts") {
  constexpr RetailExpectation K_EXPECTATIONS[]{{.basename = "125338",
                                                   .file_size = 3465536U,
                                                   .field_08 = 911U,
                                                   .visual_frames = 911U,
                                                   .audio_frames = 911U},
      {.basename = "125339",
          .file_size = 4127064U,
          .field_08 = 1299U,
          .visual_frames = 1085U,
          .audio_frames = 1084U},
      {.basename = "12533A",
          .file_size = 3659172U,
          .field_08 = 0U,
          .visual_frames = 962U,
          .audio_frames = 961U}};
  for (const RetailExpectation& expected : K_EXPECTATIONS) {
    const auto file{
        App::load_game_file(std::string{"MORPH/"} + std::string{expected.basename} + ".3dm")};
    REQUIRE(file.has_value());
    CHECK_EQ(file->bytes.size(), expected.file_size);
    const auto clip{App::Omikron::ThreeDM::load(file->bytes)};
    REQUIRE(clip.has_value());
    CHECK_EQ(clip->header().stream_mode, 0U);
    CHECK_EQ(clip->header().audio_bytes_per_frame, 368U);
    CHECK_EQ(clip->header().morph_vertex_count, 130U);
    CHECK_EQ(clip->header().object_ids.size(), 19U);
    if (expected.field_08 != 0U) {
      CHECK_EQ(clip->header().field_08, expected.field_08);
    }
    CHECK_EQ(clip->frames().size(), expected.visual_frames);
    CHECK_EQ(clip->audio_chunk_count(), expected.audio_frames);
  }
}

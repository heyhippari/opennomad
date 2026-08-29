#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Omikron/SCX.hpp"
#include "Core/Resources.hpp"
#include "Core/Sprite/SpriteResource.hpp"

namespace {

constexpr std::string_view K_SCX_PATH{"SCPTDATA/aventure.SCX"};

/// Loads a game file from the data root set via the OPENNOMAD_GAME_DATA_ROOT
/// environment variable. Returns nullopt when the data is unavailable.
std::optional<std::vector<std::byte>> load_game_file(const std::filesystem::path& relative_path) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* root{std::getenv("OPENNOMAD_GAME_DATA_ROOT")};
  if (root == nullptr) {
    return std::nullopt;
  }
  const std::filesystem::path resolved{
      App::Resources::resolve_case_insensitive(std::filesystem::path{root} / relative_path)};
  std::size_t size{0};
  void* raw{SDL_LoadFile(resolved.string().c_str(), &size)};
  if (raw == nullptr) {
    return std::nullopt;
  }
  std::vector<std::byte> bytes(size);
  if (size > 0) {
    std::memcpy(bytes.data(), raw, size);
  }
  SDL_free(raw);
  return bytes;
}

}  // namespace

TEST_SUITE("Core::Sprite::GameDataSpriteIntegration") {
  TEST_CASE("Frame tables of all 20 embedded effect resources resolve") {
    const auto scx_file{load_game_file(K_SCX_PATH)};
    if (!scx_file.has_value()) {
      WARN("OPENNOMAD_GAME_DATA_ROOT is not set or aventure.SCX is missing; test skipped");
      return;
    }

    const auto scx{App::Omikron::SCX::load(*scx_file)};
    REQUIRE(scx.has_value());
    REQUIRE_EQ(scx->models.size(), scx->sprites.size());
    REQUIRE_FALSE(scx->models.empty());

    const std::span<const std::byte> bytes{*scx_file};
    for (std::size_t index{0}; index < scx->models.size(); ++index) {
      CAPTURE(index);
      const auto resource{App::Sprite::SpriteResource::create(
          bytes, scx->models.at(index), scx->sprites.at(index))};
      REQUIRE(resource.has_value());
      MESSAGE("sprite ",
          index,
          " '",
          resource->name,
          "' (id ",
          resource->sprite_id,
          "): ",
          resource->object_count(),
          " objects, ",
          resource->images.size(),
          " textures");

      CHECK_EQ(resource->model.header.frame_count, 0U);  // Provisional fallback applies.
      CHECK_FALSE(resource->images.empty());

      // Every object with a frame table must resolve every frame.
      bool any_frames{false};
      for (std::size_t object{0}; object < resource->object_count(); ++object) {
        const std::size_t frames{resource->frame_count(object)};
        if (frames == 0) {
          continue;
        }
        any_frames = true;
        for (std::uint16_t frame{0}; frame < frames; ++frame) {
          const auto resolved{resource->resolve_frame(object, frame, 0.0F, 0.0F)};
          INFO("resource '", resource->name, "' object ", object, " frame ", frame);
          if (!resolved.has_value()) {
            MESSAGE(resolved.error().message);
          }
          CHECK(resolved.has_value());
          if (resolved.has_value()) {
            CHECK_GT(resolved->width, 0.0F);
            CHECK_GT(resolved->height, 0.0F);
            CHECK(resolved->texture_index >= 0);
            CHECK(static_cast<std::size_t>(resolved->texture_index) <
                  resource->model.materials.size());
          }
        }
      }
      CHECK(any_frames);
    }
  }

  TEST_CASE("EFFECTS2_SMOKE2.3DO provides a spawnable frame table") {
    const auto scx_file{load_game_file(K_SCX_PATH)};
    if (!scx_file.has_value()) {
      WARN("OPENNOMAD_GAME_DATA_ROOT is not set or aventure.SCX is missing; test skipped");
      return;
    }

    const auto scx{App::Omikron::SCX::load(*scx_file)};
    REQUIRE(scx.has_value());
    REQUIRE_FALSE(scx->sprites.empty());
    CHECK_EQ(scx->sprites.at(0).name, "EFFECTS2_SMOKE2.3DO");

    const auto resource{App::Sprite::SpriteResource::create(
        std::span<const std::byte>{*scx_file}, scx->models.at(0), scx->sprites.at(0))};
    REQUIRE(resource.has_value());

    const std::size_t object{resource->default_object_index()};
    const std::size_t frames{resource->frame_count(object)};
    REQUIRE_GT(frames, std::size_t{0});
    MESSAGE("EFFECTS2_SMOKE2.3DO: object ", object, ", ", frames, " frames");

    const auto first{resource->resolve_frame(object, 0, 0.0F, 0.0F)};
    REQUIRE(first.has_value());
    MESSAGE("frame 0: ",
        first->width,
        " x ",
        first->height,
        " units, texture ",
        first->texture_index,
        ", uv0 (",
        first->uv0.at(0),
        ", ",
        first->uv0.at(1),
        "), uv1 (",
        first->uv1.at(0),
        ", ",
        first->uv1.at(1),
        ")");
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

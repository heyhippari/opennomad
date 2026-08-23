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

#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Omikron/Texture3DT.hpp"
#include "Core/Resources.hpp"

namespace {

constexpr std::string_view K_SCX_RELATIVE_PATH{"SCPTDATA/aventure.SCX"};
constexpr std::string_view K_GRID_SCX_RELATIVE_PATH{"SCPTDATA/Grid.SCX"};
/// The embedded effect model the application displays.
constexpr std::string_view K_SELECTED_MODEL{"EFFECTS2_SMOKE2.3DO"};

/// Loads aventure.SCX from the game-data root set via the
/// OPENNOMAD_GAME_DATA_ROOT environment variable. Returns nullopt when the
/// original data is unavailable.
std::optional<std::vector<std::byte>> load_scx_file() {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* root{std::getenv("OPENNOMAD_GAME_DATA_ROOT")};
  if (root == nullptr) {
    return std::nullopt;
  }
  const std::filesystem::path resolved{App::Resources::resolve_case_insensitive(
      std::filesystem::path{root} / std::filesystem::path{K_SCX_RELATIVE_PATH})};
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

/// Loads Grid.SCX from the game-data root (mirrors load_scx_file).
std::optional<std::vector<std::byte>> load_grid_file() {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* root{std::getenv("OPENNOMAD_GAME_DATA_ROOT")};
  if (root == nullptr) {
    return std::nullopt;
  }
  const std::filesystem::path resolved{App::Resources::resolve_case_insensitive(
      std::filesystem::path{root} / std::filesystem::path{K_GRID_SCX_RELATIVE_PATH})};
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

TEST_SUITE("Core::Omikron::SCXIntegration") {
  TEST_CASE("Matches the verified aventure.SCX facts") {
    const auto file{load_scx_file()};
    if (!file.has_value()) {
      WARN("OPENNOMAD_GAME_DATA_ROOT is not set or aventure.SCX is missing; test skipped");
      return;
    }

    CHECK_EQ(file->size(), 3078193U);

    const auto scx{App::Omikron::SCX::load(*file)};
    REQUIRE(scx.has_value());

    CHECK_EQ(scx->header.magic, 0x00DEAD00U);
    CHECK_EQ(scx->header.version, 5U);
    CHECK_EQ(scx->header.descriptor_size, 0x41C8U);
    CHECK_EQ(scx->resource_stream_offset, 0x41D8U);
    REQUIRE_EQ(scx->sprites.size(), 20U);
    REQUIRE_EQ(scx->waves.size(), 53U);
    REQUIRE_EQ(scx->models.size(), 20U);

    CHECK_EQ(scx->sprites.at(0).name, "EFFECTS2_SMOKE2.3DO");
    CHECK_EQ(scx->sprites.at(19).name, "EFFECTS2_STAR1.3DO");
    CHECK_EQ(scx->models.at(0).header_offset, 0x27D676U);
    CHECK_EQ(scx->models.at(0).core_offset, 0x27D682U);
    CHECK_EQ(scx->models.at(0).core_size, 0x684U);
    CHECK_EQ(scx->models.at(0).auxiliary_size, 0x437EU);

    // The twentieth model package ends exactly at the end of the file.
    const App::Omikron::ScxModelResource& last{scx->models.at(19)};
    CHECK_EQ(last.auxiliary_offset + last.auxiliary_size, file->size());

    // Selection diagnostics: full sprite and model metadata, in file order.
    for (std::size_t index{0}; index < scx->sprites.size(); ++index) {
      const App::Omikron::ScxSpriteEntry& sprite{scx->sprites.at(index)};
      MESSAGE("sprite ", index, ": '", sprite.name, "' id ", sprite.sprite_id);
    }
    for (std::size_t index{0}; index < scx->models.size(); ++index) {
      const App::Omikron::ScxModelResource& resource{scx->models.at(index)};
      MESSAGE("model ", index, ": header ", resource.header_offset, ", core ",
          resource.core_offset, " (", resource.core_size, " bytes), auxiliary ",
          resource.auxiliary_offset, " (", resource.auxiliary_size, " bytes)");
    }
  }

  TEST_CASE("Decodes every embedded model (selection diagnostic)") {
    const auto file{load_scx_file()};
    if (!file.has_value()) {
      WARN("OPENNOMAD_GAME_DATA_ROOT is not set or aventure.SCX is missing; test skipped");
      return;
    }

    const auto scx{App::Omikron::SCX::load(*file)};
    REQUIRE(scx.has_value());
    REQUIRE_EQ(scx->models.size(), scx->sprites.size());

    const std::span<const std::byte> all{*file};
    std::size_t decoded_count{0};
    bool selected_model_decoded{false};
    for (std::size_t index{0}; index < scx->models.size(); ++index) {
      const App::Omikron::ScxModelResource& resource{scx->models.at(index)};
      const std::string& name{scx->sprites.at(index).name};

      auto model{App::Omikron::Model3DO::load(all.subspan(resource.core_offset, resource.core_size))};
      if (!model) {
        MESSAGE("Model '", name, "' failed to decode: ", model.error());
        continue;
      }
      auto groups{App::Omikron::Model3DO::build_static_geometry(model.value())};
      if (!groups) {
        MESSAGE("Model '", name, "' failed to build geometry: ", groups.error());
        continue;
      }
      auto images{App::Omikron::Texture3DT::load(
          all.subspan(resource.auxiliary_offset, resource.auxiliary_size), model->materials)};
      if (!images) {
        MESSAGE("Model '", name, "' failed to decode textures: ", images.error());
        continue;
      }

      // The third model-header word must equal exactly what the material
      // descriptors consume (palettes plus payloads, no padding): this is
      // what lets the texture cursor end precisely at the auxiliary end.
      const auto expected_aux{App::Omikron::Texture3DT::encoded_size(model->materials)};
      REQUIRE(expected_aux.has_value());
      CHECK_EQ(expected_aux.value(), resource.auxiliary_size);

      ++decoded_count;
      MESSAGE("Model '", name, "' decoded: ", groups->size(), " groups, ", images->size(),
          " textures");
      if (name == K_SELECTED_MODEL && !groups->empty()) {
        selected_model_decoded = true;
      }
    }

    CHECK_GT(decoded_count, 0U);
    CHECK(selected_model_decoded);
  }

  TEST_CASE("Parses the DEAD0002 script section (selection diagnostic)") {
    const auto file{load_scx_file()};
    if (!file.has_value()) {
      WARN("OPENNOMAD_GAME_DATA_ROOT is not set or aventure.SCX is missing; test skipped");
      return;
    }

    const auto scx{App::Omikron::SCX::load(*file)};
    REQUIRE(scx.has_value());

    // Order-of-magnitude diagnostics, not hardcoded parsing constants.
    CHECK_EQ(scx->scripts.size(), 22U);
    CHECK_EQ(scx->shared_values.size(), 392U);

    // The first script is the verified effects2_smoke2 effect; dump its
    // command chain and the argument words it addresses.
    const App::Omikron::ScxScript& first{scx->scripts.at(0)};
    MESSAGE("script 0: '", first.name, "' id ", first.script_id, " roots ",
        first.root_command_count, " linked ", first.linked_command_count, " repeat limit ",
        first.repeat_limit);

    for (std::size_t index{0}; index < first.root_commands.size(); ++index) {
      const App::Omikron::ScxScriptCommand& command{first.root_commands.at(index)};
      MESSAGE("  root ", index, ": opcode ", command.opcode, " args[",
          command.first_value_index, "..", command.first_value_index + command.value_count,
          ") next ", command.next_linked_command_index.value_or(0xFFFFFFFFU));
    }
    for (std::size_t index{0}; index < first.linked_commands.size(); ++index) {
      const App::Omikron::ScxScriptCommand& command{first.linked_commands.at(index)};
      MESSAGE("  linked ", index, ": opcode ", command.opcode, " args[",
          command.first_value_index, "..", command.first_value_index + command.value_count,
          ") next ", command.next_linked_command_index.value_or(0xFFFFFFFFU));
      for (std::uint32_t arg{0}; arg < command.value_count; ++arg) {
        const App::Omikron::ScriptValue& value{
            scx->shared_values.at(command.first_value_index + arg)};
        MESSAGE("    arg ", arg, ": raw ", value.raw, " float ", value.as_float(),
            " unsigned ", value.as_unsigned());
      }
    }
  }

  TEST_CASE("Grid.SCX parses through the same loader and reports its inventory") {
    const auto file{load_grid_file()};
    if (!file.has_value()) {
      WARN("OPENNOMAD_GAME_DATA_ROOT is not set or Grid.SCX is missing; test skipped");
      return;
    }

    CHECK_EQ(file->size(), 1315472U);

    const auto scx{App::Omikron::SCX::load(*file)};
    CHECK_MESSAGE(scx.has_value(), scx.error());
    if (!scx) {
      return;
    }

    CHECK_EQ(scx->header.magic, 0x00DEAD00U);
    CHECK_EQ(scx->header.version, 5U);

    MESSAGE("Grid.SCX: header descriptor_size ", scx->header.descriptor_size,
        " stream offset ", scx->resource_stream_offset);
    MESSAGE("Grid.SCX: sprites ", scx->sprites.size(), " sounds ", scx->sounds.size(),
        " waves ", scx->waves.size(), " models ", scx->models.size(), " scripts ",
        scx->scripts.size(), " shared values ", scx->shared_values.size());

    // The loader enforces a parallel sprite/model table, as for aventure.SCX.
    CHECK_EQ(scx->models.size(), scx->sprites.size());

    for (std::size_t index{0}; index < scx->scripts.size(); ++index) {
      const App::Omikron::ScxScript& script{scx->scripts.at(index)};
      MESSAGE("Grid script ", index, ": '", script.name, "' id ", script.script_id,
          " roots ", script.root_command_count, " linked ", script.linked_command_count);
    }
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

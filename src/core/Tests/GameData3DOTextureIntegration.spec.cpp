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

constexpr std::string_view K_LEVEL_MODEL{"MESHES/DECORS/Anekbah.3DO"};
constexpr std::string_view K_LEVEL_TEXTURES{"MESHES/DECORS/Anekbah.3DT"};
constexpr std::string_view K_CHARACTER_MODEL{"MESHES/PERSOS/KIL2_FN.3DO"};
constexpr std::string_view K_CHARACTER_TEXTURES{"MESHES/PERSOS/KIL2_FN.3dt"};
constexpr std::string_view K_SCX_PATH{"SCPTDATA/aventure.SCX"};

/// Loads a game file from the data root set via the OPENNOMAD_GAME_DATA_ROOT
/// environment variable. Returns nullopt when the data is unavailable.
std::optional<std::vector<std::byte>> load_game_file(const std::filesystem::path& relative_path) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* root{std::getenv("OPENNOMAD_GAME_DATA_ROOT")};
  if (root == nullptr) {
    return std::nullopt;
  }
  const std::filesystem::path resolved{App::Resources::resolve_case_insensitive(
      std::filesystem::path{root} / relative_path)};
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

/// Checks the material trailer shared by every observed record: an 8-bit
/// 256x256 texture whose runtime page/slot fields are unallocated (0xFFFF)
/// and whose atlas offsets are zero.
void check_common_material_fields(const App::Omikron::Material& material) {
  CHECK_EQ(material.bits_per_pixel, 8U);
  CHECK_EQ(material.width, 256U);
  CHECK_EQ(material.height, 256U);
  CHECK_EQ(material.texture_page_index, 0xFFFFU);
  CHECK_EQ(material.texture_slot_index, 0xFFFFU);
  CHECK_EQ(material.palette_page_index, 0xFFFFU);
  CHECK_EQ(material.palette_slot_index, 0xFFFFU);
  CHECK_EQ(material.atlas_u_offset, 0U);
  CHECK_EQ(material.atlas_v_offset, 0U);
}

/// Decodes a .3DO model and its .3DT sidecar. Returns nullopt (after a
/// diagnostic) when either the game data or one of the decoders is missing.
std::optional<App::Omikron::Model3DOData> decode_model(
    const std::optional<std::vector<std::byte>>& model_file,
    const std::optional<std::vector<std::byte>>& texture_file,
    const std::string_view name) {
  if (!model_file.has_value() || !texture_file.has_value()) {
    MESSAGE("OPENNOMAD_GAME_DATA_ROOT is not set or ", name, " is missing; test skipped");
    return std::nullopt;
  }
  auto model{App::Omikron::Model3DO::load(*model_file)};
  if (!model) {
    MESSAGE("Failed to decode ", name, ": ", model.error());
    return std::nullopt;
  }
  auto images{App::Omikron::Texture3DT::load(*texture_file, model->materials)};
  if (!images) {
    MESSAGE("Failed to decode ", name, " textures: ", images.error());
    return std::nullopt;
  }
  REQUIRE_EQ(images->size(), model->materials.size());
  return model.value();
}

}  // namespace

TEST_SUITE("Core::Omikron::GameData3DOTextureIntegration") {
  TEST_CASE("Decodes the Anekbah level model and its textures") {
    const auto model_file{load_game_file(K_LEVEL_MODEL)};
    const auto texture_file{load_game_file(K_LEVEL_TEXTURES)};
    const auto model{decode_model(model_file, texture_file, K_LEVEL_MODEL)};
    if (!model.has_value()) {
      return;
    }

    CHECK_EQ(std::string_view{model->header.signature.data(), model->header.signature.size()},
             "OD3X");
    CHECK_EQ(model->header.version_major, 4U);
    // Confirmed serialized values of the original root structure.
    CHECK_EQ(model->header.frame_count, 0U);
    CHECK_EQ(model->header.texture_count, 0U);
    CHECK_EQ(static_cast<std::size_t>(model->header.object_count), model->meshes.size());
    REQUIRE_EQ(model->materials.size(), std::size_t{20});

    const auto& first{model->materials.at(0)};
    CHECK_EQ(first.name, "BATITR05");
    CHECK_EQ(first.texture_name, "BATITR05.BMP");
    CHECK_EQ(first.palette_name, "BATITR05.BMP");
    CHECK_EQ(first.data_size, 48920U);
    check_common_material_fields(first);
    for (const auto& material : model->materials) {
      check_common_material_fields(material);
    }

    const auto groups{App::Omikron::Model3DO::build_static_geometry(model.value())};
    REQUIRE(groups.has_value());
    CHECK_FALSE(groups->empty());
  }

  TEST_CASE("Decodes the KIL2_FN character model and its textures") {
    const auto model_file{load_game_file(K_CHARACTER_MODEL)};
    const auto texture_file{load_game_file(K_CHARACTER_TEXTURES)};
    const auto model{decode_model(model_file, texture_file, K_CHARACTER_MODEL)};
    if (!model.has_value()) {
      return;
    }

    REQUIRE_EQ(model->materials.size(), std::size_t{2});
    const auto& first{model->materials.at(0)};
    CHECK_EQ(first.name, "ANISBUS");
    CHECK_EQ(first.texture_name, "ANISBUS.BMP");
    CHECK_EQ(first.palette_name, "ANISBUS.TGA");
    CHECK_EQ(first.data_size, 35714U);
    check_common_material_fields(first);

    const auto& second{model->materials.at(1)};
    CHECK_EQ(second.name, "ANISLEG");
    CHECK_EQ(second.texture_name, "ANISLEG.BMP");
    CHECK_EQ(second.palette_name, "ANISLEG.TGA");
    CHECK_EQ(second.data_size, 33454U);
    check_common_material_fields(second);

    const auto groups{App::Omikron::Model3DO::build_static_geometry(model.value())};
    REQUIRE(groups.has_value());
  }

  TEST_CASE("Decodes EFFECTS2_SMOKE2.3DO (parser correctness, not animation)") {
    const auto scx_file{load_game_file(K_SCX_PATH)};
    if (!scx_file.has_value()) {
      WARN("OPENNOMAD_GAME_DATA_ROOT is not set or aventure.SCX is missing; test skipped");
      return;
    }

    const auto scx{App::Omikron::SCX::load(*scx_file)};
    REQUIRE(scx.has_value());
    REQUIRE_EQ(scx->models.size(), scx->sprites.size());
    REQUIRE_FALSE(scx->sprites.empty());

    CHECK_EQ(scx->sprites.at(0).name, "EFFECTS2_SMOKE2.3DO");
    const App::Omikron::ScxModelResource& resource{scx->models.at(0)};
    const std::span<const std::byte> all{*scx_file};

    const auto model{
        App::Omikron::Model3DO::load(all.subspan(resource.core_offset, resource.core_size))};
    REQUIRE(model.has_value());
    REQUIRE_EQ(model->materials.size(), std::size_t{1});

    const auto& material{model->materials.at(0)};
    CHECK_EQ(material.name, "EFFECTS2");
    CHECK_EQ(material.texture_name, "EFFECTS2.BMP");
    CHECK_EQ(material.palette_name, "EFFECTS2.BMP");
    CHECK_EQ(material.data_size, 16510U);
    check_common_material_fields(material);

    const auto images{App::Omikron::Texture3DT::load(
        all.subspan(resource.auxiliary_offset, resource.auxiliary_size), model->materials)};
    REQUIRE(images.has_value());
    REQUIRE_EQ(images->size(), std::size_t{1});
    CHECK_EQ(images->at(0).rgba8.size(), std::size_t{256U} * 256U * 4U);

    // The auxiliary header word equals the exact consumption of the single
    // material: an 8-bit palette (768 bytes) plus its 16510-byte payload.
    const auto expected_aux{App::Omikron::Texture3DT::encoded_size(model->materials)};
    REQUIRE(expected_aux.has_value());
    CHECK_EQ(expected_aux.value(), std::size_t{17278});
    CHECK_EQ(expected_aux.value(), resource.auxiliary_size);

    // NOTE: this validates that the embedded texture decodes; it does not
    // declare sprite-frame animation fixed. No SpriteInstance/frameIndex
    // support exists yet — the model renders its first frame statically.
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

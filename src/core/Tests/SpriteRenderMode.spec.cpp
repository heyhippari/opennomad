#include "Core/Sprite/SpriteRenderMode.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

namespace {

using App::Sprite::BlendFactor;
using App::Sprite::SpriteRenderMode;

/// Expected decoded renderer behaviour of one mode.
struct ExpectedState {
  bool blend_enabled{false};
  BlendFactor source{BlendFactor::k_one};
  BlendFactor destination{BlendFactor::k_zero};
  bool depth_write{true};
  bool cutout{false};
  bool fogged{true};
};

/// The proven mode table (values are the original numeric render modes).
const std::array<std::pair<SpriteRenderMode, ExpectedState>, 9> k_modes{{
    {SpriteRenderMode::k_default,
        {false, BlendFactor::k_one, BlendFactor::k_zero, true, false, true}},
    {SpriteRenderMode::k_cutout,
        {false, BlendFactor::k_one, BlendFactor::k_zero, true, true, true}},
    {SpriteRenderMode::k_alpha,
        {true,
            BlendFactor::k_source_alpha,
            BlendFactor::k_one_minus_source_alpha,
            false,
            false,
            false}},
    {SpriteRenderMode::k_alpha_cutout,
        {true,
            BlendFactor::k_source_alpha,
            BlendFactor::k_one_minus_source_alpha,
            false,
            true,
            false}},
    {SpriteRenderMode::k_additive,
        {true, BlendFactor::k_one, BlendFactor::k_one, false, false, false}},
    {SpriteRenderMode::k_additive_cutout,
        {true, BlendFactor::k_one, BlendFactor::k_one, false, true, false}},
    {SpriteRenderMode::k_darken,
        {true, BlendFactor::k_zero, BlendFactor::k_one_minus_source_color, false, false, false}},
    {SpriteRenderMode::k_darken_cutout,
        {true, BlendFactor::k_zero, BlendFactor::k_one_minus_source_color, false, true, false}},
    {SpriteRenderMode::k_alternate_cutout,
        {false, BlendFactor::k_one, BlendFactor::k_zero, true, true, true}},
}};

}  // namespace

TEST_SUITE("Core::Sprite::SpriteRenderMode") {
  TEST_CASE("Modes 0-8 map to the proven renderer state") {
    for (const auto& [mode, expected] : k_modes) {
      CAPTURE(static_cast<unsigned int>(mode));
      const App::Sprite::SpriteRenderState state{App::Sprite::render_state(mode)};
      CHECK_EQ(state.blend_enabled, expected.blend_enabled);
      CHECK_EQ(state.source_factor, expected.source);
      CHECK_EQ(state.destination_factor, expected.destination);
      CHECK_EQ(state.depth_write, expected.depth_write);
      CHECK_EQ(state.cutout, expected.cutout);
      CHECK_EQ(state.fogged, expected.fogged);
    }
  }

  TEST_CASE("Numeric values match the original runtime render modes") {
    CHECK_EQ(static_cast<std::uint16_t>(SpriteRenderMode::k_default), 0U);
    CHECK_EQ(static_cast<std::uint16_t>(SpriteRenderMode::k_cutout), 1U);
    CHECK_EQ(static_cast<std::uint16_t>(SpriteRenderMode::k_alpha), 2U);
    CHECK_EQ(static_cast<std::uint16_t>(SpriteRenderMode::k_alpha_cutout), 3U);
    CHECK_EQ(static_cast<std::uint16_t>(SpriteRenderMode::k_additive), 4U);
    CHECK_EQ(static_cast<std::uint16_t>(SpriteRenderMode::k_additive_cutout), 5U);
    CHECK_EQ(static_cast<std::uint16_t>(SpriteRenderMode::k_darken), 6U);
    CHECK_EQ(static_cast<std::uint16_t>(SpriteRenderMode::k_darken_cutout), 7U);
    CHECK_EQ(static_cast<std::uint16_t>(SpriteRenderMode::k_alternate_cutout), 8U);
  }

  TEST_CASE("Modes 1 and 8 stay distinct with shared renderer behaviour") {
    CHECK(static_cast<std::uint16_t>(SpriteRenderMode::k_cutout) !=
          static_cast<std::uint16_t>(SpriteRenderMode::k_alternate_cutout));
    const App::Sprite::SpriteRenderState cutout{
        App::Sprite::render_state(SpriteRenderMode::k_cutout)};
    const App::Sprite::SpriteRenderState alternate{
        App::Sprite::render_state(SpriteRenderMode::k_alternate_cutout)};
    CHECK_EQ(cutout.blend_enabled, alternate.blend_enabled);
    CHECK_EQ(cutout.source_factor, alternate.source_factor);
    CHECK_EQ(cutout.destination_factor, alternate.destination_factor);
    CHECK_EQ(cutout.depth_write, alternate.depth_write);
    CHECK_EQ(cutout.cutout, alternate.cutout);
    CHECK_EQ(cutout.fogged, alternate.fogged);
  }

  TEST_CASE("Bucket bits match the original runtime flags") {
    CHECK_EQ(App::Sprite::bucket_bits(SpriteRenderMode::k_default), 0x0000U);
    CHECK_EQ(App::Sprite::bucket_bits(SpriteRenderMode::k_cutout), 0x0400U);
    CHECK_EQ(App::Sprite::bucket_bits(SpriteRenderMode::k_alpha), 0x2000U);
    CHECK_EQ(App::Sprite::bucket_bits(SpriteRenderMode::k_alpha_cutout), 0x2400U);
    CHECK_EQ(App::Sprite::bucket_bits(SpriteRenderMode::k_additive), 0x2100U);
    CHECK_EQ(App::Sprite::bucket_bits(SpriteRenderMode::k_additive_cutout), 0x2500U);
    CHECK_EQ(App::Sprite::bucket_bits(SpriteRenderMode::k_darken), 0x2200U);
    CHECK_EQ(App::Sprite::bucket_bits(SpriteRenderMode::k_darken_cutout), 0x2600U);
    CHECK_EQ(App::Sprite::bucket_bits(SpriteRenderMode::k_alternate_cutout), 0x0400U);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include "Core/Audio/DialogVoiceCodec.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

TEST_CASE("Runtime dialogue ADPCM is high-nibble first with no step-eighth baseline") {
  App::Audio::DialogAdpcmState state;
  std::vector<std::int16_t> output;
  const std::vector<std::byte> encoded{std::byte{0x17}};
  REQUIRE(App::Audio::decode_dialog_adpcm(encoded, state, output).has_value());
  CHECK_EQ(output, std::vector<std::int16_t>({1, 1, 13, 13}));

  state = {};
  output.clear();
  const std::vector<std::byte> zero{std::byte{0x00}};
  REQUIRE(App::Audio::decode_dialog_adpcm(zero, state, output).has_value());
  CHECK_EQ(output, std::vector<std::int16_t>({0, 0, 0, 0}));
}

TEST_CASE("Runtime dialogue ADPCM preserves state across chunks and clamps") {
  const std::vector<std::byte> encoded{std::byte{0x77}, std::byte{0x77}, std::byte{0xF7}};
  App::Audio::DialogAdpcmState continuous_state;
  std::vector<std::int16_t> continuous;
  REQUIRE(App::Audio::decode_dialog_adpcm(encoded, continuous_state, continuous).has_value());

  App::Audio::DialogAdpcmState chunked_state;
  std::vector<std::int16_t> chunked;
  REQUIRE(App::Audio::decode_dialog_adpcm(
      std::span<const std::byte>{encoded}.first(1U), chunked_state, chunked)
          .has_value());
  REQUIRE(App::Audio::decode_dialog_adpcm(
      std::span<const std::byte>{encoded}.subspan(1U), chunked_state, chunked)
          .has_value());
  CHECK_EQ(chunked, continuous);
  CHECK_EQ(chunked_state.predictor, continuous_state.predictor);
  CHECK_EQ(chunked_state.step_index, continuous_state.step_index);

  std::vector<std::byte> saturating(100U, std::byte{0x77});
  REQUIRE(App::Audio::decode_dialog_adpcm(saturating, chunked_state, chunked).has_value());
  CHECK_EQ(chunked_state.predictor, 32767);
  CHECK_EQ(chunked_state.step_index, 88);
  CHECK_EQ(chunked.size(), (encoded.size() + saturating.size()) * 4U);
}

TEST_CASE("one retail-sized ADPCM chunk produces 736 stereo frames") {
  const std::vector<std::byte> encoded(368U, std::byte{0});
  App::Audio::DialogAdpcmState state;
  std::vector<std::int16_t> output;
  REQUIRE(App::Audio::decode_dialog_adpcm(encoded, state, output).has_value());
  CHECK_EQ(output.size(), 1472U);
  CHECK_EQ(output.size() * sizeof(std::int16_t), 2944U);
}

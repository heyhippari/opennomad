#include "Core/Audio/DialogVoiceCodec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace App::Audio {
namespace {

constexpr std::array<std::int32_t, 16> K_INDEX_DELTA{
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
constexpr std::array<std::int32_t, 89> K_STEP_TABLE{7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    16,
    17,
    19,
    21,
    23,
    25,
    28,
    31,
    34,
    37,
    41,
    45,
    50,
    55,
    60,
    66,
    73,
    80,
    88,
    97,
    107,
    118,
    130,
    143,
    157,
    173,
    190,
    209,
    230,
    253,
    279,
    307,
    337,
    371,
    408,
    449,
    494,
    544,
    598,
    658,
    724,
    796,
    876,
    963,
    1060,
    1166,
    1282,
    1411,
    1552,
    1707,
    1878,
    2066,
    2272,
    2499,
    2749,
    3024,
    3327,
    3660,
    4026,
    4428,
    4871,
    5358,
    5894,
    6484,
    7132,
    7845,
    8630,
    9493,
    10442,
    11487,
    12635,
    13899,
    15289,
    16818,
    18500,
    20350,
    22385,
    24623,
    27086,
    29794,
    32767};

void decode_nibble(
    const std::uint8_t nibble, DialogAdpcmState& state, std::vector<std::int16_t>& output) {
  const std::int32_t step{K_STEP_TABLE.at(static_cast<std::size_t>(state.step_index))};
  std::int32_t difference{0};
  difference += (nibble & 0x04U) != 0U ? step * 4 : 0;
  difference += (nibble & 0x02U) != 0U ? step * 2 : 0;
  difference += (nibble & 0x01U) != 0U ? step : 0;
  difference >>= 2;
  state.predictor += (nibble & 0x08U) != 0U ? -difference : difference;
  state.predictor = std::clamp(state.predictor, -32768, 32767);
  state.step_index =
      std::clamp(state.step_index + K_INDEX_DELTA.at(static_cast<std::size_t>(nibble)), 0, 88);
  const auto sample{static_cast<std::int16_t>(state.predictor)};
  output.push_back(sample);
  output.push_back(sample);
}

}  // namespace

std::expected<void, std::string> decode_dialog_adpcm(const std::span<const std::byte> encoded,
    DialogAdpcmState& state,
    std::vector<std::int16_t>& stereo_samples) {
  constexpr std::size_t k_values_per_byte{4U};
  const std::size_t available{stereo_samples.max_size() - stereo_samples.size()};
  if (encoded.size() > available / k_values_per_byte ||
      encoded.size() > std::numeric_limits<std::size_t>::max() / k_values_per_byte) {
    return std::expected<void, std::string>{
        std::unexpect, "dialogue ADPCM decoded sample size overflows"};
  }
  stereo_samples.reserve(stereo_samples.size() + (encoded.size() * k_values_per_byte));
  for (const std::byte value : encoded) {
    const auto byte{std::to_integer<std::uint8_t>(value)};
    decode_nibble(static_cast<std::uint8_t>(byte >> 4U), state, stereo_samples);
    decode_nibble(static_cast<std::uint8_t>(byte & 0x0FU), state, stereo_samples);
  }
  return {};
}

}  // namespace App::Audio

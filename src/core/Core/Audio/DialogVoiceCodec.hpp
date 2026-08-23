#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace App::Audio {

struct DialogAdpcmState {
  std::int32_t predictor{0};
  std::int32_t step_index{0};
};

/// Decodes Runtime's high-nibble-first IMA-family stream into duplicated
/// stereo signed-16 PCM. Decoder state is preserved across calls.
[[nodiscard]] std::expected<void, std::string> decode_dialog_adpcm(
    std::span<const std::byte> encoded,
    DialogAdpcmState& state,
    std::vector<std::int16_t>& stereo_samples);

}  // namespace App::Audio

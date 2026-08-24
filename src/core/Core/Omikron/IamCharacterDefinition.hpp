#pragma once

#include <cstdint>

namespace App::Omikron {

/// Recovered numeric fields shared by the 0x114-byte AREA and SCENE
/// character-definition records. Names remain offset-based because broader
/// gameplay meanings are not fully established.
struct IamCharacterValueInitialState {
  std::int16_t field_9c{0};
  std::int16_t field_9e{0};
  std::int16_t field_a0{0};
  std::int16_t field_a2{0};
  std::int16_t field_a4{0};
  std::int16_t field_a6{0};
  std::int16_t field_a8{0};
  std::int16_t field_aa{0};
  std::uint16_t field_ac{0};
  std::int16_t field_ae{0};
};

}  // namespace App::Omikron

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "OmikronTestBuffer.hpp"

namespace App::Tests {

inline constexpr std::size_t K_AREA_118_RECORD_SIZE{0x09C0U};
inline constexpr std::uint32_t K_AREA_118_PRIMARY_EVENT{0x03FCU};
inline constexpr std::uint32_t K_AREA_118_BYTECODE_POOL_START{0x03FCU};
inline constexpr std::uint32_t K_AREA_118_BYTECODE_POOL_END{0x051CU};
inline constexpr std::array<std::uint32_t, 8> K_AREA_118_TABLE_OFFSETS{
    0x00B4U, 0x00DCU, 0x00DCU, 0x00DCU, 0x00DCU, 0x03ECU, 0x051CU, 0x03FCU};
inline constexpr std::array<std::uint16_t, 8> K_AREA_118_TABLE_COUNTS{
    2U, 0U, 0U, 0U, 2U, 1U, 27U, 0U};

/// Confirmed retail AREA 118 startup bytes through interface 29.
inline Buffer make_area_118_startup_prefix() {
  Buffer bytes;
  bytes.u8(0x0D).u16(175);
  bytes.u8(0x0E).u16(170).u8(50);
  bytes.u8(0x38).u16(136);
  bytes.u8(0x4F).u16(0xFFFF);
  bytes.u8(0x68);
  bytes.u8(0x5C).u16(997);
  bytes.u8(0x83).u16(0).u16(1);
  bytes.u8(0x67).u16(109).u16(1).u16(1);
  bytes.u8(0x76).u32(0).u16(0).u16(0);
  bytes.u8(0x46).u16(29).u16(0xFFFF).u16(19);
  return bytes;
}

}  // namespace App::Tests
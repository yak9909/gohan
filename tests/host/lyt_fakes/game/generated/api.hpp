#pragma once
#include <cstdint>

namespace gohan::game::generated {
// 公開されている CRC-32 の既知の試験文字列を使う。
inline constexpr std::uintptr_t LayoutTopEntry_addr = 1024;
inline constexpr std::uint32_t LayoutTopEntry_len = 9;
inline constexpr std::uint32_t LayoutTopEntry_crc32 = 0xCBF43926u;
inline constexpr std::uintptr_t LayoutBottomEntry_addr = 2048;
inline constexpr std::uint32_t LayoutBottomEntry_len = 9;
inline constexpr std::uint32_t LayoutBottomEntry_crc32 = 0xCBF43926u;
} // namespace gohan::game::generated

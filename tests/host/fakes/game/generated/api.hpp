#pragma once
#include <cstdint>

namespace gohan::game::generated {
// 自作の ASCII 入力用。ゲームの番地・命令・ハッシュは使わない。
inline constexpr std::uintptr_t FrameDispatch_addr = 512;
inline constexpr std::uint32_t FrameDispatch_len = 9;
inline constexpr std::uint32_t FrameDispatch_crc32 = 0xCBF43926;
} // namespace gohan::game::generated

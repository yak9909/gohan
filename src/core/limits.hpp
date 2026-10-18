#pragma once
#include <cstddef>

namespace gohan::core::limits {
inline constexpr std::size_t Features = 32;
inline constexpr std::size_t FrameSteps = 32;
inline constexpr std::size_t StatusText = 512;
// メニュー（src/menu/）。項目数は codegen の上限と同じにする。
inline constexpr std::size_t MenuItems = 192;
inline constexpr std::size_t MenuText = 96;
} // namespace gohan::core::limits

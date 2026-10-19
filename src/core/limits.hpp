#pragma once
#include <cstddef>

namespace gohan::core::limits {
inline constexpr std::size_t Features = 32;
inline constexpr std::size_t FrameSteps = 32;
inline constexpr std::size_t StatusText = 512;
// メニュー（src/menu/）。項目数は codegen の上限と同じにする。
inline constexpr std::size_t MenuItems = 192;
inline constexpr std::size_t MenuText = 96;

// 最小の自前描画の予算。ゲーム側の構造体やヒープの大きさとは独立。
inline constexpr std::size_t RendererRects = 32;
inline constexpr std::size_t RendererTexts = 16;
inline constexpr std::size_t RendererChars = 28;
inline constexpr std::size_t RendererCommands = RendererRects + RendererTexts;
inline constexpr std::size_t RendererRecordBytes = 32768;
inline constexpr std::size_t RendererRecordPercent = 80;
} // namespace gohan::core::limits

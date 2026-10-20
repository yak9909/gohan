#pragma once
#include <cstddef>

namespace gohan::core::limits {
inline constexpr std::size_t Features = 32;
inline constexpr std::size_t FrameSteps = 32;
inline constexpr std::size_t StatusText = 512;
// メニュー（src/menu/）。項目数は codegen の上限と同じにする。
inline constexpr std::size_t MenuItems = 192;
inline constexpr std::size_t MenuText = 96;

// 自前描画の予算。板は上画面 144・下画面 48、文字は各画面 48。
// ゲーム側の構造体やヒープの大きさとは独立。
inline constexpr std::size_t RendererRects = 144;
inline constexpr std::size_t RendererBottomRects = 48;
inline constexpr std::size_t RendererTexts = 48;
inline constexpr std::size_t RendererChars = 28;
inline constexpr std::size_t RendererCommands = RendererRects + RendererTexts;
// 画面ごとの記録用コマンドリストの大きさ（バイト）と、記録量の上限に使う割合。
inline constexpr std::size_t RendererRecordBytes = 98304;
inline constexpr std::size_t RendererRecordPercent = 80;
// コマンドリストが持つ GPU 提出の項目数。
inline constexpr int RendererQueueEntries = 16;
// 文字の枠（TextBox）の最大字数。昇順に並べ、文字列は入る中で最小の空き枠を使う。
// 17 字以上は 20・24・28 字の計 16 枠（28 字は 8 枠）。
inline constexpr unsigned RendererSlotChars[RendererTexts] = {
    2,  2,  2,  2,  2,  2,  4,  4,  4,  4,  4,  4,  4,  4,  8,  8,
    8,  8,  8,  8,  8,  8,  12, 12, 12, 12, 16, 16, 16, 16, 16, 16,
    20, 20, 20, 20, 24, 24, 24, 24, 28, 28, 28, 28, 28, 28, 28, 28};
// 自前の資源を置くゲームのヒープに、導入後も残す空き（バイト）。
// 場面によっては、ゲーム自身が同じヒープから大きな資源を取るため。
// UI は RGBA8 の 256×32 と 8×8 が 2 枚。字形 65,536 と合わせて 98,816 バイト。
inline constexpr std::size_t RendererUiTextureBytes = 33280;
inline constexpr unsigned RendererHeapReserve = 400000;
} // namespace gohan::core::limits

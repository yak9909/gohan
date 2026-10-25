#pragma once
#include <cstddef>
#include "ui_values.hpp"

namespace gohan::core::limits {
inline constexpr std::size_t Features = 32;
inline constexpr std::size_t FrameSteps = 32;
inline constexpr std::size_t StatusText = 4096;
// 入力の遮断。所有者の枠は内蔵の SELECT 用も含む。
inline constexpr std::size_t InputBlockOwners = 16;
inline constexpr unsigned InputDeferWindowMs = 300;
// メニュー（src/menu/）。項目数は codegen の上限と同じにする。
inline constexpr std::size_t MenuItems = 192;
inline constexpr std::size_t MenuText = 96;

// 自前描画の予算。上画面の板・文字枠・記録量は CA2 の設計表から取る。
// ゲーム側の構造体やヒープの大きさとは独立。
inline constexpr std::size_t RendererRects = design::ca2::Budget::Rects;
inline constexpr std::size_t RendererBottomRects = 48;
inline constexpr std::size_t RendererTexts = design::ca2::Budget::Texts;
inline constexpr std::size_t RendererChars = 28;
inline constexpr std::size_t RendererCommands = RendererRects + RendererTexts;
// 画面ごとの記録用コマンドリストの大きさ（バイト）と、記録量の上限に使う割合。
inline constexpr std::size_t RendererRecordBytes = design::ca2::Budget::RecordBytes;
inline constexpr std::size_t RendererRecordPercent = 80;
// コマンドリストが持つ GPU 提出の項目数。
inline constexpr int RendererQueueEntries = 16;
// 文字の枠（TextBox）の最大字数。昇順に並べ、文字列は入る中で最小の空き枠を使う。
// 17〜28字は56枠。総数88で退場中を含む8通知・切り替え中の行・説明・影を収める。
inline constexpr auto RendererSlotChars = [] {
    std::array<unsigned, RendererTexts> result{};
    for (std::size_t i = 0; i < result.size(); ++i) result[i] = design::ca2::Budget::SlotChars[i];
    return result;
}();
// 全画像を導入時に一括確保し、開閉で借り直さない。
inline constexpr std::size_t RendererFontTextureBytes = 32768;
inline constexpr std::size_t RendererUiTextureBytes = 33280;
inline constexpr std::size_t RendererWindowTextureBytes = 32768;
inline constexpr unsigned RendererSelectionSurfaces = 10;
inline constexpr unsigned RendererSelectionIdlePresents = 8;
inline constexpr std::size_t RendererSelectionSurfaceBytes = 2048;
// プラグイン側のコマの保存。GPU画像の予算へは加えない。
inline constexpr unsigned RendererSelectionPhases = 150;
inline constexpr std::size_t RendererSelectionFrameBytes = 182 * 11 / 2;
inline constexpr std::size_t RendererSelectionCacheBytes = RendererSelectionFrameBytes * RendererSelectionPhases;
static_assert(RendererSelectionCacheBytes == 150150);
inline constexpr std::size_t RendererTextureBytes = RendererFontTextureBytes + RendererUiTextureBytes +
    RendererWindowTextureBytes + RendererSelectionSurfaceBytes * RendererSelectionSurfaces;
inline constexpr unsigned RendererGameAllocationSlack = 1024;
inline constexpr unsigned RendererResidentHeapLimit = 120320;
inline constexpr unsigned RendererGameHeapLimit = RendererResidentHeapLimit;
static_assert(RendererTextureBytes == 119296);
static_assert(RendererTextureBytes + RendererGameAllocationSlack <= RendererResidentHeapLimit);
static_assert(RendererSelectionSurfaces >= RendererSelectionIdlePresents + 2);
inline constexpr unsigned RendererHeapSampleFrames = 30;
} // namespace gohan::core::limits

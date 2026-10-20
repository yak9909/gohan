#pragma once
#include <cstddef>
#include "ui_values.hpp"

namespace gohan::core::limits {
inline constexpr std::size_t Features = 32;
inline constexpr std::size_t FrameSteps = 32;
inline constexpr std::size_t StatusText = 2048;
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
// 17字以上は24枠（28字は8枠）。総数56で7通知と長い行・説明・影を収める。
inline constexpr auto RendererSlotChars = [] {
    std::array<unsigned, RendererTexts> result{};
    for (std::size_t i = 0; i < result.size(); ++i) result[i] = design::ca2::Budget::SlotChars[i];
    return result;
}();
// 自前の資源を置くゲームのヒープに、導入後も残す空き（バイト）。
// 場面によっては、ゲーム自身が同じヒープから大きな資源を取るため。
// UI は RGBA8 の 256×32 と 8×8 が 2 枚。字形 65,536 と合わせて 98,816 バイト。
inline constexpr std::size_t RendererUiTextureBytes = 33280;
inline constexpr unsigned RendererHeapReserve = 400000;
// 場面転換後の空きから逆算した、描画資源のヒープから借りる総量の上限。
// 画像本体に加え、各確保の管理欄と128バイト整列の余裕も含める。
inline constexpr unsigned RendererGameHeapLimit = 300000;
inline constexpr unsigned RendererGameAllocationSlack = 256;
inline constexpr unsigned RendererHeapSampleFrames = 30;
} // namespace gohan::core::limits

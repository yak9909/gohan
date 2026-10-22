#pragma once
#include "ui/renderer/renderer.hpp"

namespace gohan::ui::renderer::detail {
inline constexpr unsigned SelectionFirst = 0x53454C00, SelectionSecond = SelectionFirst + 1;
// 導入時に合成して持つ150コマのうち、その位相の1コマ（Morton 順の実画素、1,001 B）。
const std::uint8_t *SelectionFrame(unsigned phase);
// PC のプレビューだけ。実機はゲームの描画用ヒープの面へ直接コピーする。
void StoreSelection(unsigned slot, const std::uint8_t *packed);
// 導入時に150コマを合成した時間（マイクロ秒）。
unsigned SelectionPrepareMicros();
struct SelectionTiming {
    std::uint64_t totalMicros = 0;
    // 毎フレームのコピー（と実機では排出）の時間。
    unsigned samples = 0, lastMicros = 0, maxMicros = 0, unavailable = 0;
    unsigned prepareMicros = 0;
};
SelectionTiming ReadSelectionTiming();
} // namespace gohan::ui::renderer::detail

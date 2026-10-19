#pragma once
#include "menu/surface.hpp"
#include "menu/timing.hpp"

// 画面の割り付け。左にメニューの帯、右に説明（や入力の枠）を置く。
// 帯の寸法は旧版と同じ。右の領域は画面の幅に合わせて広がる（下画面 320 なら 144、上画面 400 なら 224）。
namespace gohan::menu::layout {
inline constexpr int kHeaderHeight = 23;
inline constexpr int kListTop = 25;    // 行の領域の上端
inline constexpr int kFirstRowY = 28;  // 1 行目の文字の y
inline constexpr int kFootTop = 216;
inline constexpr int kHoldBarY = 201;
inline constexpr int kSideX = 168;
inline constexpr int kSideMargin = 8;
inline constexpr int kSideTop = 8;

inline int SideWidth(int screen_width) { return screen_width - kSideX - kSideMargin; }

// 描画の予算（1 画面の文字の枠）から、帯に出す行数を決める。
// 帯の固定の文字（題 1・足 2）と、説明の最小（ホットキー 1・本文 2）を残し、残りを 1 行 2 枠（名前と値）で割る。
inline constexpr int kHeaderTexts = 1;
inline constexpr int kFooterTexts = 2;
inline constexpr int kDescriptionTexts = 3;
inline constexpr int kMinRows = 3;
inline int RowsFor(int texts_left) {
    int rows = (texts_left - kHeaderTexts - kFooterTexts - kDescriptionTexts) / 2;
    if (rows < kMinRows) rows = kMinRows;
    if (rows > style::kVisibleRows) rows = style::kVisibleRows;
    return rows;
}
} // namespace gohan::menu::layout

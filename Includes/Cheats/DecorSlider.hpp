#pragma once

// DecorSlider — カタログの格子の横送り（HHD の PageSlider、回り込みなし = Tum15 の設定）を写す（T022 段 2）。
// 根拠（解析リポジトリ project_v2。HHD v1.1 の逆コンパイル。work/hhd/FINDINGS.md IDA-gpt-6.1-sol-HHD-F001 / IDA-opus-5.5-HHD-F010）:
//   定数 PageSlider_Ctor 0x3470A4（+76 0.2 / +84 0.5 / +88 25 / +92 10 / +96 25 / +100 8）、状態表 0x662C8C（Wait / Drag / Slide）。
//   Drag の入口 0x346F6C: 移動量 = 0、開始位置を控える。Drag 0x346ADC: 指の横の動き分 MoveBy。離す 0x34699C。Slide 0x346D4C + Math_ApproachDamped 0x455EF8。
//   MoveByClamped 0x346460: 方向か量が ±1.1921e-7 の内なら何もしない。位置を [−幅×(ページ数−1), 0] に収め、3 枚の枠を ±1.6 幅で ±3 幅へ移す（ページ ±3）。
//   L / R 0x346228: Wait のときだけ。端では断る。速さ 25。指の速さ DragTracker_Update 0x33D2B8: v = 0.5 v + 2 |画素の差| を [0, 120]、|差| > 1 のときだけ向きを更新。
//   ドラッグの始まり DragArea_ShouldStartDrag 0x3E7B20: 横の |差| の累計（正規化 ×0.00625）>= 0.05 = 8 画素。
// 純粋な計算（描画・入力は呼ぶ側）。tools/hhd/test_decor_slider.py が逆コンパイルの写しと突き合わせる。

#include <3ds/types.h>

namespace DecorSlider {

enum class State : u8 { Wait, Drag, Slide };

struct Slider {
    float width;            // +124 ページ幅（Tum15 = L_Page_02.X − L_Page_01.X = 288）
    s32 pages;              // +148 ページ数（1 以上）
    float pos;              // +132 全体の位置（0 〜 −幅×(ページ数−1)）
    float moved;            // +128 この動きの移動量
    float target;           // +140 滑りの目標（移動量の目標）
    float speed;            // +80 滑りの一歩の上限
    float frameX[3];        // 3 枚の枠の x
    s32 framePage[3];       // +64[] 枠のページ（範囲外 = 隠す）
    u32 center;             // +156 中央の枠
    State state;
    // 指の記録（DragTracker）
    bool touching;
    float lastX, lastY, sumAbsDx, tSpeed, dirX;
};

void Setup(Slider &s, float width, s32 pages, s32 page);       // page を中央にして Wait
void TouchStart(Slider &s, float x, float y);                  // 触れた瞬間（DragArea_ResetTracker）
// 触れている間の毎フレーム。ドラッグになったら真（Wait のときだけ始まる）。x, y = 下画面の画素
bool TouchMove(Slider &s, float x, float y);
void TouchEnd(Slider &s);                                      // 離した（Drag なら Slide へ）
bool RequestStep(Slider &s, bool previous);                    // L = 前（真）/ R = 次。受け付けたら真
bool Step(Slider &s);                                          // 毎フレーム（Slide を進める）。Slide が終わってページが決まったら真
s32 CurrentPage(const Slider &s);

}  // namespace DecorSlider

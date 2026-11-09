#pragma once
#include "core/feature.hpp"
#include "core/format.hpp"

namespace gohan::game {
// 入力遮断前のゲームの写し。C スティックだけは同じ箇所でシステムから補う。
struct InputSnapshot {
    std::uint16_t buttons = 0;
    float slide_x = 0, slide_y = 0;
    std::int16_t cstick_x = 0, cstick_y = 0;
    bool touching = false;
    int touch_x = 0, touch_y = 0;
};
InputSnapshot ReadInputSnapshot();
const core::Feature &InputPadFeature();
const core::Feature &InputControllerFeature();
bool InputControllerReady();
// CTRPF が開くことを許可したときだけ呼ぶ。再開まで保持する。
void InputCtrpfOpening();
// 許可した CTRPF のメニューを閉じてゲームが再開した後、一度だけ真を返す（入力の遮断が入っている間だけ）。
// 毎フレームの処理が、再開の時点で押されているボタンを、離すまでメニューの入力から外すために使う。
bool InputTakeCtrpfResumed();
void AppendInputStatus(core::TextWriter &writer);
#ifdef GOHAN_DEV
void ToggleInputHooks();
#endif
} // namespace gohan::game

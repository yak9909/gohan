#pragma once
// 1 回の呼び出しの間だけゲームの入力欄を書き、終わったら戻す。
#include "game/native_input.hpp"
#include <cstddef>
#include <cstdint>

namespace gohan::ui {
// 画面（320x240）の枠に収め、触れていない時は座標を 0 にする。
struct TouchPoint { std::int32_t x = 0, y = 0; bool down = false; };
inline TouchPoint MakeTouch(int x, int y, bool down) {
    TouchPoint t;
    if (!down) return t;
    t.x = x < 0 ? 0 : x > 319 ? 319 : x;
    t.y = y < 0 ? 0 : y > 239 ? 239 : y;
    t.down = true;
    return t;
}
using NativeInputInjection = game::NativeInputInjection;
using NativeTouch = game::NativeTouch;
} // namespace gohan::ui

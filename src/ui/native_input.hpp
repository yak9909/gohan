#pragma once
// 1 回の呼び出しの間だけゲームの入力欄を書き、終わったら戻す。
#include "game/write_log.hpp"
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
class NativeInputInjection {
  public:
    static constexpr std::size_t Capacity = 16;
    // 書く欄（タッチの構造体・コントローラー 6 欄・メニューマネージャーのキー trig/repeat）を足す。
    bool Put(std::uintptr_t *address, std::uintptr_t value) { return log_.Write(address, value); }
    void End() { log_.Restore(); }
    ~NativeInputInjection() { End(); }
  private:
    game::WriteLog<Capacity> log_;
};
} // namespace gohan::ui

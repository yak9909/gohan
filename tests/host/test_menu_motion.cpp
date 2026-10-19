// 動きの部品（曲線・補間・跳ね）と、タッチのスクロール（タップとの見分け・慣性・端）の試験。
#include "menu/motion.hpp"
#include "menu/touch_scroll.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace gohan::menu;

namespace {
bool Near(float a, float b, float eps = 0.001f) { return std::fabs(a - b) <= eps; }

void Curves() {
    assert(Near(motion::EaseOut(0.0f), 0.0f) && Near(motion::EaseOut(1.0f), 1.0f));
    assert(Near(motion::EaseOut(0.5f), 0.875f)); // 1 - (1 - t)^3
    assert(motion::EaseOut(2.0f) == 1.0f && motion::EaseOut(-1.0f) == 0.0f);
    // 時刻が周回しても差で進む。
    assert(Near(motion::Progress(0xFFFFFFF0u, 0x00000010u, 64), 0.5f));
    assert(motion::Progress(100, 50, 64) == 0.0f && motion::Progress(0, 10, 0) == 1.0f);

    motion::Tween tween;
    tween.Jump(2.0f, 1000);
    assert(tween.At(5000) == 2.0f && tween.Done(1000));
    tween.Go(4.0f, 1000, 100);
    assert(Near(tween.At(1000), 2.0f) && Near(tween.At(1100), 4.0f) && !tween.Done(1050) && tween.Done(1100));
    // 動いている途中で行き先を変えても、今の値から続く。
    const float middle = tween.At(1050);
    tween.Go(0.0f, 1050, 100);
    assert(Near(tween.At(1050), middle) && Near(tween.At(1150), 0.0f));

    motion::Bounce bounce;
    assert(bounce.At(0, 100, 0.3f) == 0.0f);
    bounce.Fire(1000, -1);
    assert(bounce.At(999, 100, 0.3f) == 0.0f && bounce.At(1000, 100, 0.3f) == 0.0f);
    assert(Near(bounce.At(1030, 100, 0.3f), 1.0f) && bounce.At(1100, 100, 0.3f) == 0.0f);
    assert(bounce.Active(1099, 100) && !bounce.Active(1100, 100) && bounce.direction == -1);
}

TouchScroll Fresh(float max) {
    TouchScroll scroll;
    scroll.max = max;
    return scroll;
}

void TapAndDrag() {
    // 動きが小さければタップのまま。離しても慣性は無い。
    TouchScroll tap = Fresh(100.0f);
    u32 t = 1000;
    assert(!tap.Press(100.0f, t));
    for (float y : {101.0f, 102.0f, 103.0f, 102.0f}) tap.Hold(y, t += 16);
    assert(!tap.Scrolling() && tap.scroll == 0.0f);
    tap.Release(t += 16);
    assert(!tap.Flinging());

    // 上へ動かすと増え、境目を越えた所から動く（越えた分だけ飛ばない）。
    TouchScroll drag = Fresh(100.0f);
    t = 1000;
    drag.Press(200.0f, t);
    float y = 200.0f;
    for (int i = 0; i < 20; ++i) drag.Hold(y -= 2.0f, t += 16);
    assert(drag.Scrolling() && drag.scroll > 10.0f && drag.scroll < 40.0f);
    // 下へ戻せば減り、0 で止まる。上へ行き過ぎれば max で止まる。
    for (int i = 0; i < 80; ++i) drag.Hold(y += 3.0f, t += 16);
    assert(drag.scroll == 0.0f);
    for (int i = 0; i < 200; ++i) drag.Hold(y -= 3.0f, t += 16);
    assert(drag.scroll == 100.0f);
}

void Fling() {
    TouchScroll scroll = Fresh(5000.0f);
    u32 t = 1000;
    scroll.Press(200.0f, t);
    float y = 200.0f;
    for (int i = 0; i < 8; ++i) scroll.Hold(y -= 12.0f, t += 16); // 速い（約 750 画素/秒）
    scroll.Release(t);
    assert(scroll.Flinging());
    // 離した後は指数関数的に減速し、十分遅くなったら止まる。向きは変わらない。
    float last = scroll.scroll;
    int frames = 0;
    while (scroll.Flinging() && frames < 600) {
        scroll.Idle(t += 16);
        assert(scroll.scroll >= last);
        last = scroll.scroll;
        ++frames;
    }
    assert(!scroll.Flinging() && frames > 10 && frames < 200 && scroll.scroll > 100.0f);

    // 慣性の最中に触れると止めるだけ（タップにしない）。
    scroll.Press(100.0f, t += 16);
    TouchScroll again = Fresh(5000.0f);
    again.Press(200.0f, t);
    y = 200.0f;
    for (int i = 0; i < 8; ++i) again.Hold(y -= 12.0f, t += 16);
    again.Release(t);
    assert(again.Flinging());
    assert(again.Press(100.0f, t += 16) && !again.Flinging());

    // 端に着いたら慣性も止まる。
    TouchScroll edge = Fresh(10.0f);
    t = 1000;
    edge.Press(200.0f, t);
    y = 200.0f;
    for (int i = 0; i < 8; ++i) edge.Hold(y -= 12.0f, t += 16);
    edge.Release(t);
    for (int i = 0; i < 100 && edge.Flinging(); ++i) edge.Idle(t += 16);
    assert(!edge.Flinging() && edge.scroll == 10.0f);
}

void Spike() {
    // 1 回で 40 画素を越える飛び値は 1 回保留し、続く値が近ければ捨てずに取り込む。
    TouchScroll scroll = Fresh(1000.0f);
    u32 t = 1000;
    scroll.Press(200.0f, t);
    scroll.Hold(300.0f, t += 16); // 飛び（保留）
    assert(!scroll.Scrolling() && scroll.scroll == 0.0f);
    scroll.Hold(201.0f, t += 16);
    assert(!scroll.Scrolling());
}
} // namespace

int main() {
    Curves();
    TapAndDrag();
    Fling();
    Spike();
    std::puts("menu motion: ok");
    return 0;
}

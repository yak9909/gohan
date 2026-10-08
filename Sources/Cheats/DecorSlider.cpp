// DecorSlider — HHD の PageSlider（回り込みなし）を写す。説明と番地は DecorSlider.hpp。

#include "DecorSlider.hpp"

#include <cmath>

namespace DecorSlider {

namespace {

// PageSlider_Ctor 0x3470A4
const float kGain = 0.2f, kSpeedMax = 25.0f, kMinStep = 0.5f, kReleaseMax = 25.0f, kReleaseMin = 10.0f, kStepSpeed = 25.0f, kFlick = 8.0f;
const float kEps = 1.1921e-7f;                  // 0x346644 / 0x346648
const float kRecycle = 1.6f, kSnap = 0.1f;      // MoveByClamped の ±1.6 幅、StepSlide の |x| < 0.1
const float kTrackKeep = 0.5f, kTrackGain = 2.0f, kTrackMax = 120.0f;   // DragArea +436 / +432、DragTracker_Update の上限
const float kNormX = 0.00625f, kDragStart = 0.05f;                      // 正規化 X = 画素 × 0.00625、ドラッグの始まり 0.05

float Abs(float v) { return v < 0.0f ? -v : v; }
bool Visible(const Slider &s, s32 page) { return page >= 0 && page < s.pages; }

// 任意の位置でも各論理ページと枠が一意に対応する。ページを跨ぐ枠だけが読み替わる。
void FreeFrames(Slider &s) {
    const s32 page = (s32)floorf(-s.pos / s.width + 0.5f);
    for (s32 p = page - 1; p <= page + 1; ++p) {
        const u32 f = (u32)((p % 3 + 3) % 3);
        s.framePage[f] = p;
        s.frameX[f] = s.pos + (float)p * s.width;
        if (p == page)
            s.center = f;
    }
}

// MoveByClamped 0x346460: 実際に動いた量を返す
float MoveBy(Slider &s, s32 dir, float delta) {
    const float fdir = (float)dir;
    if ((fdir >= -kEps && fdir <= kEps) || (delta >= -kEps && delta <= kEps))
        return 0.0f;
    const float pos = s.pos, w = s.width, last = -(w * (float)(s.pages - 1));
    float d = delta, next = pos + delta;
    const bool neg = next < 0.0f;
    if (!neg) {
        d = delta - next;
        next = 0.0f;
    }
    if (neg && next <= last) {
        d = last - pos;
        next = last;
    }
    const float hi = w * kRecycle, lo = -hi;
    for (u32 f = 0; f < 3; ++f) {
        float x = s.frameX[f] + d;
        if (dir < 0) {
            while (x < lo) {
                x += w * 3.0f;
                s.framePage[f] -= 3 * dir;      // PageSlider_ShiftFramePage 0x346138: ページ −= 3 × 方向
            }
        } else {
            while (x > hi) {
                x -= w * 3.0f;
                s.framePage[f] -= 3 * dir;
            }
        }
        s.frameX[f] = x;
    }
    s.pos = next;
    s.moved += d;
    if (s.freeScroll)
        FreeFrames(s);
    return d;
}

// Math_ApproachDamped 0x455EF8
float Approach(float cur, float target, float gain, float maxStep, float minStep) {
    if (cur == target)
        return cur;
    float d = (target - cur) * gain;
    if (d < minStep && d > -minStep) {
        if (d <= 0.0f) {
            if (d <= -minStep)
                return cur;
            const float n = cur - minStep;
            return n > target ? n : target;
        }
        if (d >= minStep)
            return cur;
        const float n = cur + minStep;
        return n <= target ? n : target;
    }
    if (d > maxStep)
        d = maxStep;
    if (d < -maxStep)
        d = -maxStep;
    return cur + d;
}

// 目標を全体の範囲に収める（回り込みなしの枝。0x346A7C〜 / 0x346324〜）
float ClampTarget(const Slider &s, float target, float moved) {
    float d = target - moved;
    const float sum = s.pos + d, last = -(s.width * (float)(s.pages - 1));
    if (sum >= 0.0f)
        d = d - sum;
    else if (sum <= last)
        d = last - s.pos;
    return moved + d;
}

}  // namespace

void Setup(Slider &s, float width, s32 pages, s32 page) {
    s.width = width;
    s.pages = pages < 1 ? 1 : pages;
    if (page < 0 || page >= s.pages)
        page = 0;
    s.frameX[0] = -width;
    s.frameX[1] = 0.0f;
    s.frameX[2] = width;
    if (s.pages > 1) {                          // PageSlider_ResetLayout 0x346798（回り込みなし）
        for (u32 f = 0; f < 3; ++f)
            s.framePage[f] = page - 1 + (s32)f;
    } else {
        s.framePage[0] = s.framePage[2] = -1;   // 1 ページなら左右の枠は隠す（隠すだけなので範囲外の番号にする）
        s.framePage[1] = 0;
    }
    s.center = 1;
    s.pos = -(width * (float)page);
    s.moved = s.target = 0.0f;
    s.speed = kSpeedMax;
    s.state = State::Wait;
    s.touching = false;
    s.lastX = s.lastY = s.sumAbsDx = s.tSpeed = s.dirX = 0.0f;
    s.freeScroll = s.coasting = false;
    s.velocity = s.touchStartX = 0.0f;
}

void RestorePosition(Slider &s, float position) {
    const float last = -s.width * (float)(s.pages - 1);
    s.pos = std::isfinite(position) ? (position > 0.0f ? 0.0f : position < last ? last : position) : 0.0f;
    s.moved = s.target = s.velocity = 0.0f;
    s.state = State::Wait;
    s.coasting = s.touching = false;
    FreeFrames(s);
}

void SetupFree(Slider &s, float width, s32 pages, float position) {
    Setup(s, width, pages, 0);
    s.freeScroll = true;
    RestorePosition(s, position);
}

void TouchStart(Slider &s, float x, float y) {
    if (s.freeScroll) {
        s.state = State::Wait;                 // 慣性/LR送り中にも掴み直せる
        s.coasting = false;
        s.velocity = 0.0f;
    }
    s.touching = true;
    s.touchStartX = x;
    s.lastX = x;
    s.lastY = y;
    s.sumAbsDx = 0.0f;
    s.tSpeed = 0.0f;
    s.dirX = 0.0f;
}

bool TouchMove(Slider &s, float x, float y) {
    if (!s.touching)
        return false;
    const float dx = x - s.lastX, dy = y - s.lastY;
    s.lastX = x;
    s.lastY = y;
    // DragTracker_Update 0x33D2B8（画素の差の長さ、速さ、向き）
    const float len = sqrtf(dx * dx + dy * dy);
    float v = s.tSpeed * kTrackKeep + kTrackGain * len;
    if (v < 0.0f)
        v = 0.0f;
    if (v > kTrackMax)
        v = kTrackMax;
    s.tSpeed = v;
    if (len > 1.0f)
        s.dirX = dx / len;
    s.sumAbsDx += Abs(dx * kNormX);
    if (s.freeScroll)
        s.velocity = 0.5f * s.velocity + 0.5f * dx;
    if (s.state == State::Wait) {
        if (s.pages <= 1 || s.sumAbsDx < kDragStart)
            return false;
        s.state = State::Drag;                  // Drag の入口 0x346F6C
        s.moved = 0.0f;
        if (s.freeScroll)
            MoveBy(s, x < s.touchStartX ? -1 : 1, x - s.touchStartX);
        // この フレームの動きは次の Drag の計算から（HHD は始まりのフレームでは動かさない）
        return true;
    }
    if (s.state == State::Drag)
        MoveBy(s, s.dirX < 0.0f ? -1 : 1, dx);  // 0x346ADC: 向き = 記録の向き x の符号（負なら −1）
    return s.state == State::Drag;
}

void TouchEnd(Slider &s) {
    s.touching = false;
    if (s.state != State::Drag)
        return;
    if (s.freeScroll) {
        s.velocity = s.velocity > kSpeedMax ? kSpeedMax : s.velocity < -kSpeedMax ? -kSpeedMax : s.velocity;
        s.coasting = Abs(s.velocity) >= 1.0f;
        s.state = s.coasting ? State::Slide : State::Wait;
        return;
    }
    // PageSlider_BeginSlideFromRelease 0x34699C
    float v = s.tSpeed, proj = 0.0f;
    if (v > kReleaseMax)
        v = kReleaseMax;
    if (v >= kFlick)
        proj = v / 0.05f;
    if (v < kReleaseMin)
        v = kReleaseMin;
    s.speed = v;
    const float sign = s.dirX >= 0.0f ? 1.0f : -1.0f;
    const float t = s.moved + sign * proj;
    const float r = t / s.width;
    s32 idx = (s32)(r < 0.0f ? r - 0.5f : r + 0.5f);
    if (idx >= 1)
        idx = 1;
    else if (idx < -1)
        idx = -1;
    s.target = ClampTarget(s, s.width * (float)idx, s.moved);
    s.state = State::Slide;
}

bool RequestStep(Slider &s, bool previous) {
    if (s.state != State::Wait || s.pages <= 1)
        return false;
    const s32 page = s.framePage[s.center];
    if (previous ? page == 0 : s.pages - 1 <= page)
        return false;
    s.moved = 0.0f;
    s.target = s.freeScroll ? -s.width * (float)(page + (previous ? -1 : 1)) - s.pos : previous ? s.width : -s.width;
    s.coasting = false;
    s.speed = kStepSpeed;
    s.target = ClampTarget(s, s.target, 0.0f);
    s.state = State::Slide;
    return true;
}

bool Step(Slider &s) {
    if (s.state != State::Slide)
        return false;
    if (s.freeScroll && s.coasting) {
        const float got = MoveBy(s, s.velocity < 0.0f ? -1 : 1, s.velocity);
        s.velocity *= 0.9f;
        if (Abs(s.velocity) >= kMinStep && Abs(got) > kEps)
            return false;
        s.state = State::Wait;
        s.coasting = false;
        return true;
    }
    // PageSlider_StepSlide 0x346D4C
    const float m = Approach(s.moved, s.target, kGain, s.speed, kMinStep);
    const s32 dir = (m - s.moved) < 0.0f ? -1 : 1;
    const float got = MoveBy(s, dir, m - s.moved);
    if (got < -kEps || got > kEps)
        return false;
    if (s.freeScroll) {
        s.state = State::Wait;
        return true;
    }
    for (u32 f = 0; f < 3; ++f)
        if (Abs(s.frameX[f]) < kSnap) {
            if (Visible(s, s.framePage[f])) {
                s.pos = -(s.width * (float)s.framePage[f]);
                s.center = f;
            }
            break;
        }
    s.state = State::Wait;                      // PageSlider_StateSlide_Calc 0x346E9C
    return true;
}

s32 CurrentPage(const Slider &s) { return s.framePage[s.center]; }

}  // namespace DecorSlider

#pragma once
#include "menu/types.hpp"

// 動きの部品。時刻はミリ秒（u32、周回しても差で扱う）。減速の曲線は三次の ease-out。
namespace gohan::menu::motion {
inline float Clamp01(float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }
inline float Mix(float from, float to, float t) { return from + (to - from) * t; }
inline float EaseOut(float t) {
    const float u = 1.0f - Clamp01(t);
    return 1.0f - u * u * u;
}
inline float Progress(u32 start, u32 now, u32 duration) {
    if (duration == 0) return 1.0f;
    const s32 elapsed = static_cast<s32>(now - start);
    if (elapsed <= 0) return 0.0f;
    return Clamp01(static_cast<float>(elapsed) / static_cast<float>(duration));
}
inline int Round(float value) { return static_cast<int>(value + (value < 0.0f ? -0.5f : 0.5f)); }

// 値が from から to へ動く。動いている途中で行き先を変えても、今の値から続ける。
struct Tween {
    float from = 0.0f;
    float to = 0.0f;
    u32 start = 0;
    u32 duration = 0;

    float At(u32 now) const { return Mix(from, to, EaseOut(Progress(start, now, duration))); }
    bool Done(u32 now) const { return Progress(start, now, duration) >= 1.0f; }
    void Go(float target, u32 now, u32 ms) {
        from = At(now);
        to = target;
        start = now;
        duration = ms;
    }
    void Jump(float value, u32 now) {
        from = to = value;
        start = now;
        duration = 0;
    }
};

// 一瞬だけ跳ねる動き（0 → 1 → 0）。split は山の位置。
struct Bounce {
    bool on = false;
    u32 at = 0;
    int direction = 1;

    void Fire(u32 now, int dir) {
        on = true;
        at = now;
        direction = dir;
    }
    float At(u32 now, u32 duration, float split) const {
        if (!on || static_cast<s32>(now - at) < 0 || now - at >= duration) return 0.0f;
        const float p = static_cast<float>(now - at) / static_cast<float>(duration);
        return p < split ? EaseOut(p / split) : 1.0f - EaseOut((p - split) / (1.0f - split));
    }
    bool Active(u32 now, u32 duration) const { return on && now - at < duration; }
};
} // namespace gohan::menu::motion

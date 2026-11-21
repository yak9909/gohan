#include "game/native_input.hpp"
#include <algorithm>

namespace gohan::game {
void NativeTouch::Step(int px, int py, bool held) {
    previous = down; previous_changed = changed;
    previous_raw_y = raw_y;
    previous_x = x; previous_y = y;
    previous_start_x = start_x; previous_start_y = start_y;
    previous_held_frames = held_frames; previous_released_frames = released_frames;
    changed = down != held; down = held;
    if (held) {
        raw_x = std::clamp(px, 0, 319); raw_y = std::clamp(py, 0, 239);
        if (!previous) { start_x = raw_x; start_y = raw_y; held_frames = 0; }
        held_frames = std::min(held_frames + 1, 200u);
        x = raw_x; y = raw_y;
    } else if (previous) released_frames = 1;
    else released_frames = std::min(released_frames + 1, 200u);
}
bool NativeInputInjection::Begin(const NativeInputTargets &t, const NativeTouch &v,
                                 std::uint32_t trig, std::uint32_t repeat) {
    if (log_.Count() || !t.touch) return false;
    const auto put = [this](auto *p, auto value) {
        using Value = std::remove_pointer_t<decltype(p)>;
        return !p || log_.Write<Value>(p, static_cast<Value>(value));
    };
    constexpr std::uint32_t TouchMask = 0x8000;
    const auto mask = [](std::uint32_t old, std::uint32_t bits, bool on) {
        return (old & ~bits) | (on ? bits : 0u);
    };
    bool ok = log_.Write(t.touch, EncodeNativeTouch(v));
    if (t.pressed) ok = ok && put(t.pressed, mask(*t.pressed, TouchMask, v.down && !v.previous));
    if (t.released) ok = ok && put(t.released, mask(*t.released, TouchMask, !v.down && v.previous));
    if (t.pointer_flags) ok = ok && put(t.pointer_flags, mask(*t.pointer_flags, 1, v.down));
    if (t.held) ok = ok && put(t.held, mask(*t.held, TouchMask, v.down));
    ok = ok && put(t.x, float(v.x)) && put(t.y, float(v.y));
    ok = ok && put(t.menu_trig, trig) && put(t.menu_repeat, repeat);
    if (!ok) End();
    return ok;
}
} // namespace gohan::game

#include "game/input_filter.hpp"
#include <bit>

namespace gohan::game::input_filter {
namespace {
namespace part = core::input_block::part;
namespace group = core::input_block::group;
namespace limits = core::limits;
unsigned Count(Mask value) { return std::popcount(value); }

// スライドパッドを押していると見なすのは方向のビットだけ。アナログ値は中央でも 0 にならない
// ことがあるので、押しているかの判定には使わない。
Mask Parts(std::uint32_t held, bool pointer) {
    Mask result = 0;
    for (unsigned i = 0; i < 16; ++i)
        if (held & kHeldBits[i]) result |= 1u << i;
    if (pointer) result |= part::Touch;
    return result;
}
} // namespace

std::uint32_t HeldBits(Mask parts) {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 16; ++i)
        if (parts & (1u << i)) result |= kHeldBits[i];
    return result;
}

void Step(State &s, const Input &in, Output &out) {
    const Mask raw = Parts(in.held, in.pointer_on);
    const Mask previous_latch = s.latch;
    const Mask physical = raw | (in.select_down ? part::Select : 0);
    if (in.ctrpf_resumed) {
        // 止まっている間に押した物と、押し続けていた物を区別できない。離すまで渡さない。
        s.latch |= raw & (group::Buttons | part::Touch);
        s.pending = s.window = s.passing = 0;
        ++s.counts.resumes;
    }

    Mask block = in.demand.block | part::Select;
    if (in.select_down) {
        block |= group::Buttons;
        s.counts.select_protections += Count(raw & group::Buttons);
    }
    const Mask blocked = block | (s.latch & raw);
    // タッチだけは、遮断前から触れていた場合も離すまで渡さない。
    s.latch = (s.latch | (blocked & part::Touch)) & raw;
    s.counts.latch_entries += Count(s.latch & ~previous_latch);
    s.counts.hidden_presses += Count(s.latch & ~block);

    const Mask defer = in.demand.defer & group::Buttons & ~blocked;
    // 押し始めた時期によらず、直前も遮断されて押されていた区分は保持として戻す。
    const Mask hold_resume = s.previous_blocked & s.previous_raw & raw & ~blocked & ~defer & ~part::Touch;
    s.counts.hold_resumes += Count(hold_resume);
    // 要求を外した場合も、そのボタンの保留・窓・通過を捨てる。
    s.pending &= defer;
    s.window &= defer;
    s.passing &= defer;

    if (s.window && in.now_ms - s.window_ms >= limits::InputDeferWindowMs) s.window = 0;
    const Mask repress = s.window & raw;
    s.passing |= repress;
    s.window &= ~repress;
    s.counts.long_holds += Count(repress);
    s.passing &= raw;

    const Mask buttons = physical & (group::Buttons | part::Select);
    // 押し切るまで一度も他のボタンが混ざらなかった物だけを残す。
    if (buttons) s.pending &= buttons;
    if (Count(buttons) > 1) s.pending = 0;
    Mask released = s.pending & ~raw;
    if (buttons) released = 0;
    s.pending &= raw;
    const Mask pressed = raw & ~s.previous_raw & defer & ~s.passing;
    if (Count(buttons) == 1) s.pending |= pressed;
    if (released) {
        s.window = released;
        s.window_ms = in.now_ms;
        s.counts.deferred_presses += Count(released);
    }

    out.held = (in.held & ~HeldBits(blocked | defer)) | HeldBits(s.window | s.passing);
    // スライドパッドは区分で判定し、今回立っている方向のビットだけを旧状態にも足す。
    out.old_held = in.old_held | (in.held & HeldBits(hold_resume));
    out.pointer_on = in.pointer_on;
    out.old_pointer_on = in.old_pointer_on;
    out.slide_x = in.slide_x;
    out.slide_y = in.slide_y;
    out.blocked = blocked;
    if (kHideReleaseOnBlock) {
        s.counts.hidden_releases += Count(Parts(in.old_held, in.old_pointer_on) & blocked);
        out.old_held &= ~HeldBits(blocked);
        if (blocked & part::Touch) out.old_pointer_on = false;
    }
    if (blocked & part::Touch) out.pointer_on = false;
    if (blocked & part::SlidePad) out.slide_x = out.slide_y = 0;
    s.previous_raw = raw;
    s.previous_blocked = blocked;
    ++s.counts.calls;
}
} // namespace gohan::game::input_filter

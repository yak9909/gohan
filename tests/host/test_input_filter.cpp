#include "game/input_filter.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>

namespace filter = gohan::game::input_filter;
namespace block = gohan::core::input_block;
namespace part = block::part;
namespace group = block::group;
using block::Mask;
namespace {
// 期待値の変換表は実装の HeldBits を使わない。組み替えの対応を個別に検査する。
constexpr std::array<std::uint32_t, 16> bits = {
    128, 8192, 16384, 0, 65536, 131072, 262144, 524288,
    1, 2, 8, 16, 4096, 2048, 15728640, 32768,
};
std::uint32_t Encode(Mask parts) {
    std::uint32_t held = 0;
    for (unsigned n = 0; n < bits.size(); ++n) if (parts & (1u << n)) held |= bits[n];
    return held;
}
// 参照モデルは区分ごとの独立した真偽値と状態で書く。実装の集合演算は呼ばない。
struct Model {
    struct Button { bool down = false, latched = false, candidate = false, passing = false; };
    std::array<Button, 16> button{};
    int window = -1;
    std::uint32_t window_start = 0, previous_time = 0;
    unsigned remaining = 0;
    bool started = false;

    filter::Output Step(const filter::Input &in) {
        std::array<bool, 16> down{}, blocked{}, deferred{};
        for (unsigned n = 0; n < bits.size(); ++n) down[n] = (in.held & bits[n]) != 0;
        // スライドパッドを押しているかは方向のビットだけで決める。アナログ値は見ない。
        down[15] = down[15] || in.pointer_on;
        const bool resume = in.ctrpf_resumed || (started && remaining && in.now_ms - previous_time >= 100);
        if (resume) {
            for (unsigned n = 0; n < bits.size(); ++n) {
                if (n != 12 && n != 14 && down[n]) button[n].latched = true;
                button[n].candidate = button[n].passing = false;
            }
            window = -1;
            remaining = 0;
        } else if (in.select_down) remaining = 30;
        else if (remaining) --remaining;

        for (unsigned n = 0; n < bits.size(); ++n) {
            const bool is_button = (group::Buttons & (1u << n)) != 0;
            blocked[n] = n == 12 || (in.demand.block & (1u << n)) ||
                (in.select_down && is_button) || (button[n].latched && down[n]);
            button[n].latched = blocked[n] && down[n];
            deferred[n] = is_button && (in.demand.defer & (1u << n)) && !blocked[n];
            if (!deferred[n]) {
                button[n].candidate = button[n].passing = false;
                if (window == static_cast<int>(n)) window = -1;
            }
        }
        if (window >= 0 && in.now_ms - window_start >= 300) window = -1;
        if (window >= 0 && down[window]) {
            button[window].passing = true;
            window = -1;
        }
        int released = -1;
        for (unsigned n = 0; n < bits.size(); ++n) {
            auto &b = button[n];
            if (!down[n]) b.passing = false;
            bool other = in.select_down && n != 12;
            for (unsigned m = 0; m < bits.size(); ++m)
                if (m != n && m != 14 && m != 15 && down[m]) other = true;
            if (other) b.candidate = false;
            if (b.candidate && !down[n]) {
                released = static_cast<int>(n);
                b.candidate = false;
            }
            if (deferred[n] && down[n] && !b.down && !b.passing && !other) b.candidate = true;
        }
        if (released >= 0) { window = released; window_start = in.now_ms; }

        filter::Output out{};
        out.held = in.held;
        out.old_held = in.old_held;
        out.pointer_on = in.pointer_on;
        out.old_pointer_on = in.old_pointer_on;
        out.slide_x = in.slide_x; out.slide_y = in.slide_y;
        for (unsigned n = 0; n < bits.size(); ++n) {
            if (blocked[n]) {
                out.blocked |= 1u << n;
                out.held &= ~bits[n];
                if (filter::kHideReleaseOnBlock) out.old_held &= ~bits[n];
            } else if (deferred[n]) {
                if (window == static_cast<int>(n) || button[n].passing) out.held |= bits[n];
                else out.held &= ~bits[n];
            }
            button[n].down = down[n];
        }
        if (blocked[15]) {
            out.pointer_on = false;
            if (filter::kHideReleaseOnBlock) out.old_pointer_on = false;
        }
        if (blocked[14]) out.slide_x = out.slide_y = 0;
        previous_time = in.now_ms; started = true;
        return out;
    }
};

bool trace = false;
struct Rig {
    filter::State state;
    Model model;
    filter::Output last{};
    std::uint32_t now = 0, frame = 0;
    const char *name;
    explicit Rig(const char *name) : name(name) {}
    filter::Output Tick(filter::Input in) {
        in.old_held = last.held;
        in.old_pointer_on = last.pointer_on;
        const auto expected = model.Step(in);
        filter::Output actual{};
        filter::Step(state, in, actual);
        ++frame;
        if (trace) std::printf("%s %u %u 生=%08X 要求=%04X 保持=%08X 押=%08X 離=%08X\n",
            name, frame, in.now_ms, in.held, in.demand.block, actual.held,
            actual.held & ~actual.old_held, actual.old_held & ~actual.held);
        assert(actual.held == expected.held && actual.old_held == expected.old_held);
        assert(actual.pointer_on == expected.pointer_on && actual.old_pointer_on == expected.old_pointer_on);
        assert(actual.slide_x == expected.slide_x && actual.slide_y == expected.slide_y);
        assert(actual.blocked == expected.blocked);
        Mask pending = 0, passing = 0, latch = 0;
        for (unsigned n = 0; n < 16; ++n) {
            if (model.button[n].candidate) pending |= 1u << n;
            if (model.button[n].passing) passing |= 1u << n;
            if (model.button[n].latched) latch |= 1u << n;
        }
        assert(state.pending == pending && state.passing == passing && state.latch == latch);
        assert(state.window == (model.window < 0 ? 0 : 1u << model.window));
        assert(state.suspend_calls == model.remaining && state.counts.calls == frame);
        // 本体と、方向・A/B を組み替える Wrapper の両方を同じ旧状態から計算する。
        const auto changed = [](std::uint32_t current, std::uint32_t old) {
            return std::array<std::uint32_t, 2>{current & ~old, old & ~current};
        };
        assert(changed(actual.held, actual.old_held) == changed(expected.held, expected.old_held));
        auto wrapper = [](std::uint32_t held) {
            return ((held & 1) ? 2u : 0u) | ((held & 2) ? 1u : 0u) |
                ((held & 65536) ? 16u : 0u) | ((held & 32768) ? 32768u : 0u);
        };
        assert(changed(wrapper(actual.held), wrapper(actual.old_held)) ==
               changed(wrapper(expected.held), wrapper(expected.old_held)));
        if (filter::kHideReleaseOnBlock) {
            const auto edges = changed(actual.held, actual.old_held);
            assert(!((actual.held | edges[0] | edges[1]) & Encode(actual.blocked)));
        }
        const auto untouched = ~Encode(actual.blocked | in.demand.defer);
        assert((actual.held & untouched) == (in.held & untouched));
        if (in.select_down) assert(!(actual.held & Encode(group::Buttons)));
        last = expected;
        return actual;
    }
    filter::Output Expect(Mask raw, block::Claim demand, Mask want, unsigned dt = 16,
                          bool select = false, bool resumed = false) {
        filter::Input in{};
        in.held = Encode(raw);
        in.pointer_on = (raw & part::Touch) != 0;
        if (raw & part::SlidePad) { in.slide_x = 0.5f; in.slide_y = -0.25f; }
        in.demand = {demand.block, demand.defer, 0};
        in.select_down = select; in.ctrpf_resumed = resumed;
        now += dt; in.now_ms = now;
        const auto out = Tick(in);
        assert(out.held == Encode(want));
        return out;
    }
};

void Sequences() {
    static_assert(filter::kHideReleaseOnBlock && filter::kDefaultDefer == part::Start);
    for (unsigned n = 0; n < 16; ++n) assert(filter::HeldBits(1u << n) == bits[n]);
    assert(filter::HeldBits(part::Select) == 4096 && filter::HeldBits(part::Start) == 2048);
    const block::Claim menu{group::Buttons, 0}, defer{0, part::Start};
    Rig hold("押したまま開閉");
    hold.Expect(0, {}, 0); hold.Expect(part::A, {}, part::A); hold.Expect(part::A, {}, part::A);
    auto out = hold.Expect(part::A, menu, 0); assert(out.old_held == 0);
    hold.Expect(part::A, menu, 0); hold.Expect(part::A, {}, 0); hold.Expect(part::A, {}, 0);
    hold.Expect(0, {}, 0); hold.Expect(part::A, {}, part::A);
    assert(hold.state.counts.hidden_releases == 1 && hold.state.counts.latch_entries == 1);
    Rig during("遮断中の押下");
    during.Expect(0, menu, 0); during.Expect(part::B, menu, 0);
    during.Expect(part::B, {}, 0); during.Expect(0, {}, 0); during.Expect(part::B, {}, part::B);

    Rig tap("START単押しと300ms境界");
    tap.Expect(part::Start, defer, 0); tap.Expect(part::Start, defer, 0);
    tap.Expect(0, defer, part::Start); tap.Expect(0, defer, part::Start, 299);
    out = tap.Expect(0, defer, 0, 1); assert(out.old_held == Encode(part::Start));
    assert(tap.state.counts.deferred_presses == 1);
    Rig long_hold("押し直して長押し");
    long_hold.Expect(part::Start, defer, 0); long_hold.Expect(0, defer, part::Start);
    long_hold.Expect(part::Start, defer, part::Start, 299);
    long_hold.Expect(part::Start, defer, part::Start, 500);
    long_hold.Expect(0, defer, 0); assert(long_hold.state.counts.long_holds == 1);
    for (bool reverse : {false, true}) {
        Rig mixed(reverse ? "十字→START" : "START→十字");
        mixed.Expect(reverse ? part::Up : part::Start, defer, reverse ? part::Up : 0);
        mixed.Expect(part::Up | part::Start, defer, part::Up);
        mixed.Expect(part::Start, defer, 0); mixed.Expect(0, defer, 0);
        assert(mixed.state.counts.deferred_presses == 0);
    }
    Rig overlapping("窓の置き換え");
    const block::Claim two{0, part::A | part::Start};
    overlapping.Expect(part::A, two, 0); overlapping.Expect(0, two, part::A);
    overlapping.Expect(part::Start, two, part::A);
    overlapping.Expect(0, two, part::Start);
    overlapping.Expect(0, two, 0, 300);
    Rig cancelled("遮断で保留と窓と通過を破棄");
    cancelled.Expect(part::Start, defer, 0);
    cancelled.Expect(part::Start, {group::Buttons, part::Start}, 0);
    cancelled.Expect(0, defer, 0);
    cancelled.Expect(part::Start, defer, 0); cancelled.Expect(0, defer, part::Start);
    cancelled.Expect(0, {group::Buttons, part::Start}, 0);
    cancelled.Expect(0, defer, 0);
    cancelled.Expect(part::Start, defer, 0); cancelled.Expect(0, defer, part::Start);
    cancelled.Expect(part::Start, defer, part::Start);
    cancelled.Expect(part::Start, {group::Buttons, part::Start}, 0);
    cancelled.Expect(part::Start, defer, 0); cancelled.Expect(0, defer, 0);

    for (bool reverse : {false, true}) {
        Rig select(reverse ? "十字→SELECT" : "SELECT→十字");
        select.Expect(reverse ? part::Up : 0, {}, reverse ? part::Up : 0, 16, !reverse);
        select.Expect(part::Up | part::Start, defer, 0, 16, true);
        select.Expect(part::Up | part::Start, defer, 0);
        select.Expect(0, defer, 0); select.Expect(part::Start, defer, 0);
        select.Expect(0, defer, part::Start);
    }
    Rig touch("タッチとスライドパッドの掛かり");
    const Mask both = part::Touch | part::SlidePad;
    touch.Expect(both, {}, both); touch.Expect(both, {both, 0}, 0);
    touch.Expect(both, {}, 0); touch.Expect(0, {}, 0); touch.Expect(both, {}, both);
    // 中央でアナログ値が小さく残っても、方向のビットが無ければ押していないので掛かりが解ける。
    Rig residue("アナログ値が残るスライドパッドの掛かり");
    const auto slide = [&](std::uint32_t held, float analog, block::Mask block_parts, bool expect_held) {
        filter::Input in{};
        in.held = held;
        in.slide_x = analog; in.slide_y = -analog;
        in.demand = {block_parts, 0, 0};
        residue.now += 16; in.now_ms = residue.now;
        const auto out = residue.Tick(in);
        assert(((out.held & 15728640) != 0) == expect_held);
        return out;
    };
    slide(15728640, 0.5f, 0, true);
    auto edge = slide(15728640, 0.5f, part::SlidePad, false);
    assert(edge.slide_x == 0 && edge.slide_y == 0 && (edge.blocked & part::SlidePad));
    slide(15728640, 0.5f, 0, false);
    // 方向のビットが無く、アナログ値だけが残る。掛かりは解け、アナログ値は通る。
    edge = slide(0, 0.03f, 0, false);
    assert(!(edge.blocked & part::SlidePad) && edge.slide_x == 0.03f && edge.slide_y == -0.03f);
    assert(residue.state.latch == 0);
    slide(15728640, 0.5f, 0, true);
    Rig pass("メニュー中もタッチとスライドパッドを通す");
    pass.Expect(both | part::A | part::Start, menu, both);
    Rig ignored("押し切りの混ざりにタッチとスライドパッドを数えない");
    ignored.Expect(both | part::Start, defer, both); ignored.Expect(both, defer, both | part::Start);
    Rig simultaneous("押し切りの同時押しを取り消す");
    simultaneous.Expect(part::A | part::Start, two, 0); simultaneous.Expect(0, two, 0);
    Rig removed("押し切りの設定を外すと保留を破棄");
    removed.Expect(part::Start, defer, 0); removed.Expect(part::Start, {}, part::Start);
    removed.Expect(0, defer, 0);
    Rig fallback("Pが無い経路のSELECT"); fallback.Expect(part::Select | part::Start, {}, part::Start);
}

void Suspend() {
    const block::Claim defer{0, part::Start};
    Rig ctrpf("CTRPFの明示通知・短い中断・SELECT無し");
    ctrpf.Expect(part::Start, defer, 0); ctrpf.Expect(0, defer, part::Start);
    ctrpf.Expect(part::B | part::Touch | part::SlidePad, defer, part::SlidePad, 1, false, true);
    ctrpf.Expect(part::B | part::Touch, defer, 0); ctrpf.Expect(0, defer, 0);
    ctrpf.Expect(part::B | part::Touch, defer, part::B | part::Touch);
    assert(ctrpf.state.counts.suspensions == 1 && ctrpf.state.counts.latch_entries == 2);
    Rig rosalina("Rosalinaの時間による推測");
    rosalina.Expect(0, {}, 0, 16, true);
    rosalina.Expect(part::B | part::Touch | part::SlidePad, {}, part::SlidePad, 100);
    rosalina.Expect(part::B, {}, 0); rosalina.Expect(0, {}, 0); rosalina.Expect(part::B, {}, part::B);
    assert(rosalina.state.counts.suspensions == 1);
    Rig normal("通常の長い読み込み");
    normal.Expect(part::B, {}, part::B); normal.Expect(part::B | part::Touch, {}, part::B | part::Touch, 1000);
    // 長い周期の末尾で初めて SELECT を見ても、Rosalina の中断とは判断しない。
    normal.Expect(part::Touch, {}, part::Touch, 1000, true);
    assert(normal.state.counts.suspensions == 0);
    Rig timeout("待ち受けの30回境界");
    timeout.Expect(0, {}, 0, 16, true);
    for (unsigned n = 0; n < 30; ++n) timeout.Expect(0, {}, 0, 1);
    timeout.Expect(part::B, {}, part::B, 100);
    assert(timeout.state.counts.suspensions == 0);
    Rig last("待ち受けの最後の呼び出し");
    last.Expect(0, {}, 0, 16, true);
    for (unsigned n = 0; n < 29; ++n) last.Expect(0, {}, 0, 1);
    last.Expect(part::B, {}, 0, 100); assert(last.state.counts.suspensions == 1);
    Rig short_gap("99msでは中断とみなさない");
    short_gap.Expect(0, {}, 0, 16, true); short_gap.Expect(part::B, {}, part::B, 99);
    Rig wrap("時計の周回"); wrap.now = 0xFFFFFFC0u;
    wrap.Expect(part::Start, defer, 0); wrap.Expect(0, defer, part::Start);
    wrap.Expect(0, defer, part::Start, 299); wrap.Expect(0, defer, 0, 1);
    Rig wrap_suspend("時計をまたぐ中断"); wrap_suspend.now = 0xFFFFFFE0u;
    wrap_suspend.Expect(0, {}, 0, 16, true); wrap_suspend.Expect(part::B, {}, 0, 100);
}

void Random() {
    // 固定種で再現可能。低頻度の変化で長押し・窓・掛かりも続く列にする。
    std::mt19937 random(360022);
    for (unsigned series = 0; series < 300; ++series) {
        Rig rig("乱数");
        filter::Input in{};
        rig.now = random();
        for (unsigned frame = 0; frame < 400; ++frame) {
            if (random() % 3 == 0) in.held ^= bits[random() % 14];
            if (random() % 9 == 0) in.demand.block ^= 1u << (random() % 16);
            if (random() % 13 == 0) in.demand.defer ^= (1u << (random() % 14)) & group::Buttons;
            if (random() % 11 == 0) in.select_down = !in.select_down;
            if (random() % 7 == 0) in.pointer_on = !in.pointer_on;
            if (random() % 5 == 0) in.held ^= 32768;
            if (random() % 7 == 0) {
                in.slide_x = (static_cast<int>(random() % 3) - 1) * 0.5f;
                in.slide_y = (static_cast<int>(random() % 3) - 1) * 0.25f;
                in.held ^= 1u << (20 + random() % 4);
            }
            // 触らないビットは上下とも残す。
            in.held |= 0x20000764u;
            in.ctrpf_resumed = random() % 97 == 0;
            rig.now += random() % 17 == 0 ? 100 + random() % 301 : 1 + random() % 34;
            in.now_ms = rig.now;
            const auto out = rig.Tick(in);
            if (out.blocked & part::Touch)
                assert(!out.pointer_on && !out.old_pointer_on && !(out.held & 32768) && !(out.old_held & 32768));
            if (out.blocked & part::SlidePad)
                assert(!out.slide_x && !out.slide_y && !(out.held & 15728640));
            assert((out.held & 0x20000764u) == 0x20000764u);
        }
    }
    std::puts("参照モデルとの比較・不変条件: 300 系列 × 400 フレーム成功（固定種 360022）");
}
}
int main(int argc, char **argv) {
    trace = argc == 2 && std::strcmp(argv[1], "--trace") == 0;
    if (trace) std::puts("列名 / フレーム / 時刻(ms) / 生の保持 / 遮断要求 / ゲームの保持 / 押した / 離した");
    Sequences(); Suspend();
    trace = false;
    Random();
    std::puts("入力の遮断の規則: 成功");
}

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
    // latched: 遮断の間に押し始めた。carried: 遮断の前から押し続けていた。blocked: 前回の遮断。
    struct Button { bool down = false, blocked = false, latched = false, carried = false, candidate = false, passing = false; };
    std::array<Button, 16> button{};
    int window = -1;
    std::uint32_t window_start = 0;

    filter::Output Step(const filter::Input &in) {
        std::array<bool, 16> down{}, blocked{}, deferred{};
        for (unsigned n = 0; n < bits.size(); ++n) down[n] = (in.held & bits[n]) != 0;
        // スライドパッドを押しているかは方向のビットだけで決める。アナログ値は見ない。
        down[15] = down[15] || in.pointer_on;
        if (in.ctrpf_resumed) {
            for (unsigned n = 0; n < bits.size(); ++n) {
                if (n != 12 && n != 14 && down[n]) { button[n].latched = true; button[n].carried = false; }
                button[n].candidate = button[n].passing = false;
            }
            window = -1;
        }

        for (unsigned n = 0; n < bits.size(); ++n) {
            const bool is_button = (group::Buttons & (1u << n)) != 0;
            blocked[n] = n == 12 || (in.demand.block & (1u << n)) ||
                (in.select_down && is_button) || (button[n].latched && down[n]);
            if (blocked[n] && down[n]) {
                // 前のフレームでゲームに見えていた押下の続きか、遮断の間に押し始めたか。
                const bool seen = button[n].down && !button[n].blocked;
                const bool carried = !button[n].latched && (button[n].carried || seen);
                button[n].carried = carried;
                button[n].latched = !carried;
            } else button[n].latched = button[n].carried = false;
            button[n].blocked = blocked[n];
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
    // 履歴だけから導く答え。fresh は、ゲームに見えていた押下が途切れず続いている区分。
    std::array<bool, 16> fresh{}, was_held_blocked{};
    explicit Rig(const char *name) : name(name) {}
    filter::Output Tick(filter::Input in) {
        in.old_held = last.held;
        in.old_pointer_on = last.pointer_on;
        const auto expected = model.Step(in);
        filter::Output actual{};
        filter::Step(state, in, actual);
        ++frame;
        Mask pending = 0, passing = 0, latch = 0, carry = 0;
        for (unsigned n = 0; n < 16; ++n) {
            if (model.button[n].candidate) pending |= 1u << n;
            if (model.button[n].passing) passing |= 1u << n;
            if (model.button[n].latched) latch |= 1u << n;
            if (model.button[n].carried) carry |= 1u << n;
        }
        if (trace) std::printf("%s %u %u 生=%08X 要求=%04X 保持=%08X 押=%08X 離=%08X 掛かり=%04X 持越し=%04X\n",
            name, frame, in.now_ms, in.held, in.demand.block, actual.held,
            actual.held & ~actual.old_held, actual.old_held & ~actual.held, state.latch, state.carry);
        assert(actual.held == expected.held && actual.old_held == expected.old_held);
        assert(actual.pointer_on == expected.pointer_on && actual.old_pointer_on == expected.old_pointer_on);
        assert(actual.slide_x == expected.slide_x && actual.slide_y == expected.slide_y);
        assert(actual.blocked == expected.blocked);
        assert(state.pending == pending && state.passing == passing && state.latch == latch && state.carry == carry);
        assert(!(state.latch & state.carry));
        assert(state.window == (model.window < 0 ? 0 : 1u << model.window));
        assert(state.counts.calls == frame);
        // 遮断が終わった瞬間に、まだ押されている区分は、遮断の前から切れ目なく押されていた時だけ
        // 押下としてゲームへ戻る。遮断の間に押し始めた物、止まっている間に押した物は戻らない。
        for (unsigned n = 0; n < 16; ++n) {
            const bool pressed = (in.held & bits[n]) != 0 || (n == 15 && in.pointer_on);
            const bool is_blocked = (actual.blocked & (1u << n)) != 0;
            const bool deferred = (group::Buttons & in.demand.defer & (1u << n)) != 0;
            const bool resumed_latch = in.ctrpf_resumed && n != 12 && n != 14;
            if (pressed && !is_blocked && was_held_blocked[n] && n != 3 && n != 12 && !deferred) {
                const bool delivered = n == 15 ? (actual.pointer_on || (actual.held & bits[n]))
                                               : (actual.held & bits[n]) != 0;
                assert(delivered == fresh[n]);
            }
            if (!pressed || resumed_latch) fresh[n] = false;
            else if (!is_blocked) fresh[n] = true;
            was_held_blocked[n] = pressed && is_blocked;
        }
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
    // ゲームが見る押下・離し（本体の式: 押した = 新 & ~旧、離した = 旧 & ~新）。
    const auto pressed = [](const filter::Output &o) { return o.held & ~o.old_held; };
    const auto released = [](const filter::Output &o) { return o.old_held & ~o.held; };
    Rig hold("押したまま開閉");
    hold.Expect(0, {}, 0); hold.Expect(part::A, {}, part::A); hold.Expect(part::A, {}, part::A);
    // 開始の瞬間は離しも押下も出ない。遮断の間は何も出ない。
    auto out = hold.Expect(part::A, menu, 0); assert(out.old_held == 0 && !pressed(out) && !released(out));
    out = hold.Expect(part::A, menu, 0); assert(!pressed(out) && !released(out));
    // 遮断の前から押し続けていた A は、遮断が終わった瞬間に押下として戻る。
    out = hold.Expect(part::A, {}, part::A); assert(pressed(out) == Encode(part::A) && !released(out));
    out = hold.Expect(part::A, {}, part::A); assert(!pressed(out) && !released(out));
    hold.Expect(0, {}, 0); hold.Expect(part::A, {}, part::A);
    assert(hold.state.counts.hidden_releases == 1 && hold.state.counts.latch_entries == 0);
    assert(hold.state.counts.carry_entries == 1 && hold.state.counts.carry_resumes == 1);
    Rig during("遮断中の押下");
    during.Expect(0, menu, 0); during.Expect(part::B, menu, 0);
    during.Expect(part::B, {}, 0); during.Expect(0, {}, 0); during.Expect(part::B, {}, part::B);
    assert(during.state.counts.latch_entries == 1 && during.state.counts.carry_entries == 0);
    Rig mixed_hold("遮断の前の A と遮断の間の B");
    mixed_hold.Expect(part::A, {}, part::A);
    mixed_hold.Expect(part::A, menu, 0); mixed_hold.Expect(part::A | part::B, menu, 0);
    out = mixed_hold.Expect(part::A | part::B, {}, part::A);
    assert(pressed(out) == Encode(part::A));
    mixed_hold.Expect(part::A | part::B, {}, part::A); mixed_hold.Expect(part::A, {}, part::A);
    mixed_hold.Expect(0, {}, 0); mixed_hold.Expect(part::B, {}, part::B);
    Rig released_inside("遮断の間に離した物は戻らない");
    released_inside.Expect(part::A, {}, part::A); released_inside.Expect(part::A, menu, 0);
    released_inside.Expect(0, menu, 0); released_inside.Expect(0, {}, 0);
    released_inside.Expect(part::A, {}, part::A);
    assert(released_inside.state.counts.carry_resumes == 0);
    Rig repressed("遮断の間に押し直した物は掛かり");
    repressed.Expect(part::A, {}, part::A); repressed.Expect(part::A, menu, 0);
    repressed.Expect(0, menu, 0); repressed.Expect(part::A, menu, 0);
    repressed.Expect(part::A, {}, 0); repressed.Expect(part::A, {}, 0);
    repressed.Expect(0, {}, 0); repressed.Expect(part::A, {}, part::A);
    assert(repressed.state.counts.latch_entries == 1 && repressed.state.counts.carry_resumes == 0);
    Rig again("戻った直後の遮断も持ち越し");
    again.Expect(part::A, {}, part::A); again.Expect(part::A, menu, 0); again.Expect(part::A, {}, part::A);
    again.Expect(part::A, menu, 0); again.Expect(part::A, {}, part::A);
    assert(again.state.counts.carry_entries == 2 && again.state.counts.carry_resumes == 2);
    Rig chained("SELECT の保護から要求の遮断へ続く");
    chained.Expect(part::A, {}, part::A); chained.Expect(part::A, {}, 0, 16, true);
    chained.Expect(part::A, menu, 0); chained.Expect(part::A, menu, 0);
    chained.Expect(part::A, {}, part::A);
    Rig changed_block("要求の区分が変わっても持ち越しは続く");
    changed_block.Expect(part::A | part::Up, {}, part::A | part::Up);
    changed_block.Expect(part::A | part::Up, {part::A, 0}, part::Up);
    changed_block.Expect(part::A | part::Up, {part::Up, 0}, part::A);
    changed_block.Expect(part::A | part::Up, {part::A | part::Up, 0}, 0);
    changed_block.Expect(part::A | part::Up, {}, part::A | part::Up);

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
        // SELECT を離した時、先に押していた十字上は戻り、SELECT と一緒に押した START は掛かりのまま。
        select.Expect(part::Up | part::Start, defer, reverse ? part::Up : 0);
        select.Expect(0, defer, 0); select.Expect(part::Start, defer, 0);
        select.Expect(0, defer, part::Start);
    }
    Rig touch("タッチとスライドパッドの掛かり");
    const Mask both = part::Touch | part::SlidePad;
    // 遮断の間に触れ始めた指とスライドパッドは、遮断が終わっても離すまで渡さない。
    touch.Expect(0, {both, 0}, 0); touch.Expect(both, {both, 0}, 0);
    touch.Expect(both, {}, 0); touch.Expect(0, {}, 0); touch.Expect(both, {}, both);
    // 遮断の前から触れていた指とスライドパッドは、遮断が終わると戻る。
    Rig touch_carry("遮断の前から触れていたタッチとスライドパッド");
    touch_carry.Expect(both, {}, both); touch_carry.Expect(both, {both, 0}, 0);
    auto back = touch_carry.Expect(both, {}, both);
    assert(back.pointer_on && !back.old_pointer_on);
    touch_carry.Expect(0, {}, 0);
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
    slide(15728640, 0.5f, 0, true); // 遮断の前から倒していたので、遮断が終われば戻る。
    slide(0, 0.0f, part::SlidePad, false);
    slide(15728640, 0.5f, part::SlidePad, false);
    slide(15728640, 0.5f, 0, false); // 遮断の間に倒し始めたので、離すまで渡さない。
    // 方向のビットが無く、アナログ値だけが残る。掛かりは解け、アナログ値は通る。
    edge = slide(0, 0.03f, 0, false);
    assert(!(edge.blocked & part::SlidePad) && edge.slide_x == 0.03f && edge.slide_y == -0.03f);
    assert(residue.state.latch == 0 && residue.state.carry == 0);
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

void Resume() {
    const block::Claim defer{0, part::Start}, menu{group::Buttons, 0};
    // CTRPF の再開（明示の知らせ）。止まっている間に押した物と、押し続けていた物は区別できないので、
    // 押されているボタン・十字・START・タッチは、遮断が無くても離すまで渡さない。スライドパッドは対象にしない。
    Rig ctrpf("CTRPFの明示通知・短い再開・SELECT無し");
    ctrpf.Expect(part::Start, defer, 0); ctrpf.Expect(0, defer, part::Start);
    ctrpf.Expect(part::B | part::Touch | part::SlidePad, defer, part::SlidePad, 1, false, true);
    ctrpf.Expect(part::B | part::Touch, defer, 0); ctrpf.Expect(0, defer, 0);
    ctrpf.Expect(part::B | part::Touch, defer, part::B | part::Touch);
    assert(ctrpf.state.counts.resumes == 1 && ctrpf.state.counts.latch_entries == 2);
    assert(ctrpf.state.counts.carry_entries == 0);
    Rig kept("CTRPFを挟んで押し続けた物は戻さない");
    kept.Expect(part::B, {}, part::B); kept.Expect(part::B, {}, 0, 1, false, true);
    kept.Expect(part::B, {}, 0); kept.Expect(0, {}, 0); kept.Expect(part::B, {}, part::B);
    Rig in_menu("持ち越し中のCTRPFの再開");
    in_menu.Expect(part::B, {}, part::B); in_menu.Expect(part::B, menu, 0);
    in_menu.Expect(part::B, menu, 0, 1, false, true);
    in_menu.Expect(part::B, {}, 0); in_menu.Expect(0, {}, 0); in_menu.Expect(part::B, {}, part::B);
    assert(in_menu.state.counts.carry_entries == 1 && in_menu.state.counts.carry_resumes == 0);
    assert(in_menu.state.counts.latch_entries == 1);
    Rig released("CTRPFの停止中に離した物は掛からない");
    released.Expect(part::B, {}, part::B); released.Expect(0, {}, 0, 1, false, true);
    released.Expect(part::B, {}, part::B); released.Expect(0, {}, 0);
    // 十字上を押したまま SELECT で CTRPF を開き、SELECT で閉じる。十字上は再開の時点で押されたままなので、
    // 離すまで渡さない（持ち越しにしない）。
    Rig select_close("CTRPFをSELECTで閉じる");
    select_close.Expect(part::Up, {}, part::Up);
    select_close.Expect(part::Up, {}, 0, 16, true);
    select_close.Expect(part::Up, {}, 0, 1, true, true);
    select_close.Expect(part::Up, {}, 0);
    select_close.Expect(0, {}, 0); select_close.Expect(part::Up, {}, part::Up);
    assert(select_close.state.counts.carry_entries == 1 && select_close.state.counts.carry_resumes == 0);
    assert(select_close.state.counts.latch_entries == 1 && select_close.state.counts.resumes == 1);
    // Rosalina の画面から戻った時の特別な扱いは無い。時間が空いても、SELECT の後でも、普通に渡す。
    Rig gap("時間が空いても特別に扱わない");
    gap.Expect(0, {}, 0, 16, true);
    gap.Expect(part::B | part::Touch | part::SlidePad, {}, part::B | part::Touch | part::SlidePad, 100);
    gap.Expect(part::B, {}, part::B, 1000);
    assert(gap.state.counts.resumes == 0 && gap.state.counts.latch_entries == 0);
    Rig wrap("時計の周回"); wrap.now = 0xFFFFFFC0u;
    wrap.Expect(part::Start, defer, 0); wrap.Expect(0, defer, part::Start);
    wrap.Expect(0, defer, part::Start, 299); wrap.Expect(0, defer, 0, 1);
}

void Random() {
    // 固定種で再現可能。低頻度の変化で長押し・窓・掛かりも続く列にする。
    std::mt19937 random(360022);
    std::uint64_t carry_entries = 0, carry_resumes = 0, latch_entries = 0, resumes = 0;
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
        const auto &n = rig.state.counts;
        carry_entries += n.carry_entries; carry_resumes += n.carry_resumes;
        latch_entries += n.latch_entries; resumes += n.resumes;
    }
    // 乱数の列が、持ち越し・戻り・掛かり・CTRPF の再開を十分に通ること。
    assert(carry_entries > 1000 && carry_resumes > 300 && latch_entries > 1000 && resumes > 1000);
    std::printf("参照モデルとの比較・不変条件: 300 系列 × 400 フレーム成功（固定種 360022）。"
                "持ち越し %llu・戻り %llu・掛かり %llu・CTRPF 再開 %llu\n",
                static_cast<unsigned long long>(carry_entries), static_cast<unsigned long long>(carry_resumes),
                static_cast<unsigned long long>(latch_entries), static_cast<unsigned long long>(resumes));
}
}
int main(int argc, char **argv) {
    trace = argc == 2 && std::strcmp(argv[1], "--trace") == 0;
    if (trace) std::puts("列名 / フレーム / 時刻(ms) / 生の保持 / 遮断要求 / ゲームの保持 / 押した / 離した / 掛かり / 持越し");
    Sequences(); Resume();
    trace = false;
    Random();
    std::puts("入力の遮断の規則: 成功");
}

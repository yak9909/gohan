#pragma once
#include "core/input_block.hpp"
#include <cstdint>

// ゲームのメモリを読まず、保持の列と旧状態から次の出力だけを計算する。
// ゲームの構造体とは独立した値。フックとの接続は別の段で行う。
namespace gohan::game::input_filter {
using core::input_block::Mask;
inline constexpr bool kHideReleaseOnBlock = true;
inline constexpr Mask kDefaultDefer = core::input_block::part::Start;

// ゲームの保持の並び。対応の無い区分（ZR）は 0。
inline constexpr std::uint32_t kHeldBits[] = {
    0x80, 0x2000, 0x4000, 0, 0x10000, 0x20000, 0x40000, 0x80000,
    1, 2, 8, 0x10, 0x1000, 0x800, 0xF00000, 0x8000,
};
std::uint32_t HeldBits(Mask parts);

struct Input {
    std::uint32_t held = 0;
    std::uint32_t old_held = 0;
    bool pointer_on = false;
    bool old_pointer_on = false;
    float slide_x = 0, slide_y = 0;
    core::input_block::Effective demand{};
    bool select_down = false;
    // 開く通知と停止前の競合の処理を終えた、再開後だけの知らせ。
    bool ctrpf_resumed = false;
    std::uint32_t now_ms = 0;
};
struct Output {
    std::uint32_t held = 0, old_held = 0;
    bool pointer_on = false, old_pointer_on = false;
    float slide_x = 0, slide_y = 0;
    Mask blocked = 0;
};
struct Counters {
    std::uint32_t calls = 0;
    // 区分単位の出来事の数。hidden_presses は掛かりで隠した区分・呼び出しの累計。
    // select_protections も保護した区分・呼び出しの累計。中断は検出回数。
    std::uint32_t hidden_releases = 0, hidden_presses = 0, latch_entries = 0;
    std::uint32_t select_protections = 0, suspensions = 0, deferred_presses = 0, long_holds = 0;
};
struct State {
    Mask latch = 0, pending = 0, window = 0, passing = 0;
    Mask previous_raw = 0;
    std::uint32_t window_ms = 0, previous_ms = 0;
    unsigned suspend_calls = 0;
    bool initialized = false;
    Counters counts{};
};

void Step(State &state, const Input &input, Output &output);
} // namespace gohan::game::input_filter

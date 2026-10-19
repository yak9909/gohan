#include "core/format.hpp"
#include "menu/engine.hpp"
#include "menu/value_text.hpp"

// 行の表示。OFF／待機／ON の 3 つと、実行の項目の 実行／予約／OK を決める。
namespace gohan::menu {
namespace {
unsigned PopCount(u32 mask) {
    unsigned n = 0;
    for (; mask; mask >>= 1) n += mask & 1u;
    return n;
}
} // namespace

RowView Engine::View(u16 item) const {
    RowView view;
    if (!Has(item)) return view;
    const ItemDef &d = defs_[item];
    const ItemState &st = state_[item];
    view.def = &d;
    view.button = d.control == Control::Action;
    view.disabled = Disabled(item);
    view.pending = Dirty(item);
    view.locked = st.fixed;
    core::TextWriter w(view.value, sizeof(view.value));
    const bool editing = HasValue(item) && st.value != st.applied;
    switch (d.control) {
    case Control::Folder: break;
    case Control::Toggle: {
        if (editing || st.applied == 0) {
            w.Append(st.value ? "ON" : "OFF"); // 編集中は編集値を見せる（適用待ちは黄色）
        } else if (d.behavior == Behavior::State) {
            // 待機: 項目は ON でホットキーを待っている。効果が入っていれば ON。
            w.Append(EffectOn(item) ? "ON" : st.applied_hotkey != 0 ? "待機" : "OFF");
        } else if (d.behavior == Behavior::Hold) {
            w.Append(st.hold_active ? "動作中" : "待機");
        } else {
            w.Append(st.applied_hotkey != 0 ? "待機" : "ON");
        }
        break;
    }
    case Control::Action:
        if (d.behavior == Behavior::Mode) w.Append(st.mode_active ? "起動中" : "開く");
        else if (st.feedback) w.Append("OK");
        else w.Append(d.menu == MenuKind::OnApply && st.value != 0 ? "予約" : "実行");
        break;
    case Control::Number:
    case Control::Slider:
        if (!st.available) w.Append("---");
        else FormatNumber(d, st.value, view.value, sizeof(view.value));
        break;
    case Control::Select:
        if (!st.available) w.Append("---");
        else if (st.value >= 0 && st.value < d.option_count) w.Append(d.options[st.value]);
        break;
    case Control::Multiselect:
        w.Unsigned(PopCount(static_cast<u32>(st.value))).Append("/").Unsigned(d.option_count);
        break;
    case Control::Key: FormatHotkey(static_cast<u16>(st.value), view.value, sizeof(view.value)); break;
    }
    return view;
}
} // namespace gohan::menu

#include "menu/engine.hpp"

// 編集（value）・適用待ち・適用（applied）・差し戻し・値の固定。
namespace gohan::menu {
namespace {
s32 Clamp(s32 value, s32 low, s32 high) { return value < low ? low : value > high ? high : value; }
} // namespace

bool Engine::SetValue(u16 item, s32 value) {
    if (!Has(item) || !HasValue(item) || Hidden(item) || Disabled(item)) return false;
    const ItemDef &d = defs_[item];
    switch (d.control) {
    case Control::Toggle:
    case Control::Action: value = value ? 1 : 0; break;
    case Control::Select:
    case Control::Number:
    case Control::Slider: value = Clamp(value, d.min, d.max); break;
    case Control::Multiselect: value &= d.max; break;
    case Control::Key: value &= 0x3FFF; break;
    case Control::Folder: return false;
    }
    state_[item].value = value;
    return true;
}

bool Engine::SetHotkey(u16 item, u16 mask) {
    if (!Has(item) || Hidden(item) || Disabled(item)) return false;
    const ItemDef &d = defs_[item];
    if (d.control == Control::Folder || d.control == Control::Key) return false;
    if (mask != 0) {
        if ((mask & reserved_) != 0) return false;
        for (u16 i = 0; i < count_; ++i) {
            if (i == item || Hidden(i) || state_[i].hotkey != mask) continue;
            Notice notice;
            NoticeHotkeyConflict(notice, d.label, defs_[i].label);
            Emit(notice);
            return false;
        }
    }
    state_[item].hotkey = mask; // 適用するまで有効にならない。効果にも触れない
    return true;
}

// 値を書く・代入する。ゲームへの書き込みが失敗したら false（理由は通知する）。
bool Engine::ApplyValue(u16 item) {
    const ItemDef &d = defs_[item];
    ItemState &st = state_[item];
    if (d.source == Source::Game) {
        Result result = is_bound_[item] && bound_[item].game.write ? bound_[item].game.write(st.applied) : Result::Failed;
        if (result != Result::Ok) {
            Notice notice;
            NoticeFailed(notice, d.label, result);
            Emit(notice);
            return false;
        }
        if (st.fixed) st.fixed_value = st.applied; // 固定中は新しい値で固定し続ける
    } else if (is_bound_[item] && bound_[item].setting.applied) {
        bound_[item].setting.applied(st.applied);
    }
    return true;
}

// 適用: 編集値を適用値にして、振る舞いごとの効果を起こす。ホットキーだけの変更では効果に触れない。
bool Engine::CommitBody(u16 item, u32 now) {
    if (!Dirty(item)) return false;
    const ItemDef &d = defs_[item];
    ItemState &st = state_[item];
    const bool value_changed = HasValue(item) && st.value != st.applied;
    const s32 previous = st.applied;
    if (HasValue(item)) st.applied = st.value;
    st.applied_hotkey = st.hotkey;
    if (!value_changed) return true;

    switch (d.behavior) {
    case Behavior::State:
        if (st.applied != 0) {
            DeactivateGroup(item, now);
            // ON: ホットキーが無ければ効果 ON。あれば待機（効果は変えず、ホットキーが駆動する）。
            if (st.applied_hotkey == 0) SetEffect(item, true);
        } else {
            SetEffect(item, false); // OFF は必ず効果 OFF
        }
        break;
    case Behavior::Hold:
        if (st.applied == 0) {
            if (st.hold_active && is_bound_[item] && bound_[item].hold.step) bound_[item].hold.step(false);
            st.hold_active = false;
        }
        break;
    case Behavior::Trigger:
        if (d.control == Control::Action && d.menu == MenuKind::OnApply) {
            if (st.applied != 0) RunTrigger(item, 0, now);
            st.value = st.applied = 0; // 実行しても取り消しても、予約は戻る
        }
        break;
    case Behavior::Value:
        if (!ApplyValue(item)) {
            st.applied = previous; // 適用できなかった。適用待ちのまま残す
            return false;
        }
        break;
    case Behavior::Mode:
    case Behavior::None: break;
    }
    return true;
}

bool Engine::Commit(u16 item, u32 now) {
    if (!Has(item) || Hidden(item) || Disabled(item)) return false;
    const s32 previous = state_[item].applied;
    const u16 previous_hotkey = state_[item].applied_hotkey;
    const bool changed = CommitBody(item, now);
    if (previous_hotkey != state_[item].applied_hotkey ||
        (previous != state_[item].applied && RemembersValue(item))) ++revision_;
    if (state_[item].fixed) state_[item].fixed_value = state_[item].applied;
    Poll(); // 効果が変わったら、次のフレームを待たずに通知する
    return changed;
}

bool Engine::CommitAll(u32 now) {
    bool any = false;
    for (u16 i = 0; i < count_; ++i) any = Commit(i, now) || any;
    return any;
}

void Engine::Discard(u16 item) {
    if (!Has(item) || !Dirty(item)) return;
    ItemState &st = state_[item];
    if (HasValue(item)) st.value = st.applied;
    st.hotkey = st.applied_hotkey;
}

void Engine::DiscardAll() {
    for (u16 i = 0; i < count_; ++i)
        if (!Hidden(i) && !Disabled(i)) Discard(i);
}

// 編集値だけを適用して固定する。編集中のホットキーはここでは適用しない。
bool Engine::SetFixed(u16 item, bool on, u32 now) {
    if (!CanFix(item)) return false;
    auto &st = state_[item];
    if (!on) {
        if (!st.fixed) return true;
        st.fixed = false;
        st.fixed_failed = false;
    } else {
        if (Hidden(item) || Disabled(item)) return false;
        if (st.fixed && st.applied == st.value) return true;
        const auto previous = st;
        st.applied = st.value;
        const auto &d = defs_[item];
        bool ok = true;
        if (d.behavior == Behavior::State) {
            if (st.applied) DeactivateGroup(item, now);
            if (!st.applied || !st.applied_hotkey) ok = SetEffect(item, st.applied != 0) == Result::Ok;
        } else if (d.behavior == Behavior::Hold) {
            if (!st.applied && st.hold_active && is_bound_[item] && bound_[item].hold.step)
                bound_[item].hold.step(false);
            if (!st.applied) st.hold_active = false;
        } else {
            ok = ApplyValue(item);
        }
        if (!ok) { st = previous; return false; }
        st.fixed_value = st.applied;
        st.fixed = true;
        if (!previous.fixed) st.fixed_failed = false;
    }
    ++revision_;
    // 固定の操作は専用の通知だけを出す。効果の通知を重ねない。
    if (defs_[item].behavior == Behavior::State) st.seen = EffectOn(item);
    Notice notice;
    NoticeItemAction(notice, !on ? "固定を外す" : defs_[item].control == Control::Toggle ?
        (st.applied ? "ONで固定" : "OFFで固定") : "値を固定", defs_[item].label);
    Emit(notice);
    return true;
}

void Engine::DriveFixed() {
    for (u16 i = 0; i < count_; ++i) {
        auto &st = state_[i];
        if (!st.fixed || !is_bound_[i]) continue;
        if (defs_[i].behavior == Behavior::State) {
            if (!st.applied || st.applied_hotkey || EffectOn(i)) continue;
            const auto result = bound_[i].patch.set ? bound_[i].patch.set(true) : Result::Failed;
            if (result == Result::Ok) st.effect = true;
            else if (result != Result::NotReady && !st.fixed_failed) {
                st.fixed_failed = true;
                Notice notice;
                NoticeFailed(notice, defs_[i].label, result);
                Emit(notice);
            }
        } else if (defs_[i].behavior == Behavior::Value && defs_[i].source == Source::Game) {
            s32 value = 0;
            if (!bound_[i].game.read || !bound_[i].game.read(&value) || value == st.fixed_value) continue;
            if (bound_[i].game.write) bound_[i].game.write(st.fixed_value);
        }
    }
}

void Engine::SubmitUi(s32 value, u32 now) {
    const UiRequest request = ui_;
    ui_ = UiRequest{};
    if (request.kind == UiKind::None || request.kind == UiKind::Actions ||
        request.kind == UiKind::Reorder || request.kind == UiKind::Settings || !Has(request.item)) return;
    if (request.kind == UiKind::TriggerInput) {
        if (!Hidden(request.item) && !Disabled(request.item)) RunTrigger(request.item, static_cast<u32>(value), now);
    } else if (request.kind == UiKind::Key && defs_[request.item].control != Control::Key) {
        SetHotkey(request.item, static_cast<u16>(value));
    } else {
        SetValue(request.item, value);
    }
    Poll();
}

void Engine::CancelUi() { ui_ = UiRequest{}; }
} // namespace gohan::menu

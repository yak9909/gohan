#include "menu/engine.hpp"

// 効果・実行・画面・ホットキーの発火・毎フレームの処理・排他。
namespace gohan::menu {
// 効果を入れる・外す。すでにその状態なら何もしない。
Result Engine::SetEffect(u16 item, bool on) {
    if (EffectOn(item) == on) return Result::Ok;
    Result result = Result::Ok;
    if (is_bound_[item] && bound_[item].patch.set) result = bound_[item].patch.set(on);
    if (result == Result::Ok) {
        state_[item].effect = on;
    } else {
        Notice notice;
        NoticeFailed(notice, defs_[item].label, result);
        Emit(notice);
    }
    return result;
}

// 同じ group の別の項目を止める（状態は項目も効果も OFF、画面は閉じる）。
void Engine::Deactivate(u16 item, u32 now) {
    (void)now;
    ItemState &st = state_[item];
    if (defs_[item].behavior == Behavior::State) {
        st.value = st.applied = 0;
        SetEffect(item, false);
    } else if (defs_[item].behavior == Behavior::Mode && st.mode_active) {
        st.mode_active = false;
        if (is_bound_[item] && bound_[item].mode.close) bound_[item].mode.close();
    }
}

void Engine::DeactivateGroup(u16 item, u32 now) {
    const u16 group = defs_[item].group;
    if (group == 0) return;
    for (u16 i = 0; i < count_; ++i)
        if (i != item && defs_[i].group == group) Deactivate(i, now);
}

void Engine::RunTrigger(u16 item, u32 input, u32 now) {
    const ItemDef &d = defs_[item];
    ItemState &st = state_[item];
    Result result = Result::Failed;
    if (is_bound_[item]) {
        const Trigger &t = bound_[item].trigger;
        if (d.input != InputKind::None) {
            if (t.run_with) result = t.run_with(input);
        } else if (t.run) {
            result = t.run();
        }
    }
    Notice notice;
    if (result == Result::Ok) {
        st.feedback = true; // 数フレームの間「OK」を見せる
        st.feedback_until = now + timing::kFeedbackMs;
        NoticeExecuted(notice, d.label);
    } else {
        NoticeFailed(notice, d.label, result);
    }
    Emit(notice);
}

// 画面（mode）。起動中ならもう一度で閉じる。開く前に同じ group の別の画面を閉じる。
void Engine::ToggleMode(u16 item, u32 now) {
    ItemState &st = state_[item];
    if (st.mode_active) {
        Deactivate(item, now);
        return;
    }
    DeactivateGroup(item, now);
    Result result = Result::Failed;
    if (is_bound_[item] && bound_[item].mode.open) result = bound_[item].mode.open();
    if (result == Result::Ok) {
        st.mode_active = true;
        return;
    }
    Notice notice;
    NoticeFailed(notice, defs_[item].label, result);
    Emit(notice);
}

void Engine::OpenDialog(DialogKind kind, u16 item) {
    dialog_ = kind;
    dialog_item_ = item;
    dialog_yes_ = true;
    hold_active_ = false;
}

// ホットキーを押した瞬間。メニューを開いている間は呼ばれない。
void Engine::Fire(u16 item, u32 now) {
    if (Hidden(item) || Disabled(item)) return;
    const ItemDef &d = defs_[item];
    ItemState &st = state_[item];
    switch (d.behavior) {
    case Behavior::State:
        // 項目が OFF なら何もしない。ON なら効果だけを反転する（項目の ON/OFF は変えない）。
        if (st.applied == 0) return;
        if (!EffectOn(item)) DeactivateGroup(item, now);
        SetEffect(item, !EffectOn(item));
        return;
    case Behavior::Hold: return; // 押している間の処理は Tick が見る
    case Behavior::Trigger:
        // トグルの実行（単発実行型）は有効の間だけ。実行（action）はいつでも。
        if (d.control == Control::Toggle && st.applied == 0) return;
        if (d.input != InputKind::None) {
            ui_ = UiRequest{UiKind::TriggerInput, item};
        } else if (d.confirm == Confirm::Hotkey) {
            OpenDialog(DialogKind::Action, item);
        } else {
            RunTrigger(item, 0, now);
        }
        return;
    case Behavior::Mode: ToggleMode(item, now); return;
    case Behavior::Value:
        if (d.source == Source::Game && !ReadGame(item)) return;
        switch (d.control) {
        case Control::Number: ui_ = UiRequest{UiKind::Number, item}; break;
        case Control::Slider: ui_ = UiRequest{UiKind::Slider, item}; break;
        case Control::Select: ui_ = UiRequest{UiKind::List, item}; break;
        case Control::Multiselect: ui_ = UiRequest{UiKind::CheckList, item}; break;
        default: break;
        }
        return;
    case Behavior::None: return;
    }
}

// 効果の変化を観測して通知する。原因（メニュー・ホットキー・機能）を問わず、本当に変わった時だけ出す。
void Engine::Poll() {
    for (u16 i = 0; i < count_; ++i) {
        if (defs_[i].behavior != Behavior::State) continue;
        const bool on = EffectOn(i);
        if (on == state_[i].seen) continue;
        state_[i].seen = on;
        Notice notice;
        NoticeEffect(notice, defs_[i].label, on);
        Emit(notice);
    }
}

void Engine::Tick(u32 now, u16 held) {
    const bool blocked = visible_ || dialog_ != DialogKind::None || ui_.kind != UiKind::None;
    for (u16 i = 0; i < count_; ++i) {
        const ItemDef &d = defs_[i];
        ItemState &st = state_[i];
        if (d.behavior == Behavior::Hold && st.applied != 0) {
            // 押している間だけ動く。メニューを開いている間は自動的に止まる。
            const u16 mask = st.applied_hotkey;
            st.hold_active = mask != 0 && !blocked && (held & mask) == mask;
            if (is_bound_[i] && bound_[i].hold.step) bound_[i].hold.step(st.hold_active);
        } else if (d.behavior == Behavior::Mode && st.mode_active && is_bound_[i] && bound_[i].mode.step) {
            if (!bound_[i].mode.step()) st.mode_active = false; // 機能が自分で閉じた
        }
        if (st.feedback && static_cast<s32>(now - st.feedback_until) >= 0) st.feedback = false;
    }
    DriveFixed();
    Poll();
}
} // namespace gohan::menu

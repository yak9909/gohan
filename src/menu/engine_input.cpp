#include "menu/engine.hpp"

// 入力の解釈。ボタンの立ち上がり・十字キーのリピート・X/L の長押し・確認ダイアログ・ホットキー。
namespace gohan::menu {
namespace {
constexpr u16 kDirections[4] = {key::Up, key::Down, key::Left, key::Right};
constexpr u16 kPlainKeys[] = {key::Up, key::Down, key::Left, key::Right, key::A, key::B, key::Y, key::R, key::Start};
} // namespace

void Engine::Press(u16 pressed_key, u32 now, bool repeated) {
    if (dialog_ != DialogKind::None) {
        if (!repeated) PressDialog(pressed_key, now);
        return;
    }
    if (!visible_ || ui_.kind != UiKind::None) return;
    PressMenu(pressed_key, now, repeated);
    Poll();
}

void Engine::PressMenu(u16 pressed_key, u32 now, bool repeated) {
    const u16 selected = Selected();
    switch (pressed_key) {
    case key::Up: Move(-1, !repeated); break; // 端での折り返しは押した瞬間だけ（リピートでは端で止まる）
    case key::Down: Move(1, !repeated); break;
    case key::Left: Adjust(selected, -1); break;
    case key::Right: Adjust(selected, 1); break;
    case key::A:
        if (!repeated) Activate(selected, now);
        break;
    case key::B:
        if (!repeated) Back(now);
        break;
    case key::X:
        if (!repeated && selected != kNoItem) Commit(selected, now);
        break;
    case key::L:
        if (!repeated && selected != kNoItem && !Disabled(selected)) Discard(selected);
        break;
    case key::R:
        if (!repeated && !hold_active_ && Actions(selected, nullptr)) ui_ = {UiKind::Actions, selected};
        break;
    case key::Start:
        if (!repeated && !hold_active_) ui_ = {UiKind::Settings, kNoItem};
        break;
    case key::Y: // ホットキーの設定
        if (!repeated && selected != kNoItem && Def(selected).control != Control::Folder && !Disabled(selected))
            ui_ = UiRequest{UiKind::Key, selected};
        break;
    default: break;
    }
}

void Engine::PressDialog(u16 pressed_key, u32 now) {
    switch (pressed_key) {
    case key::Left:
    case key::Up: dialog_yes_ = true; break;
    case key::Right:
    case key::Down: dialog_yes_ = false; break;
    case key::A: Confirm(dialog_yes_, now); break;
    case key::B: // 取り消しは「キャンセル」を選んだ状態で閉じる（閉じる途中の表示も取り消しを示す）。
        dialog_yes_ = false;
        Confirm(false, now);
        break;
    default: break;
    }
    Poll();
}

// 承諾した時だけ中身を実行する。閉じる時の確認は、全項目を適用してから閉じる。
void Engine::Confirm(bool yes, u32 now) {
    const DialogKind kind = dialog_;
    const u16 item = dialog_item_;
    dialog_ = DialogKind::None;
    dialog_item_ = kNoItem;
    if (!yes || kind == DialogKind::None) return;
    switch (kind) {
    case DialogKind::Close:
        CommitAll(now);
        visible_ = false;
        break;
    case DialogKind::ApplyAll: CommitAll(now); break;
    case DialogKind::RevertAll: DiscardAll(); break;
    case DialogKind::Action:
        if (item != kNoItem) RunTrigger(item, 0, now);
        break;
    case DialogKind::None: break;
    }
    Poll();
}

// 十字キーのリピート。押した瞬間から 200ms 後に始まり、60ms ごとに送る。
void Engine::Repeat(u16 held, u16 pressed, u32 now) {
    for (unsigned n = 0; n < 4; ++n) {
        const u16 bit = kDirections[n];
        if ((pressed & bit) != 0) {
            repeat_at_[n] = now + timing::kRepeatDelayMs;
            continue;
        }
        if ((held & bit) == 0) continue;
        for (int guard = 0; guard < 8 && static_cast<s32>(now - repeat_at_[n]) >= 0; ++guard) {
            Press(bit, now, true);
            repeat_at_[n] += timing::kRepeatIntervalMs;
        }
    }
}

// X／L の長押し。満了の 2/5 未満で離せば単体の操作、満了前に離せば取り消し、満了で全項目の確認。
void Engine::FinishHold(u32 now, bool released) {
    const u32 elapsed = now - hold_start_;
    if (released) {
        hold_active_ = false;
        if (static_cast<std::uint64_t>(elapsed) * timing::kHoldCancelDen <
            static_cast<std::uint64_t>(timing::kHoldMs) * timing::kHoldCancelNum) {
            Press(hold_key_, now);
        } else {
            Notice notice;
            NoticeHoldCancelled(notice, hold_key_ == key::X);
            Emit(notice);
        }
        return;
    }
    if (elapsed >= timing::kHoldMs) OpenDialog(hold_key_ == key::X ? DialogKind::ApplyAll : DialogKind::RevertAll, kNoItem);
}

bool Engine::HoldProgress(u32 now, u32 *permille, bool *muted, u16 *held_key) const {
    if (!hold_active_) return false;
    const std::uint64_t elapsed = now - hold_start_;
    if (permille) *permille = elapsed >= timing::kHoldMs ? 1000 : static_cast<u32>(elapsed * 1000 / timing::kHoldMs);
    if (muted) *muted = elapsed * timing::kHoldCancelDen < static_cast<std::uint64_t>(timing::kHoldMs) * timing::kHoldCancelNum;
    if (held_key) *held_key = hold_key_;
    return true;
}

// 1 フレーム。held は押されているボタンのビット。
void Engine::Frame(u32 now, u16 held) {
    const u16 pressed = static_cast<u16>(held & ~prev_held_);
    const u16 released = static_cast<u16>(prev_held_ & ~held);
    if (dialog_ != DialogKind::None) {
        for (u16 bit : kPlainKeys)
            if ((pressed & bit) != 0) Press(bit, now);
    } else if (ui_.kind != UiKind::None) {
        // 入力 UI が持っている間は、メニューもホットキーも動かさない。
    } else if (visible_) {
        for (u16 bit : kPlainKeys)
            if ((pressed & bit) != 0) Press(bit, now);
        Repeat(held, pressed, now);
        if (!hold_active_ && dialog_ == DialogKind::None && ui_.kind == UiKind::None && (pressed & (key::X | key::L)) != 0) {
            hold_active_ = true;
            hold_key_ = (pressed & key::X) != 0 ? key::X : key::L;
            hold_start_ = now;
        }
        if (hold_active_) FinishHold(now, (released & hold_key_) != 0);
    } else if (pressed != 0) {
        // メニューを閉じている間のホットキー。全ボタンが揃い、かつ今フレームで押し始めたものがある時。
        for (u16 i = 0; i < count_; ++i) {
            const u16 mask = state_[i].applied_hotkey;
            if (mask == 0 || (held & mask) != mask || (pressed & mask) == 0) continue;
            Fire(i, now);
            if (dialog_ != DialogKind::None || ui_.kind != UiKind::None) break;
        }
    }
    prev_held_ = held;
    Tick(now, held);
}
} // namespace gohan::menu

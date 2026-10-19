#include "menu/shell.hpp"
#include "menu/layout.hpp"

namespace gohan::menu {
Shell::Shell(Engine &engine, ShellConfig config) : engine_(engine), config_(config) {
    engine_.SetNoticeSink({&Shell::OnNotice, this});
}

void Shell::OnNotice(void *self, const Notice &notice) {
    Shell *shell = static_cast<Shell *>(self);
    shell->notices_.Add(notice, shell->now_);
}

void Shell::Toggle(u32 now) {
    if (Modal()) return;
    if (engine_.Visible()) engine_.RequestClose(now);
    else engine_.Open(now);
}

void Shell::Step(u32 now, const Input &input, bool toggle) {
    now_ = now;
    const u16 pressed = static_cast<u16>(input.held & ~prev_held_);
    const bool touch_down = config_.touch && input.touch.down && !prev_touch_;
    const bool touch_up = config_.touch && !input.touch.down && prev_touch_;
    // Select 単独で開閉する（Up を押している時は CTRPF のメニューのために残す）。
    if (toggle || ((pressed & key::Select) != 0 && (input.held & key::Up) == 0)) Toggle(now);

    const u16 selected = engine_.Selected();
    const bool folder = selected == kNoItem || engine_.Def(selected).control == Control::Folder;
    const bool entry_before = entry_.Active();
    // 開閉のボタンはエンジンに渡さない（ホットキーにも使えない）。
    engine_.Frame(now, static_cast<u16>(input.held & ~key::Select));
    if (engine_.Ui().kind != UiKind::None) {
        if (!entry_.Active()) entry_.Begin(engine_, input, config_.screen_width, config_.touch);
        else if (entry_before) entry_.Step(engine_, now, input, engine_.Visible());
    } else {
        entry_.End();
    }
    if (!Modal() && engine_.Visible()) {
        if ((pressed & key::A) != 0 && !folder) panel_.NudgeAction(now);
        if ((pressed & (key::Left | key::Right)) != 0) panel_.NudgeValue(now, (pressed & key::Right) != 0 ? 1 : -1);
    }
    if (!entry_.Active()) Touch(now, input, touch_down, touch_up);
    else dragging_list_ = false;

    const bool modal = Modal();
    if (modal && !was_modal_) {
        modal_.Jump(0.0f, now);
        modal_.Go(1.0f, now, timing::kDialogMs);
    }
    was_modal_ = modal;
    panel_.Sync(engine_, now);
    prev_held_ = input.held;
    prev_touch_ = input.touch.down;
}

void Shell::TapRow(int row, u32 now) {
    const u16 item = panel_.ItemAt(row);
    if (item == kNoItem) return;
    if (item == engine_.Selected()) {
        panel_.NudgeAction(now);
        engine_.Press(key::A, now);
    } else {
        // 指で動かしたスクロールの位置は変えず、選択だけ移す。
        panel_.KeepViewOnNextSelect();
        engine_.Select(item);
    }
}

void Shell::Touch(u32 now, const Input &input, bool down, bool up) {
    const Pointer &pointer = input.touch;
    if (engine_.Dialog() != DialogKind::None) {
        if (down) {
            const int option = DialogOptionAt(config_.screen_width, engine_.Visible(), pointer.x, pointer.y);
            if (option >= 0) engine_.Confirm(option == 0, now);
        }
        return;
    }
    if (!engine_.Visible()) {
        dragging_list_ = false;
        return;
    }
    if (down) {
        int count = 0;
        const Hit *hits = panel_.Hits(&count);
        for (int i = 0; i < count; ++i) {
            if (!hits[i].bounds.Contains(pointer.x, pointer.y)) continue;
            engine_.Press(hits[i].key, now);
            return;
        }
        if (panel_.InList(pointer.x, pointer.y, now)) {
            scroll_.scroll = panel_.ViewStart(now) * static_cast<float>(style::kItemHeight);
            scroll_.max = panel_.MaxViewStart() * static_cast<float>(style::kItemHeight);
            const bool was_flinging = scroll_.Press(static_cast<float>(pointer.y), now);
            dragging_list_ = true;
            press_row_ = was_flinging ? -1 : panel_.RowAt(pointer.y, now);
        }
    }
    if (pointer.down && dragging_list_) {
        scroll_.Hold(static_cast<float>(pointer.y), now);
        if (scroll_.Scrolling()) panel_.JumpView(scroll_.scroll / static_cast<float>(style::kItemHeight), now);
    }
    if (up && dragging_list_) {
        const bool tap = !scroll_.Scrolling() && press_row_ >= 0;
        scroll_.Release(now);
        dragging_list_ = false;
        if (tap) TapRow(press_row_, now);
    }
    if (!pointer.down) {
        scroll_.Idle(now);
        if (scroll_.Flinging()) panel_.JumpView(scroll_.scroll / static_cast<float>(style::kItemHeight), now);
    }
}

bool Shell::NeedsDraw(u32 now) const {
    return panel_.Visible(now) || panel_.Animating(now) || Modal() || notices_.Alive(now) || !modal_.Done(now);
}

void Shell::Draw(Surface &surface, u32 now) {
    panel_.SetRows(layout::RowsFor(surface.TextsLeft()), now);
    const bool modal = Modal();
    panel_.Draw(surface, engine_, now, modal);
    const float menu_amount = panel_.OpenAmount(now);
    const float appear = modal_.At(now);
    if (engine_.Dialog() != DialogKind::None) DrawDialog(surface, engine_, menu_amount, appear);
    else if (entry_.Active()) entry_.Draw(surface, menu_amount, appear, engine_.Visible());
    notices_.Draw(surface, now);
    if (!modal) panel_.DrawDescription(surface, engine_, now);
}
} // namespace gohan::menu

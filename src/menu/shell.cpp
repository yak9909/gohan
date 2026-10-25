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
    // Select 単独で開閉する（Up を押している時は CTRPF のメニューのために残す）。
    if (toggle || ((pressed & key::Select) != 0 && (input.held & key::Up) == 0)) Toggle(now);

    const u16 selected = engine_.Selected();
    const bool folder = selected == kNoItem || engine_.Def(selected).control == Control::Folder;
    const bool entry_before = entry_.Active();
    const DialogView before = ViewOf(engine_);
    // 開閉のボタンはエンジンに渡さない（ホットキーにも使えない）。
    engine_.Frame(now, static_cast<u16>(input.held & ~key::Select));
    // 確認が閉じたら、決定した答えと操作案内を写しに残して退場させる。
    // 選んだ状態を選択の動きの時間だけ見せてから、確認の出入りと同じ時間で消す。
    if (before.kind != DialogKind::None && engine_.Dialog() == DialogKind::None) {
        closing_ = before;
        closing_.yes = engine_.DialogYes();
        dialog_close_ = motion::Tween{1.0f, 0.0f, now + timing::kSelectionMoveMs, timing::kDialogMs};
    }
    if (engine_.Ui().kind != UiKind::None) {
        if (!entry_.Active()) entry_.Begin(engine_, input, config_.screen_width);
        else if (entry_before) entry_.Step(engine_, now, input, engine_.Visible());
    } else {
        entry_.End();
    }
    if (!Modal() && engine_.Visible()) {
        if ((pressed & key::A) != 0 && !folder) panel_.NudgeAction(now);
        if ((pressed & (key::Left | key::Right)) != 0) panel_.NudgeValue(now, (pressed & key::Right) != 0 ? 1 : -1);
    }

    // 退場の途中に新しい確認や入力の枠が出たら、退場の写しは残さない。
    if (Modal()) {
        dialog_close_ = motion::Tween{};
        closing_ = {};
    } else if (engine_.Visible() && dialog_close_.Done(now)) closing_ = {};
    const bool modal = Modal();
    if (modal && !was_modal_) {
        modal_.Jump(0.0f, now);
        modal_.Go(1.0f, now, timing::kDialogMs);
    }
    was_modal_ = modal;
    panel_.Sync(engine_, now);
    prev_held_ = input.held;
}

bool Shell::NeedsDraw(u32 now) const {
    return panel_.Visible(now) || panel_.Animating(now) || Modal() || notices_.Alive(now) || !modal_.Done(now) ||
           !dialog_close_.Done(now);
}

core::input_block::Claim Shell::InputDemand() const {
    return {engine_.Visible() || Modal() ? core::input_block::group::Buttons : 0, 0};
}

void Shell::Draw(Surface &surface, u32 now) {
    panel_.SetRows(layout::RowsFor(surface.TextsLeft()), now);
    const bool modal = Modal();
    panel_.Draw(surface, engine_, now, modal);
    const float menu_amount = panel_.OpenAmount(now);
    const float appear = modal_.At(now);
    if (!entry_.Active()) panel_.DrawDescription(surface, engine_, now);
    if (engine_.Dialog() != DialogKind::None) DrawDialog(surface, engine_, ViewOf(engine_), menu_amount, appear);
    else if (entry_.Active()) entry_.Draw(surface, menu_amount, appear, engine_.Visible());
    else if (!dialog_close_.Done(now)) DrawDialog(surface, engine_, closing_, menu_amount, dialog_close_.At(now));
    const bool dialog = engine_.Dialog() != DialogKind::None;
    const bool closing = !dialog_close_.Done(now);
    // メニューごと閉じる場合は、確認の退場後に通常の帯のフェードの残りを出さない。
    const bool menu_bar = panel_.BarVisible(now) &&
                          (engine_.Visible() || entry_.Active() || closing_.kind == DialogKind::None);
    if (dialog || closing || menu_bar)
        panel_.DrawBar(surface, engine_, now, dialog || closing, closing ? dialog_close_.At(now) : appear);
    notices_.Draw(surface, now, panel_.Visible(now));
}
} // namespace gohan::menu

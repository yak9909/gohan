// メニューの画面（開閉・行の表示・タッチ・ダイアログ・通知）を、収録している見本の spec で試験する。
// 描画先は代役で、描画指示（矩形・文字・色）と予算（矩形 48・文字 48）の中身を確かめる。
#include "menu/layout.hpp"
#include "menu/sample_items.hpp"
#include "menu_ids.hpp"
#include "menu_rig.hpp"
#include <cstdio>
#include <cstring>

using namespace menu_test;

namespace {
u16 Idx(MenuId id) { return static_cast<u16>(id); }

// 開いて見本のフォルダに入る。
void EnterSample(Rig &rig) {
    rig.Open();
    rig.Tap(key::A);
    rig.Advance(200);
    rig.Draw(); // 行数が決まる
}

void AssertBudget(const RecordingSurface &s) {
    assert(s.dropped == 0);
    assert(s.texts <= static_cast<int>(gohan::core::limits::RendererTexts));
    assert(s.rects <= static_cast<int>(gohan::core::limits::RendererRects));
}

// 行 n の文字の中央（開いて動きが止まり、先頭から表示している時）。
int RowY(int row) { return 25 + row * 18 + 6; }

void OpenAndClose() {
    sample::Get() = sample::State{};
    Rig rig(kMenuItems, kMenuItemCount);
    assert(rig.engine.BindAll(sample::kBindings, sample::kBindingCount));
    assert(!rig.shell.NeedsDraw(rig.now));
    assert(rig.Draw().commands.empty());

    // Select 単独で開く。出入りは 320ms で、帯が左から滑り込む。
    rig.Tap(key::Select);
    assert(rig.engine.Visible() && rig.shell.NeedsDraw(rig.now));
    const RecordingSurface sliding = rig.Draw();
    const Command &early = sliding.commands.front();
    assert(early.kind == Command::Fill && early.bounds.w == 160 && early.bounds.x < 0 && early.bounds.x > -160);
    rig.Advance(400);
    const RecordingSurface open = rig.Draw();
    assert(open.commands.front().bounds.x == 0 && open.commands.front().bounds.h == 240);
    assert(open.commands.front().color == style::kPanel);
    assert(open.HasText("-* GOHAN *-") && open.HasText(">見本") && open.HasText("A決定"));
    AssertBudget(open);

    // 閉じる動きが終わったら、描くものは無い。
    rig.Tap(key::Select);
    assert(!rig.engine.Visible());
    assert(rig.shell.NeedsDraw(rig.now));
    rig.Advance(400);
    assert(!rig.shell.NeedsDraw(rig.now));

    // Select+十字上は CTRPF のメニューのために残す。外からの要求では開閉する。
    rig.Hold(key::Select | key::Up);
    assert(!rig.engine.Visible());
    rig.Release();
    rig.Frame(16, true);
    assert(rig.engine.Visible());
    rig.Frame(16, true);
    assert(!rig.engine.Visible());
}

void RowsAndStates() {
    sample::Get() = sample::State{};
    Rig rig(kMenuItems, kMenuItemCount);
    assert(rig.engine.BindAll(sample::kBindings, sample::kBindingCount));
    EnterSample(rig);
    RecordingSurface s = rig.Draw();
    AssertBudget(s);
    // 題に今のフォルダの名前が付く。帯の行数は予算（文字 48）から決まり、上限の 10 行になる。
    assert(s.HasText("-* GOHAN *- 見本"));
    assert(rig.shell.panel().Rows() == 10 && rig.shell.panel().Count() == 6);
    assert(s.HasText("見本のフラグ") && s.HasText("見本の画面 B") && s.HasText("見本の値")); // 6 項目が 10 行に収まる
    // 選択中の行は白、他は既定の色。OFF の値が右端にある。
    assert(s.FindText("見本のフラグ")->color == style::kWhite);
    assert(s.FindText("見本の押下中")->color == style::kText);
    const Command *off = s.FindText("OFF");
    assert(off && off->bounds.x + off->bounds.w == 152);
    // 実行の項目はボタン（値の周りに枠）。ホットキーのある行には印が付く。
    const Command *run = s.FindText("実行");
    assert(run);
    bool framed = false, marked = false;
    for (const Command &c : s.commands) {
        framed = framed || (c.kind == Command::Outline && c.bounds.x == run->bounds.x - 4 && c.bounds.w == run->bounds.w + 8);
        marked = marked || (c.kind == Command::Fill && c.bounds.w == 1 && c.bounds.h == 12 && c.bounds.x == 4);
    }
    assert(framed && marked);
    // 行と選択の帯は行の領域（帯の幅 158 x 10 行）で切り抜く。題・足・説明は切り抜かない。
    const Command *band = nullptr;
    for (const Command &c : s.commands)
        if (c.kind == Command::Fill && c.bounds.w == 150 && c.bounds.h == 15) band = &c;
    assert(band && band->clipped && band->clip.x == 0 && band->clip.y == 25 && band->clip.w == 158 && band->clip.h == 10 * 18);
    const Command *label = s.FindText("見本のフラグ");
    assert(label->clipped && label->clip.y == 25 && label->clip.h == 10 * 18);
    assert(!s.FindText("A決定")->clipped && !s.FindText("-* GOHAN *-")->clipped && !s.FindText("HOTKEY")->clipped);
    // 説明は予算の残りで描く。予算が十分なら、ホットキーと本文を省かずに描く。
    assert(s.HasText("HOTKEY:L+Up") && !s.HasText("..."));

    // 状態の項目: 編集中の ON は黄色、適用すると待機（青）。ホットキーで効果が入ると ON（緑）。
    const u16 flag = Idx(MenuId::SampleFlag);
    assert(rig.engine.SetValue(flag, 1));
    s = rig.Draw();
    assert(s.FindText("ON")->color == style::kPending);
    assert(rig.engine.Commit(flag, rig.now));
    s = rig.Draw();
    assert(s.FindText("待機")->color == style::kHotkey);
    rig.engine.Press(key::A, rig.now); // 項目を切り替えない位置へ
    rig.engine.Discard(flag);
}

void ReservedButton() {
    sample::Get() = sample::State{};
    Rig rig(kMenuItems, kMenuItemCount);
    assert(rig.engine.BindAll(sample::kBindings, sample::kBindingCount));
    EnterSample(rig);
    rig.Tap(key::Down);
    rig.Tap(key::Down);
    rig.Advance(200);
    RecordingSurface s = rig.Draw();
    assert(rig.engine.Selected() == Idx(MenuId::SampleStamp));
    // 行が増えたので、選んだ行（スタンプ）の値の枠を、行の文字と重なる位置から探す。
    auto stamp_border = [&s] {
        const Command *label = s.FindText("見本のスタンプ");
        const Command *found = nullptr;
        for (const Command &c : s.commands)
            if (c.kind == Command::Outline && c.bounds.h == 14 && c.bounds.y <= label->bounds.y + 4 &&
                c.bounds.y + c.bounds.h >= label->bounds.y + 4)
                found = &c;
        return found;
    };
    const Command *border = stamp_border();
    assert(border && border->color == style::kAccent);
    // A で予約（黄色の「予約」。枠も黄色）。まだ実行しない。
    rig.Tap(key::A);
    s = rig.Draw();
    assert(s.FindText("予約")->color == style::kPending && s.FindText("見本のスタンプ")->color == style::kPending);
    border = stamp_border();
    assert(border && border->color == style::kPending);
    assert(sample::Get().stamps == 0 && s.HasText("1変更"));

    // 足の「X適用」を押す（タッチ）。適用で実行し、予約は戻って、しばらく「OK」を見せる。
    int count = 0;
    const Hit *hits = rig.shell.panel().Hits(&count);
    const Hit *apply = nullptr;
    for (int i = 0; i < count; ++i)
        if (hits[i].key == key::X) apply = &hits[i];
    assert(apply && count == 5);
    rig.TapAt(apply->bounds.x + 3, apply->bounds.y + 3);
    assert(sample::Get().stamps == 1);
    s = rig.Draw();
    assert(s.FindText("OK")->color == style::kAccent);
    rig.Advance(900);
    s = rig.Draw();
    assert(s.HasText("実行") && !s.HasText("OK"));
}

void TouchSelectAndScroll() {
    sample::Get() = sample::State{};
    Rig rig(kMenuItems, kMenuItemCount);
    rig.text_limit = 16; // 6 項目が 5 行に収まらない予算（スクロールできる状態）で確かめる
    assert(rig.engine.BindAll(sample::kBindings, sample::kBindingCount));
    EnterSample(rig);
    // 選んでいない行に触れると選ぶだけ、選んだ行に触れると実行（画面 A は開く）。
    assert(rig.engine.Selected() == Idx(MenuId::SampleFlag));
    rig.TapAt(40, RowY(3));
    assert(rig.engine.Selected() == Idx(MenuId::SampleEditorA) && !sample::Get().editor_open[0]);
    rig.TapAt(40, RowY(3));
    assert(sample::Get().editor_open[0]);
    rig.TapAt(40, RowY(4));
    rig.TapAt(40, RowY(4));
    assert(sample::Get().editor_open[1] && !sample::Get().editor_open[0]);
    RecordingSurface s = rig.Draw();
    assert(s.HasText("起動中"));
    AssertBudget(s);

    // 縦になぞるとスクロールし、選択は変えない。
    const u16 selected = rig.engine.Selected();
    assert(rig.shell.panel().ViewStart(rig.now) < 0.1f);
    rig.TouchAt(60, 110);
    for (int y = 104; y >= 40; y -= 8) rig.TouchAt(60, y);
    assert(rig.shell.panel().ViewStart(rig.now) > 0.3f);
    rig.Untouch();
    assert(rig.engine.Selected() == selected);
    s = rig.Draw();
    AssertBudget(s);
    rig.Advance(1500); // 慣性が止まるまで
    const float rest = rig.shell.panel().ViewStart(rig.now);
    assert(rest > 0.3f && rest <= 1.0f);
    s = rig.Draw();
    assert(s.HasText("見本の値") && !s.HasText("見本のフラグ"));
}

void FooterAndBack() {
    sample::Get() = sample::State{};
    Rig rig(kMenuItems, kMenuItemCount);
    assert(rig.engine.BindAll(sample::kBindings, sample::kBindingCount));
    EnterSample(rig);
    rig.Draw();
    assert(rig.engine.Depth() == 1);
    int count = 0;
    const Hit *hits = rig.shell.panel().Hits(&count);
    const Hit *back = nullptr;
    for (int i = 0; i < count; ++i)
        if (hits[i].key == key::B) back = &hits[i];
    assert(back);
    rig.TapAt(back->bounds.x + 2, back->bounds.y + 2);
    assert(rig.engine.Depth() == 0);
    // 帯の外（右の領域）に触れても何も起きない。
    rig.TapAt(250, 100);
    assert(rig.engine.Depth() == 0 && rig.engine.Visible());
}

void CloseDialog() {
    sample::Get() = sample::State{};
    Rig rig(kMenuItems, kMenuItemCount);
    assert(rig.engine.BindAll(sample::kBindings, sample::kBindingCount));
    EnterSample(rig);
    assert(rig.engine.SetValue(Idx(MenuId::SampleFlag), 1));
    rig.Tap(key::Select); // 適用待ちがあれば、閉じる前に確認する
    assert(rig.engine.Visible() && rig.shell.Modal() && rig.engine.Dialog() == DialogKind::Close);
    rig.Advance(300);
    RecordingSurface s = rig.Draw();
    AssertBudget(s);
    assert(s.HasText("変更を適用しますか？") && s.HasText("適用") && s.HasText("キャンセル"));
    // 確認の間は、足と説明は描かない（枠のための文字を空ける）。
    assert(!s.HasText("HOTKEY") && !s.HasText("A決定"));
    const Rect box = DialogBox(320, true);
    assert(box.x == 168 && box.w == 144);
    // キャンセルに触れると閉じず、メニューに戻る。
    const Rect cancel = DialogOption(box, 1);
    rig.TapAt(cancel.x + 5, cancel.y + 5);
    assert(rig.engine.Dialog() == DialogKind::None && rig.engine.Visible() && !rig.engine.Applied(Idx(MenuId::SampleFlag)));
    // もう一度閉じて「適用」に触れると、適用して閉じる。
    rig.Tap(key::Select);
    const Rect yes = DialogOption(box, 0);
    rig.TapAt(yes.x + 5, yes.y + 5);
    assert(!rig.engine.Visible() && rig.engine.Applied(Idx(MenuId::SampleFlag)) == 1);
}

void HoldToApplyAll() {
    sample::Get() = sample::State{};
    Rig rig(kMenuItems, kMenuItemCount);
    assert(rig.engine.BindAll(sample::kBindings, sample::kBindingCount));
    EnterSample(rig);
    assert(rig.engine.SetValue(Idx(MenuId::SampleFlag), 1));
    // X を押し続けると進みの棒が出る。2/5 未満は灰色、以降は黄色。満了で全項目の確認。
    rig.Hold(key::X);
    rig.Advance(100);
    RecordingSurface s = rig.Draw();
    assert(s.HasText("全適用") && s.rects <= static_cast<int>(gohan::core::limits::RendererRects) && s.dropped == 0);
    bool muted = false, yellow = false;
    for (const Command &c : s.commands) muted = muted || (c.kind == Command::Outline && c.bounds.y == 201 && c.color == style::Argb(0xFF, 0x74, 0x7B, 0x76));
    assert(muted);
    rig.Advance(300);
    s = rig.Draw();
    for (const Command &c : s.commands) yellow = yellow || (c.kind == Command::Outline && c.bounds.y == 201 && c.color == style::kPending);
    assert(yellow);
    rig.Advance(300);
    assert(rig.engine.Dialog() == DialogKind::ApplyAll);
    rig.Release();
    s = rig.Draw();
    assert(s.HasText("全ての変更を適用しますか？"));
    // 短く押して離すと、選択中の項目だけの適用（X）。
    rig.Tap(key::B); // ダイアログを取り消す
    assert(rig.engine.Dialog() == DialogKind::None);
}

void HotkeyDialogWhileClosed() {
    sample::Get() = sample::State{};
    Rig rig(kMenuItems, kMenuItemCount);
    assert(rig.engine.BindAll(sample::kBindings, sample::kBindingCount));
    // メニューを閉じたまま、ホットキーの確認が下画面の中央に出る。
    rig.Hold(key::L | key::Down);
    assert(rig.shell.Modal() && rig.engine.Dialog() == DialogKind::Action && sample::Get().stamps == 0);
    rig.Release();
    rig.Advance(300);
    const RecordingSurface s = rig.Draw();
    AssertBudget(s);
    assert(s.HasText("見本のスタンプを実行しますか？") && s.HasText("実行"));
    const Rect box = DialogBox(320, false);
    assert(box.x == 88);
    bool centered = false;
    for (const Command &c : s.commands) centered = centered || (c.kind == Command::Fill && c.bounds.x == 88 && c.bounds.w == 144);
    assert(centered);
    const Rect yes = DialogOption(box, 0);
    rig.TapAt(yes.x + 5, yes.y + 5);
    assert(!rig.shell.Modal() && sample::Get().stamps == 1);
}

void Notices() {
    sample::Get() = sample::State{};
    Rig rig(kMenuItems, kMenuItemCount);
    assert(rig.engine.BindAll(sample::kBindings, sample::kBindingCount));
    // 待機にしてから、ホットキーで効果を入れる。通知が右から入る。
    rig.engine.Open(rig.now);
    assert(rig.engine.SetValue(Idx(MenuId::SampleFlag), 1) && rig.engine.Commit(Idx(MenuId::SampleFlag), rig.now));
    assert(rig.engine.RequestClose(rig.now));
    rig.Advance(500);
    assert(!rig.shell.NeedsDraw(rig.now));
    rig.Hold(key::L | key::Up);
    assert(sample::Get().flag && rig.shell.notices().Alive(rig.now));
    rig.Release();
    RecordingSurface early = rig.Draw();
    const Command *entering = early.FindText("を有効にしました");
    assert(entering && entering->bounds.x > 168 && entering->bounds.x < 320);
    rig.Advance(500);
    const RecordingSurface settled = rig.Draw();
    AssertBudget(settled);
    const Command *message = settled.FindText("見本のフラグを有効にしました");
    assert(message && message->bounds.x == 320 - 8 - 144 + 7 && message->color == style::kNoticeMessage);
    assert(settled.HasText("見本のフラグ")); // 題
    // 表示が終わったら、描くものは無い。
    rig.Advance(4000);
    assert(!rig.shell.NeedsDraw(rig.now) && !rig.shell.notices().Alive(rig.now));
}

void TopScreenLayout() {
    // 上画面（幅 400）では右の領域が広がり、タッチは受けない。
    ShellConfig config;
    config.screen_width = 400;
    config.touch = false;
    sample::Get() = sample::State{};
    Rig rig(kMenuItems, kMenuItemCount, config);
    assert(rig.engine.BindAll(sample::kBindings, sample::kBindingCount));
    EnterSample(rig);
    const RecordingSurface s = rig.Draw(400);
    AssertBudget(s);
    bool wide = false;
    for (const Command &c : s.commands) wide = wide || (c.kind == Command::Fill && c.bounds.x == 168 && c.bounds.w == 224);
    assert(wide);
    const u16 selected = rig.engine.Selected();
    rig.TapAt(40, RowY(3));
    assert(rig.engine.Selected() == selected);
}

void BudgetShrinksRows() {
    // 予算が増えれば行数も増える（上限は旧版の 10 行）。減れば最小の 3 行まで。
    assert(gohan::menu::layout::RowsFor(16) == 5);
    assert(gohan::menu::layout::RowsFor(48) == 10);
    assert(gohan::menu::layout::RowsFor(0) == 3);
}
} // namespace

int main() {
    OpenAndClose();
    RowsAndStates();
    ReservedButton();
    TouchSelectAndScroll();
    FooterAndBack();
    CloseDialog();
    HoldToApplyAll();
    HotkeyDialogWhileClosed();
    Notices();
    TopScreenLayout();
    BudgetShrinksRows();
    std::puts("menu shell: ok");
    return 0;
}

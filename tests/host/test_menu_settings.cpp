// 設定画面（段 3）を、実際のエンジン・Shell で確かめる。
// 開閉と位置（START・B・SELECT・同じ起動中の位置）、固定中の項目（順・A で外す・選択の寄せ・0 件は選べて入れない）、
// 設定画面の中の X・L（効かない）、
// トグルを全て保持（その場で効く・版の数）、保存を全て消す（既定はキャンセル・全部を戻す・効果は残す）、
// 押し切るボタン（決定・既定・ボタンの対応）、設定画面の行の対象外、確認の中の START。
#include "menu/paint.hpp"
#include "menu/sample_items.hpp"
#include "menu_ids.hpp"
#include "menu_rig.hpp"
#include <cstdio>

using namespace menu_test;

namespace {
u16 Idx(MenuId id) { return static_cast<u16>(id); }

gohan::menu::Engine::SettingsItems Settings() {
    return {Idx(kSettingsRoot), Idx(MenuId::SettingsKeepErase), Idx(MenuId::SettingsKeepToggles),
            Idx(MenuId::SettingsPressThrough)};
}

// 一覧を開いた状態（見本の結び付けと、設定の項目の結び付け）。
void Prepare(Rig &r) {
    sample::Get() = sample::State{};
    assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    r.engine.BindSettings(Settings());
    r.Open();
}

// 一覧の START で設定画面へ入る（同じ起動中の 2 回目以降は、前に見ていた位置から始まる）。
void OpenSettings(Rig &r) {
    r.Tap(key::Start);
    r.Advance(400);
    assert(r.engine.InSettings() && r.engine.Ui().kind == UiKind::None);
}

// 今の場所の行を選んで A で入る（フォルダ・トグル・確認などの A）。
void EnterRow(Rig &r, MenuId id) {
    assert(r.engine.Select(Idx(id)));
    r.Tap(key::A);
    r.Advance(400);
}

// 1. START で開き、もう一度の START で一覧の元の位置へ戻る。B は一番上で一覧へ、1 段目で 1 段戻る。
//    SELECT で閉じると次に開いた時は一覧の元の位置、同じ起動中は設定画面の中の位置を保つ。
void OpenCloseAndPosition() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    const u16 start = r.engine.Selected();
    assert(r.Draw().HasText("設定")); // 一覧の下の帯に START 設定
    OpenSettings(r);
    assert(r.Draw().HasText("閉じる") && r.Draw().HasText("戻る") && !r.Draw().HasText("適用"));
    r.Tap(key::Start);
    r.Advance(400);
    assert(!r.engine.InSettings() && r.engine.Visible() && r.engine.Selected() == start && r.engine.Depth() == 0);
    OpenSettings(r);
    r.Tap(key::B);
    r.Advance(400);
    assert(!r.engine.InSettings() && r.engine.Visible() && r.engine.Depth() == 0);
    OpenSettings(r);
    EnterRow(r, MenuId::SettingsKeep);
    assert(r.engine.InSettings() && r.engine.Depth() == 1 && r.engine.Folder() == Idx(MenuId::SettingsKeep));
    r.Tap(key::B);
    r.Advance(400);
    assert(r.engine.InSettings() && r.engine.Depth() == 0 && r.engine.Folder() == Idx(kSettingsRoot));
    EnterRow(r, MenuId::SettingsKeep);
    r.Tap(key::Start);
    r.Advance(400);
    assert(!r.engine.InSettings() && r.engine.Depth() == 0);
    OpenSettings(r);
    assert(r.engine.Depth() == 1 && r.engine.Folder() == Idx(MenuId::SettingsKeep));
    r.Tap(key::Select);
    assert(!r.engine.Visible());
    r.Open();
    assert(r.engine.Visible() && !r.engine.InSettings() && r.engine.Depth() == 0 && r.engine.Selected() == start);
}

// 2. 固定中の項目: 固定した順に並ぶ。行は元の項目で、左右・Y・R は効かない。A で外し、選択は次の行（最後なら前の行）。
//    0 件の時は灰色で入れない。
void FixedListRules() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    assert(r.engine.SetFixed(Idx(MenuId::SampleHold), true, r.now));
    assert(r.engine.SetFixed(Idx(MenuId::SampleFlag), true, r.now));
    assert(r.engine.FixedCount() == 2 && r.engine.FixedOrder()[0] == Idx(MenuId::SampleHold) &&
           r.engine.FixedOrder()[1] == Idx(MenuId::SampleFlag));
    OpenSettings(r);
    EnterRow(r, MenuId::SettingsFixed);
    assert(r.engine.Depth() == 1 && r.engine.Folder() == Idx(MenuId::SettingsFixed));
    u16 rows[8]{};
    assert(r.engine.Children(r.engine.Folder(), rows, 8) == 2 && rows[0] == Idx(MenuId::SampleHold) &&
           rows[1] == Idx(MenuId::SampleFlag));
    r.Tap(key::Right);
    r.Tap(key::Y);
    r.Tap(key::R);
    assert(r.engine.Ui().kind == UiKind::None && r.engine.Dialog() == DialogKind::None && r.engine.Depth() == 1);
    // 最初の行を外す: 行が消え、選択は次の行（今は最後の行）。
    assert(r.engine.Selected() == Idx(MenuId::SampleHold));
    r.Tap(key::A);
    assert(r.engine.FixedCount() == 1 && !r.engine.State(Idx(MenuId::SampleHold)).fixed &&
           r.engine.Selected() == Idx(MenuId::SampleFlag));
    // 最後の行を外す: 選択は前の行へ（残りは 1 つ）。
    assert(r.engine.SetFixed(Idx(MenuId::SampleHold), true, r.now));
    r.Tap(key::Down);
    assert(r.engine.Selected() == Idx(MenuId::SampleHold));
    r.Tap(key::A);
    assert(r.engine.FixedCount() == 1 && r.engine.Selected() == Idx(MenuId::SampleFlag));
    // 残りを全て外すと、フォルダは灰色になり、入れない。一番上へ戻り、「固定中の項目」の行を選んだままにする。
    r.Tap(key::A);
    assert(r.engine.FixedCount() == 0 && r.engine.Depth() == 0 && r.engine.Folder() == Idx(kSettingsRoot));
    assert(r.engine.Selected() == Idx(MenuId::SettingsFixed) && r.engine.Disabled(Idx(MenuId::SettingsFixed)));
    assert(r.engine.Select(Idx(MenuId::SettingsFixed)));
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Depth() == 0 && r.engine.Folder() == Idx(kSettingsRoot));
    // 説明の状態は「固定している項目はありません」（5.1）。
    assert(r.engine.Select(Idx(MenuId::SettingsFixed)));
    r.Frame();
    assert(r.Draw().HasText("固定している項目はありません"));
}

// 2b. 0 件の「固定中の項目」は灰色で入れないが、選べる行。0 件で初めて開くと先頭の行（固定中の項目）を選び、A では入らない。
void EmptyFixedListIsSelectable() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    OpenSettings(r);
    assert(r.engine.Folder() == Idx(kSettingsRoot) && r.engine.Selected() == Idx(MenuId::SettingsFixed));
    assert(r.engine.Disabled(Idx(MenuId::SettingsFixed)));
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Depth() == 0 && r.engine.Selected() == Idx(MenuId::SettingsFixed));
    r.Tap(key::Down);
    assert(r.engine.Selected() == Idx(MenuId::SettingsKeep));
    r.Tap(key::Up);
    assert(r.engine.Selected() == Idx(MenuId::SettingsFixed));
}

// 2c. 設定画面の中では X・L が効かない（短押しも長押しも始めない）。一覧の適用待ちは確認にかからず、そのまま残る。
//     同じ長押しは一覧では今どおり確認を出す（対照）。
void XAndLDoNothingInSettings() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    const auto amount = Idx(MenuId::SampleAmount);
    assert(r.engine.SetValue(amount, 70) && r.engine.Dirty(amount) && r.engine.DirtyCount() == 1);
    OpenSettings(r);
    assert(r.engine.Select(Idx(MenuId::SettingsKeep)));
    const u32 hold = gohan::menu::timing::kHoldMs + 500;
    r.Hold(key::X);
    r.Advance(hold);
    assert(r.engine.Dialog() == DialogKind::None && r.engine.Ui().kind == UiKind::None);
    assert(!r.engine.HoldProgress(r.now, nullptr, nullptr, nullptr));
    r.Release();
    assert(r.engine.Dialog() == DialogKind::None && r.engine.DirtyCount() == 1 && r.engine.State(amount).value == 70);
    r.Hold(key::L);
    r.Advance(hold);
    assert(r.engine.Dialog() == DialogKind::None && !r.engine.HoldProgress(r.now, nullptr, nullptr, nullptr));
    r.Release();
    assert(r.engine.Dialog() == DialogKind::None && r.engine.DirtyCount() == 1 && r.engine.State(amount).value == 70);
    // 短押しでも何も変わらない（適用も差し戻しもしない）。
    r.Tap(key::X);
    r.Tap(key::L);
    assert(r.engine.DirtyCount() == 1 && r.engine.State(amount).value == 70 && r.engine.Applied(amount) != 70);
    // 対照: 一覧に戻ると、同じ長押しは確認を出す。B で閉じると適用待ちは残る。
    r.Tap(key::Start);
    r.Advance(400);
    assert(!r.engine.InSettings() && r.engine.Visible());
    r.Hold(key::X);
    r.Advance(hold);
    assert(r.engine.Dialog() == DialogKind::ApplyAll);
    r.Release();
    r.Tap(key::B);
    assert(r.engine.Dialog() == DialogKind::None && r.engine.DirtyCount() == 1);
}

// 3. トグルを全て保持: 切り替えは適用待ちを経ず、その場で効く（既定は OFF）。版の数が増える。
void KeepToggles() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    OpenSettings(r);
    EnterRow(r, MenuId::SettingsKeep);
    const auto keep = Idx(MenuId::SettingsKeepToggles);
    assert(!r.engine.KeepToggles() && r.engine.Applied(keep) == 0);
    const auto revision = r.engine.Revision();
    assert(r.engine.Select(keep));
    r.Tap(key::A);
    assert(r.engine.Applied(keep) == 1 && !r.engine.Dirty(keep) && r.engine.KeepToggles());
    assert(r.engine.Revision() > revision);
    r.Tap(key::A);
    assert(r.engine.Applied(keep) == 0 && !r.engine.Dirty(keep) && !r.engine.KeepToggles());
}

// 4. 保存を全て消す: A で確認（既定はキャンセル）。キャンセルでは何もしない。消すで全てを既定に戻す。
//    今 ON のトグルの効果は残す（その起動の間）。
void EraseEverything() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    assert(r.engine.SetFixed(Idx(MenuId::SampleHold), true, r.now));
    assert(r.engine.SetFavorite(Idx(MenuId::SampleFlag), true));
    assert(r.engine.SetHotkey(Idx(MenuId::SampleHold), gohan::menu::key::ZL) && r.engine.Commit(Idx(MenuId::SampleHold), r.now));
    assert(r.engine.SetValue(Idx(MenuId::SampleAmount), 70) && r.engine.Commit(Idx(MenuId::SampleAmount), r.now));
    assert(r.engine.SetValue(Idx(MenuId::SampleFlag), 1) && r.engine.Commit(Idx(MenuId::SampleFlag), r.now));
    OpenSettings(r);
    EnterRow(r, MenuId::SettingsKeep);
    assert(r.engine.Select(Idx(MenuId::SettingsKeepToggles)));
    r.Tap(key::A);
    assert(r.engine.KeepToggles());
    const auto erase = Idx(MenuId::SettingsKeepErase);
    assert(r.engine.Select(erase));
    r.Tap(key::A);
    assert(r.engine.Dialog() == DialogKind::Erase && !r.engine.DialogYes());
    r.Tap(key::B);
    assert(r.engine.Dialog() == DialogKind::None && r.engine.FixedCount() == 1 && r.engine.FavoriteCount() == 1 &&
           r.engine.EraseCount() == 0);
    assert(r.engine.Select(erase));
    r.Tap(key::A);
    r.Tap(key::Left);
    assert(r.engine.DialogYes());
    r.Tap(key::A);
    assert(r.engine.Dialog() == DialogKind::None && r.engine.FixedCount() == 0 && r.engine.FavoriteCount() == 0);
    assert(!r.engine.KeepToggles() && r.engine.Applied(Idx(MenuId::SettingsKeepToggles)) == 0);
    assert(r.engine.Applied(Idx(MenuId::SampleAmount)) == r.engine.Def(Idx(MenuId::SampleAmount)).def);
    assert(r.engine.State(Idx(MenuId::SampleHold)).hotkey == r.engine.Def(Idx(MenuId::SampleHold)).hotkey);
    assert(r.engine.EraseCount() == 1);
    assert(r.engine.Applied(Idx(MenuId::SampleFlag)) == 1); // 効果とは別に、今の ON は残す（その起動の間）
}

// 5. 押し切るボタン: 既定は START だけ。選んだ選択肢（ボタン）が、決定でその場で効き、遮断の対象になる。
void PressThroughKeys() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    assert(r.engine.DeferKeys() == gohan::menu::key::Start);
    OpenSettings(r);
    const auto press = Idx(MenuId::SettingsPressThrough);
    assert(r.engine.Select(press));
    r.Tap(key::A);
    assert(r.engine.Ui().kind == UiKind::CheckList);
    // 選択肢の並びは A・B・X・Y・Start（ビット 0・1・2・3・4）。A と X を選ぶ（0b00101）。
    r.engine.SubmitUi(5, r.now);
    r.Frame();
    assert(r.engine.Applied(press) == 5 && !r.engine.Dirty(press));
    assert(r.engine.DeferKeys() == (gohan::menu::key::A | gohan::menu::key::X));
    // 0 にすると、押し切るボタンは無い。
    assert(r.engine.Select(press));
    r.Tap(key::A);
    r.engine.SubmitUi(0, r.now);
    r.Frame();
    assert(r.engine.Applied(press) == 0 && r.engine.DeferKeys() == 0);
}

// 6. 設定画面の行は、R の操作のリスト・固定・お気に入り・ホットキーの対象にしない。
void SettingsRowsAreNotListed() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    OpenSettings(r);
    EnterRow(r, MenuId::SettingsKeep);
    const auto toggles = Idx(MenuId::SettingsKeepToggles);
    assert(r.engine.Select(toggles));
    r.Tap(key::R);
    assert(r.engine.Ui().kind == UiKind::None);
    r.Tap(key::Y);
    assert(r.engine.Ui().kind == UiKind::None);
    assert(r.engine.Actions(toggles, nullptr) == 0);
    assert(!r.engine.SetFavorite(toggles, true) && !r.engine.SetHotkey(toggles, gohan::menu::key::ZL));
    assert(r.engine.FavoriteCount() == 0);
}

// 7. 確認の中では START が効かない。閉じてから押すと設定画面を出る。
void StartWaitsForDialog() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    OpenSettings(r);
    EnterRow(r, MenuId::SettingsKeep);
    assert(r.engine.Select(Idx(MenuId::SettingsKeepErase)));
    r.Tap(key::A);
    assert(r.engine.Dialog() == DialogKind::Erase);
    r.Tap(key::Start);
    assert(r.engine.Dialog() == DialogKind::Erase && r.engine.InSettings() && r.engine.Depth() == 1);
    r.Tap(key::B);
    assert(r.engine.Dialog() == DialogKind::None && r.engine.InSettings());
    r.Tap(key::Start);
    r.Advance(400);
    assert(!r.engine.InSettings() && r.engine.Visible());
}
} // namespace

// 描画の指示を種類と名前で探す（先頭の 1 つ）。
const Command *FindCommand(const RecordingSurface &s, Command::Kind kind, const std::string &name) {
    for (const Command &c : s.commands)
        if (c.kind == kind && c.text == name) return &c;
    return nullptr;
}

// 10. 固定中の項目の件数・0 件の行（D.12.4）。件数は「n件」で、0 件の時は灰色の帯で入れない。
void FixedCountRows() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    OpenSettings(r);
    // 0 件で開くと、固定中の項目（灰色）の行を選んでいる。件数は 0 件、説明の状態に理由。
    RecordingSurface empty = r.Draw();
    assert(empty.HasText("0件") && empty.HasText("固定している項目はありません") && empty.dropped == 0);
    assert(FindCommand(empty, Command::Slice, "disband") != nullptr); // 選んだ灰色の行は帯を灰色に替える
    assert(r.engine.SetFixed(Idx(MenuId::SampleHold), true, r.now));
    assert(r.engine.SetFixed(Idx(MenuId::SampleFlag), true, r.now));
    r.Frame();
    RecordingSurface two = r.Draw();
    assert(two.HasText("2件") && !two.HasText("固定している項目はありません") && two.dropped == 0);
    assert(FindCommand(two, Command::Slice, "disband") == nullptr);
}

// 11. 固定中の項目の中の行: 鍵は付き、選んでも ◀ ▶ を出さない（D.12.4）。対照として、一覧の中の数値の行は矢印を出す。
void FixedRowsHaveNoArrows() {
    Rig c(kMenuItems, kMenuItemCount);
    Prepare(c);
    assert(c.engine.Select(Idx(MenuId::Sample)));
    c.Tap(key::A);
    c.Advance(400);
    assert(c.engine.Select(Idx(MenuId::SampleValues)));
    c.Tap(key::A);
    c.Advance(400);
    assert(c.engine.Select(Idx(MenuId::SampleWallet)));
    c.Advance(400); // 選ぶ動きが終わってから描く（選んだ行の印と矢印は、動きの後に出る）
    assert(c.Draw().HasSprite("arrow_l")); // 対照: 一覧の数値の行は左右で値が変わるので矢印を出す

    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    assert(r.engine.SetFixed(Idx(MenuId::SampleWallet), true, r.now));
    OpenSettings(r);
    EnterRow(r, MenuId::SettingsFixed);
    assert(r.engine.Folder() == Idx(MenuId::SettingsFixed) && r.engine.Select(Idx(MenuId::SampleWallet)));
    r.Advance(400);
    RecordingSurface s = r.Draw();
    assert(s.HasSprite("lock") && !s.HasSprite("arrow_l") && !s.HasSprite("arrow_r") && s.dropped == 0);
}

// 12. 押し切るボタンの行の印（D.12.4）: 選んだボタンの印を右詰めで並べ、間を 2 画素空ける。
//     右端は値の右端から 4 画素内側、印の y は行 + 1。1 つも無ければ「なし」。選んだ行は帯の色の印。
void ButtonRowIcons() {
    namespace ca = gohan::core::design::ca2;
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    OpenSettings(r);
    const u16 row = Idx(MenuId::SettingsPressThrough);
    const int edge = ca::Row::X + ca::Row::W + ca::Row::Value::Right + ca::Row::Value::Text[0]; // 印の右端（排他）
    const int y = ca::Row::Top + 2 * ca::Row::Pitch + ca::Row::Buttons[1];                     // 押し切るボタンは 3 行目
    // 既定は START だけ。印 1 つを右詰め（右端の START は 23 画素）。
    RecordingSurface one = r.Draw();
    const Command *start = FindCommand(one, Command::Sprite, "key.paper.start");
    assert(start && start->bounds.x == edge - 23 && start->bounds.y == y && !one.HasText("なし"));
    // 5 つ全部: A・B・X・Y・START を左から、間を空けて並べる。
    assert(r.engine.SetValue(row, 31));
    r.Frame();
    RecordingSurface five = r.Draw();
    const int a = edge - (11 * 4 + 23 + 2 * 4);
    const char *names[] = {"key.paper.a", "key.paper.b", "key.paper.x", "key.paper.y", "key.paper.start"};
    const int xs[] = {a, a + 13, a + 26, a + 39, a + 52};
    for (int i = 0; i < 5; ++i) {
        const Command *c = FindCommand(five, Command::Sprite, names[i]);
        assert(c && c->bounds.x == xs[i] && c->bounds.y == y);
    }
    assert(five.dropped == 0);
    // 1 つも無ければ「なし」。選んだ行の印は帯の色。
    assert(r.engine.SetValue(row, 0));
    assert(r.engine.Select(row));
    r.Advance(400);
    RecordingSurface none = r.Draw();
    assert(none.HasText("なし") && none.dropped == 0);
    assert(r.engine.SetValue(row, 16));
    r.Frame();
    assert(r.Draw().HasSprite("key.bar.start") && !r.Draw().HasSprite("key.paper.start"));
}

// 13. 押し切るボタンの枠（D.12.4）: 字の代わりに印を並べる。選んだ行は帯の色、START は紙の色。字は出さない。
void ButtonCheckListIcons() {
    namespace ca = gohan::core::design::ca2;
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    OpenSettings(r);
    assert(r.engine.Select(Idx(MenuId::SettingsPressThrough)));
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Ui().kind == UiKind::CheckList);
    RecordingSurface s = r.Draw();
    const int x = 236 + ca::Entry::Check::Button[0];       // 窓の x から
    const int top = 4 + ca::Entry::List::Top + ca::Entry::Check::Button[1];
    const Command *a = FindCommand(s, Command::Sprite, "key.bar.a");
    const Command *start = FindCommand(s, Command::Sprite, "key.paper.start");
    assert(a && a->bounds.x == x && a->bounds.y == top);
    assert(start && start->bounds.x == x && start->bounds.y == top + 4 * ca::Entry::List::Pitch);
    assert(!s.HasText("Start") && !s.HasText("A") && s.HasText("決定") && s.HasText("取消") && s.dropped == 0);
}

// 14. 開閉の向き（D.12.4）: 一覧の深い所から START で開くと、設定画面は右から入り、一覧は左へ出る。
//     閉じる時は逆。題は旧い側も旧い道筋（GOHAN）で描き、新しい側は「設定」。
void SettingsDirection() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    assert(r.engine.Select(Idx(MenuId::Sample)));
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Select(Idx(MenuId::SampleValues)));
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Depth() == 2);
    r.Hold(key::Start);
    const u32 press = r.now;
    r.in.held = 0;
    r.now = press + 60;
    r.shell.Step(r.now, r.in, false);
    assert(r.engine.InSettings());
    RecordingSurface mid = r.Draw();
    assert(mid.HasText("GOHAN") && mid.HasText("設定") && mid.dropped == 0);
    const Command *entering = mid.FindText("固定中の項目");
    r.Advance(400);
    RecordingSurface rest = r.Draw();
    const Command *settled = rest.FindText("固定中の項目");
    assert(entering && settled && entering->bounds.x > settled->bounds.x); // 右から入る
    assert(!rest.HasText("GOHAN"));

    // 閉じる: 設定画面は右へ出て、一覧は左から戻る。
    r.Hold(key::Start);
    const u32 close = r.now;
    r.in.held = 0;
    r.now = close + 60;
    r.shell.Step(r.now, r.in, false);
    assert(!r.engine.InSettings() && r.engine.Depth() == 2);
    RecordingSurface back = r.Draw();
    const Command *returning = back.FindText("見本の所持金");
    r.Advance(400);
    RecordingSurface main = r.Draw();
    const Command *main_rest = main.FindText("見本の所持金");
    assert(returning && main_rest && returning->bounds.x < main_rest->bounds.x);
}

int main() {
    OpenCloseAndPosition();
    FixedListRules();
    EmptyFixedListIsSelectable();
    XAndLDoNothingInSettings();
    KeepToggles();
    EraseEverything();
    PressThroughKeys();
    SettingsRowsAreNotListed();
    StartWaitsForDialog();
    FixedCountRows();
    FixedRowsHaveNoArrows();
    ButtonRowIcons();
    ButtonCheckListIcons();
    SettingsDirection();
    std::puts("menu settings: ok");
    return 0;
}

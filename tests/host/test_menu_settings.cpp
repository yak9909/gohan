// 設定画面（段 3）を、実際のエンジン・Shell で確かめる。
// 開閉と位置（START・B・SELECT・同じ起動中の位置）、固定中の項目（順・R で外す・A は決定・選択の寄せ・0 件は選べて入れない）、
// 固定中の設定の値（左右・X で適用）、設定画面のお気に入り（0 件・並べ替えと外す・親へ戻る）、
// 設定画面の行の中の X・L（効かない）、
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

// 2. 固定中の項目: 固定した順に並ぶ。行は元の項目で、一覧と同じく A は決定（固定は外れない）・左右・X・L・Y・R が効く。
//    外すのは R の「固定を外す」だけ。選択は次の行（最後なら前の行）。最後の 1 件を外したら親へ戻る。0 件の時は灰色で入れない。
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
    // A は決定: トグルの ON/OFF を編集するだけで、固定は外れない（適用待ちは L で戻す）。
    assert(r.engine.Selected() == Idx(MenuId::SampleHold));
    r.Tap(key::A);
    assert(r.engine.State(Idx(MenuId::SampleHold)).fixed && r.engine.FixedCount() == 2 && r.engine.Dirty(Idx(MenuId::SampleHold)));
    // 適用待ちがある時に閉じようとすると、閉じる時の確認が出る（一覧と同じ）。B で取り消すと残る。
    r.Tap(key::Select);
    assert(r.engine.Dialog() == DialogKind::Close);
    r.Tap(key::B);
    assert(r.engine.Dialog() == DialogKind::None && r.engine.Visible() && r.engine.Dirty(Idx(MenuId::SampleHold)));
    r.Tap(key::L);
    assert(!r.engine.Dirty(Idx(MenuId::SampleHold)) && r.engine.State(Idx(MenuId::SampleHold)).fixed);
    // Y は一覧と同じくホットキーの割り当てを開く（行は元の項目なので、普通の項目と同じ）。
    r.Tap(key::Y);
    assert(r.engine.Ui().kind == UiKind::Key && r.engine.Ui().item == Idx(MenuId::SampleHold));
    r.Tap(key::Select); // キーの枠は SELECT で取り消す
    assert(r.engine.Ui().kind == UiKind::None);
    // R の「固定を外す」で外す: 行が消え、選択は次の行（今は最後の行）。
    assert(r.engine.Selected() == Idx(MenuId::SampleHold));
    r.Tap(key::R);
    assert(r.engine.Ui().kind == UiKind::Actions);
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.FixedCount() == 1 && !r.engine.State(Idx(MenuId::SampleHold)).fixed &&
           r.engine.Selected() == Idx(MenuId::SampleFlag));
    // 最後の行を外す: 並びは [SampleFlag, SampleHold] に戻す。最後の SampleHold を外すと、選択は前の SampleFlag へ。
    assert(r.engine.SetFixed(Idx(MenuId::SampleHold), true, r.now));
    r.Tap(key::Down);
    assert(r.engine.Selected() == Idx(MenuId::SampleHold));
    r.Tap(key::R);
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.FixedCount() == 1 && !r.engine.State(Idx(MenuId::SampleHold)).fixed &&
           r.engine.Selected() == Idx(MenuId::SampleFlag));
    // 先頭の SampleFlag を外す（残りは SampleHold）: 選択は残った行へ。
    assert(r.engine.SetFixed(Idx(MenuId::SampleHold), true, r.now));
    r.Tap(key::Down);
    assert(r.engine.Selected() == Idx(MenuId::SampleHold));
    r.Tap(key::Up);
    assert(r.engine.Selected() == Idx(MenuId::SampleFlag));
    r.Tap(key::R);
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.FixedCount() == 1 && r.engine.Selected() == Idx(MenuId::SampleHold));
    // 残りを全て外すと、フォルダは灰色になり、入れない。一番上へ戻り、「固定中の項目」の行を選んだままにする。
    r.Tap(key::R);
    r.Tap(key::A);
    r.Advance(400);
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

// 2d. 固定中の項目の中の設定の値: 左右で変わり、X で適用すると固定値が新しくなる（固定は外れない）。
//     固定中の項目の中の数値は ◀ ▶ を出す（一覧と同じ）。
void FixedSettingValueRules() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    const auto amount = Idx(MenuId::SampleAmount);
    assert(r.engine.SetFixed(amount, true, r.now));
    OpenSettings(r);
    EnterRow(r, MenuId::SettingsFixed);
    assert(r.engine.Select(amount));
    r.Advance(400);
    assert(r.Draw().HasSprite("arrow_l") && r.Draw().HasSprite("arrow_r") && r.Draw().HasSprite("lock"));
    const s32 before = r.engine.State(amount).value;
    r.Tap(key::Right);
    assert(r.engine.State(amount).value > before && r.engine.Dirty(amount) && r.engine.State(amount).fixed);
    r.Tap(key::X);
    assert(!r.engine.Dirty(amount) && r.engine.State(amount).fixed && r.engine.State(amount).fixed_value == r.engine.Applied(amount));
    assert(r.engine.Applied(amount) > before);
}

// 2e. 設定画面のお気に入り（段 5b）: 0 件の時は灰色で入れず、件数は 0件・説明は「お気に入りはありません」。
//     1 件以上なら、一番上のお気に入りと同じ並びを共有し、中で R の「お気に入りから外す」・「並べ替え」が効く。
//     最後の 1 件を外したら親へ戻り、「お気に入り」の行を選ぶ。B で戻る。
void SettingsFavoritesRules() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    const auto flag = Idx(MenuId::SampleFlag), hold = Idx(MenuId::SampleHold);
    const auto favorites = Idx(MenuId::SettingsFavorites);
    // 0 件: 灰色で入れない。選ぶことはできる。説明は「お気に入りはありません」。
    OpenSettings(r);
    assert(r.engine.Folder() == Idx(kSettingsRoot) && r.engine.Disabled(favorites));
    assert(r.engine.Select(favorites)); // 灰色でもカーソルは置ける
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Depth() == 0 && r.engine.Selected() == favorites);
    r.Frame();
    assert(r.Draw().HasText("お気に入りはありません") && r.Draw().HasText("0件"));
    // 2 件にする。一番上のお気に入りと同じ並び（追加した順）。
    assert(r.engine.SetFavorite(flag, true) && r.engine.SetFavorite(hold, true));
    r.Frame();
    assert(!r.engine.Disabled(favorites) && r.Draw().HasText("2件"));
    assert(r.engine.Select(favorites));
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Depth() == 1 && r.engine.Folder() == favorites);
    u16 rows[8]{};
    assert(r.engine.Children(favorites, rows, 8) == 2 && rows[0] == flag && rows[1] == hold);
    assert(r.Draw().HasText("設定 > お気に入り"));
    // R の「お気に入りから外す」（hold は操作の 2 番目）: 選択は次の行。
    assert(r.engine.Select(hold));
    r.Tap(key::R);
    r.Tap(key::Down);
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.FavoriteCount() == 1 && !r.engine.IsFavorite(hold) && r.engine.Folder() == favorites);
    // 最後の 1 件を外す: 親へ戻り、「お気に入り」の行を選ぶ（選択は今の行の動き）。
    r.Tap(key::R);
    r.Tap(key::Down);
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.FavoriteCount() == 0 && r.engine.Depth() == 0 && r.engine.Selected() == favorites);
    assert(r.engine.Disabled(favorites));
    // 同じ起動中に閉じて開き直しても、空のお気に入りには入らない（崩れない）。
    r.Tap(key::Start);
    r.Advance(400);
    OpenSettings(r);
    assert(r.engine.Depth() == 0 && r.engine.Selected() == favorites);
    // 1 件にして入り、B で親へ戻る。
    assert(r.engine.SetFavorite(flag, true));
    r.Frame();
    assert(r.engine.Select(favorites));
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Depth() == 1 && r.engine.Folder() == favorites);
    r.Tap(key::B);
    r.Advance(400);
    assert(r.engine.InSettings() && r.engine.Depth() == 0 && r.engine.Folder() == Idx(kSettingsRoot));
}

// 2f. 固定中の項目の位置を、外から外しても崩れない（同じ起動中の位置は D.8）。
void RememberedPositionSurvivesOutsideChanges() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    assert(r.engine.SetFixed(Idx(MenuId::SampleHold), true, r.now));
    assert(r.engine.SetFixed(Idx(MenuId::SampleFlag), true, r.now));
    OpenSettings(r);
    EnterRow(r, MenuId::SettingsFixed);
    assert(r.engine.Select(Idx(MenuId::SampleFlag)));
    r.Tap(key::Start);
    r.Advance(400);
    assert(!r.engine.InSettings());
    assert(r.engine.SetFixed(Idx(MenuId::SampleFlag), false, r.now)); // 一覧の外（ホットキーなど）で外す
    OpenSettings(r);
    assert(r.engine.InSettings() && r.engine.Depth() == 1 && r.engine.Folder() == Idx(MenuId::SettingsFixed));
    assert(r.engine.Selected() == Idx(MenuId::SampleHold));
    r.Frame();
    assert(r.Draw().dropped == 0);
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
    r.Tap(key::Down); // 次は「お気に入り」（0 件でも選べる）
    assert(r.engine.Selected() == Idx(MenuId::SettingsFavorites));
    r.Tap(key::Down);
    assert(r.engine.Selected() == Idx(MenuId::SettingsKeep));
    r.Tap(key::Up);
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

// 5b. 押し切るボタンの枠（apply = immediate・K4）: 枠の A で切り替えた時点でその場で効く（適用値・DeferKeys・版の数）。
//     X は何もしない。B は閉じるだけで、それまでの切り替えは戻さない。下の帯は 切替・閉じる。
void PressThroughFrameApplies() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    OpenSettings(r);
    const auto press = Idx(MenuId::SettingsPressThrough);
    assert(r.engine.Select(press));
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Ui().kind == UiKind::CheckList && r.shell.Modal());
    // 既定は START（ビット 4）。カーソルは A の所: A を加えると、枠を開いたままその場で効く。
    const u32 before = r.engine.Revision();
    r.Tap(key::A);
    assert(r.engine.Ui().kind == UiKind::CheckList && r.shell.Modal());
    assert(r.engine.Applied(press) == 17 && !r.engine.Dirty(press));
    assert(r.engine.DeferKeys() == (gohan::menu::key::A | gohan::menu::key::Start));
    assert(r.engine.Revision() == before + 1);
    // X は何もしない（枠は開いたまま、値も版の数も変わらない）。
    r.Tap(key::X);
    assert(r.engine.Ui().kind == UiKind::CheckList && r.engine.Applied(press) == 17 && r.engine.Revision() == before + 1);
    // 2 つ下の X を加える。帯は 切替・閉じる（決定と取消は無い）。
    r.Tap(key::Down);
    r.Tap(key::Down);
    r.Tap(key::A);
    RecordingSurface frame = r.Draw();
    assert(frame.HasText("切替") && frame.HasText("閉じる") && !frame.HasText("決定") && !frame.HasText("取消") && frame.dropped == 0);
    assert(r.engine.Applied(press) == 21 && r.engine.DeferKeys() == (gohan::menu::key::A | gohan::menu::key::X | gohan::menu::key::Start));
    // B で閉じる。それまでの切り替えは戻らない（設定画面に残り、版の数も残る）。
    r.Tap(key::B);
    r.Advance(400);
    assert(r.engine.Ui().kind == UiKind::None && !r.shell.Modal() && r.engine.InSettings());
    assert(r.engine.Applied(press) == 21 && !r.engine.Dirty(press) && r.engine.Revision() == before + 2);
    assert(r.engine.DeferKeys() == (gohan::menu::key::A | gohan::menu::key::X | gohan::menu::key::Start));
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
// 2g. 設定画面のお気に入りの中の「並べ替え」（段 5b の採点の直し）: 上下と A で並びが変わり、一番上のお気に入りの並びも同じになる。
//     B なら並びは変わらない（版の数も変わらない）。
void SettingsFavoritesReorder() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    const auto flag = Idx(MenuId::SampleFlag), hold = Idx(MenuId::SampleHold), stamp = Idx(MenuId::SampleStamp);
    assert(r.engine.SetFavorite(flag, true) && r.engine.SetFavorite(hold, true) && r.engine.SetFavorite(stamp, true));
    OpenSettings(r);
    EnterRow(r, MenuId::SettingsFavorites);
    assert(r.engine.Folder() == Idx(MenuId::SettingsFavorites));
    // hold の R の操作（OFFで固定・お気に入りから外す・並べ替え）: 「並べ替え」は 3 番目。
    assert(r.engine.Select(hold));
    r.Tap(key::R);
    r.Tap(key::Down);
    r.Tap(key::Down);
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Ui().kind == UiKind::Reorder);
    // 上へ 1 つ（hold と flag を入れ替える）。A で決める。
    r.Tap(key::Up);
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Ui().kind == UiKind::None);
    assert(r.engine.FavoriteCount() == 3 && r.engine.FavoriteOrder()[0] == hold && r.engine.FavoriteOrder()[1] == flag &&
           r.engine.FavoriteOrder()[2] == stamp);
    // 一番上のお気に入りの並びも同じ（並びは共有）。
    u16 top[8]{};
    assert(r.engine.Children(gohan::menu::kFavoritesFolder, top, 8) == 3 && top[0] == hold && top[1] == flag && top[2] == stamp);
    u16 inside[8]{};
    assert(r.engine.Children(Idx(MenuId::SettingsFavorites), inside, 8) == 3 && inside[0] == hold && inside[2] == stamp);
    // B で取り消す: flag を下へ動かしてから B。並びも版の数も変わらない。
    const auto revision = r.engine.Revision();
    assert(r.engine.Select(flag));
    r.Tap(key::R);
    r.Tap(key::Down);
    r.Tap(key::Down);
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Ui().kind == UiKind::Reorder);
    r.Tap(key::Down);
    r.Tap(key::B);
    r.Advance(400);
    assert(r.engine.Ui().kind == UiKind::None);
    assert(r.engine.FavoriteOrder()[0] == hold && r.engine.FavoriteOrder()[1] == flag && r.engine.FavoriteOrder()[2] == stamp);
    assert(r.engine.Revision() == revision && r.engine.Folder() == Idx(MenuId::SettingsFavorites));
}

// 2h. 設定画面のお気に入りの中のフォルダ（段 5b の採点の直し）: 設定 > お気に入り > 見本の値 に入り、B で 設定 > お気に入り へ戻る。
//     同じ起動中に閉じて開いても、深い所の位置を保つ。
void SettingsFavoritesFolderPath() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    const auto values = Idx(MenuId::SampleValues), amount = Idx(MenuId::SampleAmount), favorites = Idx(MenuId::SettingsFavorites);
    assert(r.engine.SetFavorite(values, true));
    OpenSettings(r);
    EnterRow(r, MenuId::SettingsFavorites);
    assert(r.engine.Depth() == 1 && r.engine.Folder() == favorites && r.engine.Selected() == values);
    // 見本の値（フォルダ）に入る。道筋は 設定 > お気に入り > 見本の値。
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Depth() == 2 && r.engine.Folder() == values);
    assert(r.Draw().HasText("設定 > お気に入り > 見本の値"));
    // 中の行を選んで、B で お気に入り へ戻る（道筋は 設定 > お気に入り）。
    assert(r.engine.Select(amount));
    r.Advance(400);
    r.Tap(key::B);
    r.Advance(400);
    assert(r.engine.InSettings() && r.engine.Depth() == 1 && r.engine.Folder() == favorites);
    assert(r.Draw().HasText("設定 > お気に入り") && !r.Draw().HasText("設定 > お気に入り > 見本の値"));
    // もう一度入って、深い所（見本の値の中の金額）を選ぶ。
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Depth() == 2 && r.engine.Folder() == values && r.engine.Select(amount));
    // SELECT でメニューごと閉じて、開き直すと同じ行（同じ起動中の位置）。
    r.Tap(key::Select);
    assert(!r.engine.Visible());
    r.Open();
    assert(!r.engine.InSettings() && r.engine.Visible());
    OpenSettings(r);
    assert(r.engine.Depth() == 2 && r.engine.Folder() == values && r.engine.Selected() == amount);
    // START で閉じて開き直しても、同じ位置。
    r.Tap(key::Start);
    r.Advance(400);
    OpenSettings(r);
    assert(r.engine.Depth() == 2 && r.engine.Folder() == values && r.engine.Selected() == amount);
    // 空になったら、お気に入りの行へ寄せる（崩れない）。
    r.Tap(key::B);
    r.Advance(400);
    assert(r.engine.SetFavorite(values, false) && r.engine.FavoriteCount() == 0);
    assert(r.engine.Depth() == 0 && r.engine.Selected() == favorites && r.engine.Disabled(favorites));
}
} // namespace

// 描画の指示を種類と名前で探す（先頭の 1 つ）。
const Command *FindCommand(const RecordingSurface &s, Command::Kind kind, const std::string &name) {
    for (const Command &c : s.commands)
        if (c.kind == kind && c.text == name) return &c;
    return nullptr;
}
// 字の描画指示を、文字と位置で探す（影と本体を区別するため）。
bool HasTextAt(const RecordingSurface &s, const std::string &text, int x, int y) {
    for (const Command &c : s.commands)
        if (c.kind == Command::Text && c.text == text && c.bounds.x == x && c.bounds.y == y) return true;
    return false;
}
// 字の描画指示を、文字と色で探す。
bool HasTextWithColor(const RecordingSurface &s, const std::string &text, u32 color) {
    for (const Command &c : s.commands)
        if (c.kind == Command::Text && c.text == text && c.color == color) return true;
    return false;
}
// 下の帯の語（字の高さが帯の字の行にあるもの）。
const Command *BarWord(const RecordingSurface &s, const std::string &word) {
    namespace ca = gohan::core::design::ca2;
    for (const Command &c : s.commands)
        if (c.kind == Command::Text && c.text == word && c.bounds.y == ca::Bar::Rect[1] + ca::Bar::Changed[1]) return &c;
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
    // 0 件の行（K3）: 選んでいても字・件数・指しは選んでいない時と同じ色で、影は付けない。
    namespace ca = gohan::core::design::ca2;
    const int lx = ca::Row::X + ca::Row::Label[0], ly = ca::Row::Top + ca::Row::Label[1];
    assert(HasTextAt(empty, "固定中の項目", lx, ly));
    assert(!HasTextAt(empty, "固定中の項目", lx + ca::Color::Sel::ShadowOffset[0], ly + ca::Color::Sel::ShadowOffset[1]));
    assert(HasTextWithColor(empty, "固定中の項目", ca::Color::Disabled::Text));
    assert(HasTextWithColor(empty, "0件", ca::Color::Sub));
    assert(FindCommand(empty, Command::Sprite, "chevron")->color == ca::Color::Text);
    assert(r.engine.SetFixed(Idx(MenuId::SampleHold), true, r.now));
    assert(r.engine.SetFixed(Idx(MenuId::SampleFlag), true, r.now));
    r.Frame();
    RecordingSurface two = r.Draw();
    assert(two.HasText("2件") && !two.HasText("固定している項目はありません") && two.dropped == 0);
    assert(FindCommand(two, Command::Slice, "disband") == nullptr);
    // 件数は指しの左へ、設定の件数の画素の分だけ空けて置く（お気に入りの件数と同じ位置）。
    // 選択中の行は影を先に描くので、本文は同じ字の最後の描画指示。
    const Command *count = nullptr;
    for (const Command &c : two.commands)
        if (c.kind == Command::Text && c.text.find("2件") != std::string::npos) count = &c;
    const Command *chevron = FindCommand(two, Command::Sprite, "chevron");
    assert(count && chevron && count->bounds.x + count->bounds.w == chevron->bounds.x - ca::Settings::Count);
    // 0 件の行を選んでいない時（保持設定を選ぶ）も、字・件数・指しは同じ色（K3）。
    assert(r.engine.SetFixed(Idx(MenuId::SampleHold), false, r.now));
    assert(r.engine.SetFixed(Idx(MenuId::SampleFlag), false, r.now));
    assert(r.engine.Select(Idx(MenuId::SettingsKeep)));
    r.Advance(400);
    RecordingSurface away = r.Draw();
    assert(!away.HasText("2件") && away.HasText("0件") && away.dropped == 0);
    assert(HasTextAt(away, "固定中の項目", lx, ly) && HasTextWithColor(away, "固定中の項目", ca::Color::Disabled::Text));
    assert(HasTextWithColor(away, "0件", ca::Color::Sub) && FindCommand(away, Command::Sprite, "chevron")->color == ca::Color::Text);
}

// 11. 固定中の項目の中の行: 鍵は付き、選んでも ◀ ▶ を出さない（D.12.4）。対照として、一覧の中の数値の行は矢印を出す。
void FixedRowsShowArrows() {
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
    assert(r.engine.SetFixed(Idx(MenuId::SampleFlag), true, r.now));
    OpenSettings(r);
    EnterRow(r, MenuId::SettingsFixed);
    assert(r.engine.Folder() == Idx(MenuId::SettingsFixed) && r.engine.Select(Idx(MenuId::SampleWallet)));
    r.Advance(400);
    RecordingSurface s = r.Draw();
    // 固定中の数値は左右で値が変わるので、◀ ▶ を出す（段 5b の直し）。
    assert(s.HasSprite("lock") && s.HasSprite("arrow_l") && s.HasSprite("arrow_r") && s.dropped == 0);
    // 固定中のトグルは左右では変わらないので、選んでも矢印は出さない。
    assert(r.engine.Select(Idx(MenuId::SampleFlag)));
    r.Advance(400);
    RecordingSurface toggle = r.Draw();
    assert(toggle.HasSprite("lock") && !toggle.HasSprite("arrow_l") && !toggle.HasSprite("arrow_r") && toggle.dropped == 0);
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
    const int y = ca::Row::Top + 3 * ca::Row::Pitch + ca::Row::Buttons[1];                     // 押し切るボタンは 4 行目（固定中の項目・お気に入りの次）
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
    assert(none.HasText("なし") && none.dropped == 0 && !none.HasSprite("arrow_l") && !none.HasSprite("arrow_r")); // 選んでも ◀ ▶ は出さない
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
    // 下の帯の印（A など）は枠より先に描くので、枠の印は x で探す。
    const Command *a = nullptr, *start = nullptr;
    for (const Command &c : s.commands) {
        if (c.kind == Command::Sprite && c.text == "key.bar.a" && c.bounds.x == x) a = &c;
        if (c.kind == Command::Sprite && c.text == "key.paper.start" && c.bounds.x == x) start = &c;
    }
    assert(a && a->bounds.x == x && a->bounds.y == top);
    assert(start && start->bounds.x == x && start->bounds.y == top + 4 * ca::Entry::List::Pitch);
    // apply = immediate の枠の下の帯は 上下 えらぶ・A 切替・B 閉じる（決定と取消は無い。K4）。
    assert(!s.HasText("Start") && !s.HasText("A") && s.HasText("切替") && s.HasText("閉じる") && !s.HasText("決定") && !s.HasText("取消") && s.dropped == 0);
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

// 15. 下の帯の語の位置（D.12.5）。一覧の値の行は 25・65・97・137・177・217・257・309（START 設定まで）、n変更は 366。
//     設定画面は A・B・START の 25・65・117 で、n変更は出さない。固定中の項目の中は A が「外す」。
void BarWordPositions() {
    Rig r(kMenuItems, kMenuItemCount);
    Prepare(r);
    // 数値の行（左右で値）を選ぶ。Sample > SampleValues の中にある。
    assert(r.engine.Select(Idx(MenuId::Sample)));
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Select(Idx(MenuId::SampleValues)));
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Select(Idx(MenuId::SampleAmount)) && r.engine.Depth() == 2);
    r.Advance(400);
    const RecordingSurface list = r.Draw();
    const char *words[] = {"決定", "値", "戻る", "適用", "戻し", "キー", "操作", "設定"};
    const int xs[] = {25, 65, 97, 137, 177, 217, 257, 309};
    for (int i = 0; i < 8; ++i) {
        const Command *c = BarWord(list, words[i]);
        assert(c && c->bounds.x == xs[i]);
    }
    assert(list.FindText("変更") && list.FindText("変更")->bounds.x == 366);
    OpenSettings(r);
    assert(r.engine.Select(Idx(MenuId::SettingsKeep)));
    r.Frame();
    const RecordingSurface root = r.Draw();
    assert(BarWord(root, "決定") && BarWord(root, "決定")->bounds.x == 25);
    assert(BarWord(root, "戻る") && BarWord(root, "戻る")->bounds.x == 65);
    assert(BarWord(root, "閉じる") && BarWord(root, "閉じる")->bounds.x == 117);
    assert(!root.FindText("変更") && !root.HasText("値"));
    // 固定中の項目の中: 一覧と同じ案内（A は決定・R は操作・START は閉じる）。n変更も出す（段 5b）。
    // 閉じるは n変更と重ならない位置にある。
    assert(r.engine.SetFixed(Idx(MenuId::SampleHold), true, r.now));
    EnterRow(r, MenuId::SettingsFixed);
    assert(r.engine.Folder() == Idx(MenuId::SettingsFixed));
    r.Frame();
    const RecordingSurface fixed = r.Draw();
    assert(BarWord(fixed, "決定") && BarWord(fixed, "決定")->bounds.x == 25);
    assert(BarWord(fixed, "戻る") && BarWord(fixed, "閉じる"));
    assert(BarWord(fixed, "外す") == nullptr && fixed.FindText("変更")); // 下の帯に「外す」は出さない（説明の窓の「外すまで」とは別）
    const Command *close = BarWord(fixed, "閉じる"), *changed = fixed.FindText("変更");
    assert(close && changed && close->bounds.x + close->bounds.w < changed->bounds.x);
}

// 16. 段 3b の直し（D.12.4）: enabled_if で灰色になった普通のフォルダは、灰色の行と同じ字の影を付け、指しは灰色の行の色（ink）。
//     固定中の項目（0 件）の見た目は 10 のとおり（選んでも色と影は変えない）。試験の表は小さく手で書く（見本の表は変えない）。
void DisabledFolderLooksLikeDisabledRow() {
    namespace ca = gohan::core::design::ca2;
    ItemDef defs[3]{};
    // 0: 灰色のフォルダ（1 が ON の時だけ有効）。子は 2。
    defs[0].key = "test.box";
    defs[0].label = "灰色の箱";
    defs[0].desc = "";
    defs[0].control = Control::Folder;
    defs[0].parent = kNoItem;
    defs[0].first_child = 2;
    defs[0].next = 1;
    defs[0].cond = Cond{1, CondOp::Eq, 1};
    // 1: 切り替え（既定 OFF）。
    defs[1].key = "test.switch";
    defs[1].label = "切り替え";
    defs[1].desc = "";
    defs[1].control = Control::Toggle;
    defs[1].behavior = Behavior::State;
    defs[1].persist = true;
    defs[1].min = 0;
    defs[1].max = 1;
    defs[1].step = 1;
    defs[1].parent = kNoItem;
    defs[1].first_child = kNoItem;
    defs[1].next = kNoItem;
    // 2: 箱の中の行。
    defs[2].key = "test.inner";
    defs[2].label = "中";
    defs[2].desc = "";
    defs[2].control = Control::Toggle;
    defs[2].behavior = Behavior::State;
    defs[2].persist = true;
    defs[2].min = 0;
    defs[2].max = 1;
    defs[2].step = 1;
    defs[2].parent = 0;
    defs[2].first_child = kNoItem;
    defs[2].next = kNoItem;
    Rig r(defs, 3);
    r.Open();
    assert(r.engine.Selected() == 1); // 灰色の箱は入れないので、一番上の選べる行（切り替え）
    assert(r.engine.Select(0) && r.engine.Disabled(0)); // 灰色でもカーソルは置ける（一覧と同じ）
    r.Advance(400);
    const int lx = ca::Row::X + ca::Row::Label[0], ly = ca::Row::Top + ca::Row::Label[1];
    RecordingSurface grey = r.Draw();
    const Command *chevron = FindCommand(grey, Command::Sprite, "chevron");
    assert(chevron && chevron->color == ca::Color::Disabled::Sel::Text); // 指しは灰色の行の色（段 3b より前と同じ）
    assert(HasTextAt(grey, "灰色の箱", lx, ly));
    assert(HasTextAt(grey, "灰色の箱", lx + ca::Color::Sel::ShadowOffset[0], ly + ca::Color::Sel::ShadowOffset[1])); // 字の影は付ける
    assert(grey.dropped == 0);
    // 対照: 条件が成り立つと（切り替えを ON）、普通の選択中のフォルダの色と影になる。
    assert(r.engine.Select(1));
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.State(1).value == 1);
    assert(r.engine.Select(0) && !r.engine.Disabled(0));
    r.Advance(400);
    RecordingSurface plain = r.Draw();
    const Command *open = FindCommand(plain, Command::Sprite, "chevron");
    assert(open && open->color == ca::Color::Sel::Text);
    assert(HasTextAt(plain, "灰色の箱", lx + ca::Color::Sel::ShadowOffset[0], ly + ca::Color::Sel::ShadowOffset[1]));
}

int main() {
    OpenCloseAndPosition();
    FixedListRules();
    FixedSettingValueRules();
    SettingsFavoritesRules();
    RememberedPositionSurvivesOutsideChanges();
    EmptyFixedListIsSelectable();
    XAndLDoNothingInSettings();
    KeepToggles();
    EraseEverything();
    PressThroughKeys();
    PressThroughFrameApplies();
    SettingsRowsAreNotListed();
    StartWaitsForDialog();
    FixedCountRows();
    FixedRowsShowArrows();
    SettingsFavoritesReorder();
    SettingsFavoritesFolderPath();
    ButtonRowIcons();
    ButtonCheckListIcons();
    SettingsDirection();
    BarWordPositions();
    DisabledFolderLooksLikeDisabledRow();
    std::puts("menu settings: ok");
    return 0;
}

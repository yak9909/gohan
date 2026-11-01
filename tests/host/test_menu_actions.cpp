// R の操作のリスト（段 2b）を、実際の Shell・エンジン・描画の指示で確かめる。
// 開閉・入力（上下の折り返しとリピート、A・B・R・Y、受け付けないボタン）・灰色・閉じる途中の R・重なりの順・描画の予算。
#include "menu/paint.hpp"
#include "menu/sample_items.hpp"
#include "menu_ids.hpp"
#include "menu_rig.hpp"
#include <cstdio>

using namespace menu_test;

namespace {
u16 Idx(MenuId id) { return static_cast<u16>(id); }
void Budget(const RecordingSurface &s) { assert(s.dropped == 0 && s.texts <= int(gohan::core::limits::RendererTexts) && s.rects <= int(gohan::core::limits::RendererRects)); }

void Enter(Rig &r) { r.Open(); r.Tap(key::A); r.Advance(400); }
// 所持金は「見本の値」の中にあるので、そのフォルダへ入ってから選ぶ。
void EnterValues(Rig &r) {
    assert(r.engine.Select(Idx(MenuId::SampleValues)));
    r.Tap(key::A);
    r.Advance(400);
}

Notice MakeNotice(const char *title, const char *message) {
    Notice notice{};
    std::snprintf(notice.title, sizeof(notice.title), "%s", title);
    std::snprintf(notice.message, sizeof(notice.message), "%s", message);
    return notice;
}

// 操作のリストの窓（幅 160 の notice.window。通知は幅 150）。
const Command *ListWindow(const RecordingSurface &s) {
    for (const auto &c : s.commands)
        if (c.kind == Command::Slice && c.text == "notice.window" && c.bounds.w == 160) return &c;
    return nullptr;
}
// 名前と x の箱（説明の窓は x 236 の window）。
const Command *Slice(const RecordingSurface &s, const char *name, int x) {
    for (const auto &c : s.commands)
        if (c.kind == Command::Slice && c.text == name && c.bounds.x == x) return &c;
    return nullptr;
}
// 選んだ行の番号。指（x 242）の y から、窓の題（22）と行送り（14）で数える。
int Cursor(const RecordingSurface &s) {
    const Command *window = ListWindow(s);
    for (const auto &c : s.commands)
        if (c.kind == Command::Sprite && c.text == "finger" && c.bounds.x == 242 && window)
            return (c.bounds.y - 3 - (window->bounds.y + 22)) / 14;
    return -1;
}
// 名前の印の数（x が min_x 以上のもの。一覧の印は x ≥ 236、行の印と混ざらないように分ける）。
int SpritesNamed(const RecordingSurface &s, const char *name, int min_x = 0) {
    int n = 0;
    for (const auto &c : s.commands) n += c.kind == Command::Sprite && c.text == name && c.bounds.x >= min_x;
    return n;
}
// 字の位置（最初と最後）。同じ字が一覧の行と操作のリストの題に出る時は、後ろ（最後）を使う。
int IndexOfText(const RecordingSurface &s, const char *needle, bool last = false) {
    int found = -1;
    for (std::size_t i = 0; i < s.commands.size(); ++i)
        if (s.commands[i].kind == Command::Text && s.commands[i].text.find(needle) != std::string::npos) {
            found = int(i);
            if (!last) break;
        }
    return found;
}
int IndexOfListWindow(const RecordingSurface &s) {
    for (std::size_t i = 0; i < s.commands.size(); ++i)
        if (s.commands[i].kind == Command::Slice && s.commands[i].text == "notice.window" && s.commands[i].bounds.w == 160) return int(i);
    return -1;
}
int IndexOfNoticeWindow(const RecordingSurface &s) {
    for (std::size_t i = 0; i < s.commands.size(); ++i)
        if (s.commands[i].kind == Command::Slice && s.commands[i].text == "notice.window" && s.commands[i].bounds.w == 150) return int(i);
    return -1;
}

// 開く（R）。開いた後の 200 ms で、開く動きは終わる。
void OpenList(Rig &r, u16 item) {
    assert(r.engine.Select(item));
    r.Tap(key::R);
    assert(r.engine.Ui().kind == UiKind::Actions && r.shell.Modal());
    r.Advance(200);
}

// 開いた一覧: 開くとカーソルは最初の使える操作。説明の窓は描かず、下の帯は R のリストの案内になる。
void OpenAndDraw() {
    sample::Get() = sample::State{};
    Rig r(kMenuItems, kMenuItemCount);
    assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    Enter(r);
    OpenList(r, Idx(MenuId::SampleFlag));
    auto s = r.Draw(); Budget(s);
    // 見本のフラグ（状態・ホットキーあり）: OFF で固定 → お気に入りに追加 → ホットキーを変更（Y）→ ホットキーを外す。
    assert(s.HasText("OFFで固定") && s.HasText("お気に入りに追加") && s.HasText("ホットキーを変更") && s.HasText("ホットキーを外す"));
    assert(!s.HasText("値を固定") && !s.HasText("お気に入りから外す"));
    assert(Cursor(s) == 0);
    // 印: 一覧の鍵（OFF で固定）・星（お気に入り）・ホットキーの三角（一覧の行の印。行の印は hotkey.selected）。
    // 印（一覧の範囲 x ≥ 236。選んだ行の鍵は白の影付きで 2 枚、ホットキーの行は 2 つ、星は 1 つ）。
    assert(SpritesNamed(s, "lock", 236) == 2 && SpritesNamed(s, "star", 236) == 1 && SpritesNamed(s, "hotkey", 236) == 2);
    assert(SpritesNamed(s, "key.paper.y", 236) == 1); // Y の近道はホットキーの行だけ
    assert(s.HasText("見本のフラグ") && Slice(s, "band3", 236 + 3) != nullptr);
    // 説明の窓は描かない。下の帯は R のリストの案内（n変更は出さない）。
    assert(Slice(s, "window", 236) == nullptr);
    assert(s.HasText("えらぶ") && s.HasText("決定") && s.HasText("閉じる"));
    assert(!s.HasText("戻る") && !s.HasText("1変更") && !s.HasText("0変更"));
    // 窓の位置と高さ: 選んだ行（y 28）の y − 3。高さは 22 + 14 × 4 + 6。
    const Command *window = ListWindow(s);
    assert(window && window->bounds.x == 236 && window->bounds.y == 25 && window->bounds.h == 22 + 14 * 4 + 6);
}

// 上下: 押した瞬間は端で折り返し、リピートでは端で止まる。
void Movement() {
    sample::Get() = sample::State{};
    Rig r(kMenuItems, kMenuItemCount);
    assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    Enter(r);
    OpenList(r, Idx(MenuId::SampleFlag));
    r.Tap(key::Up); assert(Cursor(r.Draw()) == 3); // 先頭で押すと末尾へ折り返す
    r.Tap(key::Down); assert(Cursor(r.Draw()) == 0); // 末尾で押すと先頭へ折り返す
    r.Hold(key::Down); // 押し続けると 1 つずつ進み、末尾で止まる（折り返さない）
    r.Advance(1200);
    assert(Cursor(r.Draw()) == 3 && r.engine.Ui().kind == UiKind::Actions);
    r.Release();
    r.Hold(key::Up); r.Advance(1200); // 先頭でも同じく止まる
    assert(Cursor(r.Draw()) == 0);
    r.Release();
    assert(r.engine.Ui().kind == UiKind::Actions);
}

// 灰色の操作: カーソルは置けるが、A でも Y でも閉じず何もしない。
void GrayRows() {
    sample::Get() = sample::State{};
    sample::Get().wallet_readable = false; // 読めないゲームの値は、値を固定・ホットキーの操作が灰色
    Rig r(kMenuItems, kMenuItemCount);
    assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    Enter(r);
    EnterValues(r);
    OpenList(r, Idx(MenuId::SampleWallet));
    // 最初の使える操作は「お気に入りに追加」（2 行目）。
    auto s = r.Draw(); Budget(s);
    assert(Cursor(s) == 1 && s.HasText("値を固定") && s.HasText("お気に入りに追加") && s.HasText("ホットキーを設定"));
    r.Tap(key::Up); // 灰色の「値を固定」へ
    s = r.Draw();
    assert(Cursor(s) == 0 && Slice(s, "disband", 236 + 19) != nullptr);
    r.Tap(key::A); r.Advance(16);
    assert(r.engine.Ui().kind == UiKind::Actions && r.shell.Modal()); // 閉じない
    assert(!r.engine.State(Idx(MenuId::SampleWallet)).fixed);
    r.Tap(key::Y); r.Advance(16); // 灰色の「ホットキーを設定」も何もしない
    assert(r.engine.Ui().kind == UiKind::Actions && r.shell.Modal());
    r.Tap(key::B); r.Advance(400);
    assert(r.engine.Ui().kind == UiKind::None && !r.shell.Modal());
}

// A で実行して閉じる（1 回だけ）。実行の後の通知は、お気に入りの追加と固定の 1 件ずつ。
void RunOnce() {
    sample::Get() = sample::State{};
    Rig r(kMenuItems, kMenuItemCount);
    assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    Enter(r);
    EnterValues(r);
    const u16 wallet = Idx(MenuId::SampleWallet);
    OpenList(r, wallet);
    assert(Cursor(r.Draw()) == 0 && r.engine.FavoriteCount() == 0);
    r.Tap(key::Down); // 「お気に入りに追加」
    assert(Cursor(r.Draw()) == 1);
    r.Tap(key::A);
    assert(r.engine.IsFavorite(wallet) && r.engine.FavoriteCount() == 1);
    assert(r.engine.Ui().kind == UiKind::None && !r.shell.Modal());
    r.Advance(400); // 閉じる動きの後も、A は 1 回しか効かない
    assert(r.engine.FavoriteCount() == 1);
    auto s = r.Draw(); Budget(s);
    assert(s.HasText("お気に入りに追加") && s.HasText("見本の所持金"));
    // 固定（値）も A で 1 回。
    OpenList(r, wallet);
    assert(Cursor(r.Draw()) == 0 && r.Draw().HasText("値を固定") && r.Draw().HasText("お気に入りから外す"));
    r.Tap(key::A);
    assert(r.engine.State(wallet).fixed && r.engine.Ui().kind == UiKind::None);
    r.Advance(400);
    assert(r.engine.State(wallet).fixed && r.engine.FavoriteCount() == 1);
}

// Y はホットキーの行を実行して割り当ての枠を開く。ホットキーの行が無い（灰色）時は何もしない。
void HotkeyShortcut() {
    sample::Get() = sample::State{};
    Rig r(kMenuItems, kMenuItemCount);
    assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    Enter(r);
    OpenList(r, Idx(MenuId::SampleFlag));
    r.Tap(key::Y); r.Advance(16);
    assert(r.engine.Ui().kind == UiKind::Key && r.shell.Modal());
    r.Tap(key::Select); r.Advance(200); // 割り当ての枠を取り消す
    assert(r.engine.Ui().kind == UiKind::None);
    // ホットキーが無い値（所持金）も、「ホットキーを設定」で割り当ての枠を開く。
    EnterValues(r);
    OpenList(r, Idx(MenuId::SampleWallet));
    assert(!r.engine.State(Idx(MenuId::SampleWallet)).hotkey);
    r.Tap(key::Y); r.Advance(16);
    assert(r.engine.Ui().kind == UiKind::Key);
    r.Tap(key::Select); r.Advance(200);
    assert(r.engine.Ui().kind == UiKind::None);
}

// 開いている間は、X・L・左右・START を受け付けない。SELECT では一覧が閉じず、B で閉じる。
void IgnoredButtons() {
    sample::Get() = sample::State{};
    Rig r(kMenuItems, kMenuItemCount);
    assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    Enter(r);
    EnterValues(r);
    const u16 wallet = Idx(MenuId::SampleWallet);
    OpenList(r, wallet);
    const s32 before = r.engine.State(wallet).value;
    for (u16 k : {key::X, key::L, key::Left, key::Right, key::Start}) {
        r.Tap(k); r.Advance(16);
        assert(r.engine.Ui().kind == UiKind::Actions && r.shell.Modal());
    }
    assert(r.engine.State(wallet).value == before && !r.engine.Dirty(wallet));
    assert(r.engine.Depth() == 2 && r.engine.Selected() == wallet); // 見本 > 見本の値 の中のまま
    r.Tap(key::Select); r.Advance(200);
    assert(r.engine.Visible() && r.engine.Ui().kind == UiKind::Actions && r.shell.Modal());
    r.Tap(key::B); r.Advance(400);
    assert(r.engine.Visible() && r.engine.Ui().kind == UiKind::None);
}

// B と R は取り消して閉じる。閉じた後は、説明の窓が戻り、操作のリストは描かれない。
void CloseByBAndR() {
    sample::Get() = sample::State{};
    Rig r(kMenuItems, kMenuItemCount);
    assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    Enter(r);
    const u16 flag = Idx(MenuId::SampleFlag);
    OpenList(r, flag);
    r.Tap(key::B); r.Advance(200);
    assert(r.engine.Ui().kind == UiKind::None && !r.shell.Modal() && !r.engine.State(flag).fixed);
    auto s = r.Draw();
    assert(ListWindow(s) == nullptr && Slice(s, "window", 236) != nullptr);
    OpenList(r, flag);
    r.Tap(key::R); r.Advance(200);
    assert(r.engine.Ui().kind == UiKind::None && !r.shell.Modal());
    assert(ListWindow(r.Draw()) == nullptr);
}

// 閉じる途中の R は、今の位相から開き直す（位置が飛ばない）。
void ReopenWhileClosing() {
    sample::Get() = sample::State{};
    Rig r(kMenuItems, kMenuItemCount);
    assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    Enter(r);
    OpenList(r, Idx(MenuId::SampleFlag));
    r.Tap(key::B); r.Advance(64); // 閉じ始めて約 80 ms。途中の窓は x 236 より左にある
    const Command *closing = ListWindow(r.Draw());
    assert(closing && closing->bounds.x > 228 && closing->bounds.x < 236);
    const int x = closing->bounds.x;
    r.Tap(key::R); // 閉じる途中に R で開き直す
    assert(r.engine.Ui().kind == UiKind::Actions && r.shell.Modal());
    const Command *again = ListWindow(r.Draw());
    assert(again && again->bounds.x >= x && again->bounds.x < 236); // 228 へ戻らず、今の位相から続く
    r.Advance(200);
    assert(ListWindow(r.Draw()) && ListWindow(r.Draw())->bounds.x == 236);
}

// 重なりの順: 一覧 → 下の帯 → 通知 → 操作のリスト。通知の上に操作のリストが重なる。
void Layering() {
    sample::Get() = sample::State{};
    Rig r(kMenuItems, kMenuItemCount);
    assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    Enter(r);
    r.shell.Notify(MakeNotice("通知の見本", "重なりの順の見本"), r.now);
    r.Advance(400);
    OpenList(r, Idx(MenuId::SampleFlag));
    const auto s = r.Draw(); Budget(s);
    const int notice_text = IndexOfText(s, "通知の見本");
    const int notice_window = IndexOfNoticeWindow(s);
    const int list_window = IndexOfListWindow(s);
    const int list_title = IndexOfText(s, "見本のフラグ", true);
    assert(notice_text >= 0 && notice_window >= 0 && list_window > notice_window);
    assert(list_title > notice_text && list_window > notice_text); // 題・窓の地とも、通知より後に描く
    assert(Slice(s, "window", 236) == nullptr);
}

// 描画の量: 通知（積む・あふれ）と操作のリストと下の帯で、上限内（捨てた指示 0）。
void WorstBudget() {
    sample::Get() = sample::State{};
    Rig r(kMenuItems, kMenuItemCount);
    assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    Enter(r);
    for (int n = 0; n < 8; ++n) r.shell.Notify(MakeNotice("通知の見本", "長い名前の見本の項目で 2 行になる物の折り返し"), r.now + n * 40u);
    r.Advance(400);
    OpenList(r, Idx(MenuId::SampleFlag));
    const auto s = r.Draw(); Budget(s);
    assert(s.dropped == 0 && s.HasText("ホットキーを変更"));
}

// D.12.7 の actions_worst に近い場面: 10 行（全行に鍵・ホットキー・適用待ち・長い名前）と通知 8 件と操作のリスト。
void WorstRows() {
    namespace menu = gohan::menu;
    static menu::ItemDef defs[11]{};
    static std::string labels[11], keys[11];
    labels[0] = "見本の並び";
    defs[0].key = "worst.root"; defs[0].label = labels[0].c_str(); defs[0].desc = "";
    defs[0].control = menu::Control::Folder; defs[0].behavior = menu::Behavior::None; defs[0].source = menu::Source::None;
    defs[0].parent = menu::kNoItem; defs[0].first_child = 1; defs[0].next = menu::kNoItem;
    for (unsigned n = 1; n <= 10; ++n) {
        char text[64];
        std::snprintf(text, sizeof(text), "とても長い名前の見本の項目 %02u", n);
        labels[n] = text;
        keys[n] = "worst." + std::to_string(n);
        auto &d = defs[n];
        d.key = keys[n].c_str(); d.label = labels[n].c_str(); d.desc = "描画の量を見る見本の項目です。";
        d.control = menu::Control::Toggle; d.behavior = menu::Behavior::State; d.source = menu::Source::None;
        d.persist = true; d.parent = 0; d.first_child = menu::kNoItem;
        d.next = n < 10 ? static_cast<menu::u16>(n + 1) : menu::kNoItem;
        d.min = 0; d.max = 1; d.step = 1;
    }
    Rig r(defs, 11);
    // 行ごとに別のホットキー（重なると割り当てられない）。奇数は ON を適用待ち、偶数は適用済み。固定は前半の 5 行。
    const u16 masks[10] = {key::L | key::Up, key::L | key::Down, key::L | key::Left, key::L | key::Right, key::L | key::A,
                           key::L | key::B, key::L | key::X, key::L | key::Y, key::R | key::Up, key::R | key::Down};
    for (u16 n = 1; n <= 10; ++n) {
        assert(r.engine.SetHotkey(n, masks[n - 1]) && r.engine.Commit(n, 0));
        assert(r.engine.SetValue(n, 1));
        if (n % 2 == 0) assert(r.engine.Commit(n, 0));
        if (n <= 5) assert(r.engine.SetFixed(n, true, 0));
    }
    r.Open();
    assert(r.engine.Select(0));
    r.Tap(key::A); // 見本の並びへ入る
    r.Advance(400);
    for (int n = 0; n < 8; ++n) r.shell.Notify(MakeNotice("通知の見本", "長い名前の項目の見本で 2 行になる物の折り返し"), r.now + n * 40u);
    assert(r.engine.Select(9));
    r.Tap(key::R);
    r.Advance(400);
    const auto s = r.Draw(); Budget(s);
    assert(s.dropped == 0 && ListWindow(s) != nullptr && s.HasText("ホットキーを変更"));
}

} // namespace

int main() {
    OpenAndDraw();
    Movement();
    GrayRows();
    RunOnce();
    HotkeyShortcut();
    IgnoredButtons();
    CloseByBAndR();
    ReopenWhileClosing();
    Layering();
    WorstBudget();
    WorstRows();
    std::printf("R の操作のリスト: 開閉・上下の折り返しとリピート・灰色・A の 1 回の実行・Y の近道・受け付けないボタン・閉じる途中の R・重なりの順・描画の予算\n");
    return 0;
}

// お気に入りの中と並べ替え（段 2c）を、実際の Shell・エンジン・描画の指示で確かめる。
// 持ち上げと上下の入れ替え（端で止まる・リピート・スクロール）・A の決定と B の取消・受け付けないボタン・
// 場所の行・題の道筋・お気に入りの中の R（外す・並べ替え）・外した後の選択・描画の予算。
#include "menu/paint.hpp"
#include "menu_rig.hpp"
#include <cstdio>
#include <string>

using namespace menu_test;
namespace menu = gohan::menu;
using menu::ActionKind;
using menu::UiKind;
using menu::u16;

namespace {
// 見本の木（ゲームには触れない）。0: 「見本の並び」（1〜12 の状態のトグル）、13: 「村」（14・15 が中）。
constexpr std::size_t kTotal = 16;
constexpr u16 kList = 0, kFirst = 1, kLast = 12, kVillage = 13, kInVillage = 14;

struct Tree {
    menu::ItemDef defs[kTotal]{};
    std::string labels[kTotal], keys[kTotal];
};

// 定義の文字列を指すので、一度作ったら動かさない（static の中で組み立てる）。
Tree &Defs() {
    static Tree tree;
    static const bool built = [] {
        Tree &t = tree;
        auto make = [&](u16 n, const char *label, menu::Control control, u16 parent, u16 first, u16 next) {
            t.labels[n] = label;
            t.keys[n] = "reorder." + std::to_string(n);
            auto &d = t.defs[n];
            d.key = t.keys[n].c_str();
            d.label = t.labels[n].c_str();
            d.desc = "並べ替えの見本の項目です。";
            d.control = control;
            d.behavior = control == menu::Control::Folder ? menu::Behavior::None : menu::Behavior::State;
            d.source = menu::Source::None;
            d.persist = true;
            d.parent = parent;
            d.first_child = first;
            d.next = next;
            d.min = 0;
            d.max = 1;
            d.step = 1;
        };
        const u16 none = menu::kNoItem;
        make(kList, "見本の並び", menu::Control::Folder, none, kFirst, kVillage);
        for (u16 n = kFirst; n <= kLast; ++n) {
            char label[32];
            std::snprintf(label, sizeof(label), "見本の項目 %02u", static_cast<unsigned>(n));
            make(n, label, menu::Control::Toggle, kList, none, n < kLast ? static_cast<u16>(n + 1) : none);
        }
        make(kVillage, "村", menu::Control::Folder, none, kInVillage, none);
        make(kInVillage, "村の中の項目", menu::Control::Toggle, kVillage, none, kInVillage + 1);
        make(kInVillage + 1, "村の中の項目 2", menu::Control::Toggle, kVillage, none, none);
        return true;
    }();
    (void)built;
    return tree;
}

const char *Label(u16 item) { return Defs().defs[item].label; }

// 捨てた指示（予算の外）と、切った字（省略記号の付いた字）が無いこと。
void Budget(const RecordingSurface &s) {
    assert(s.dropped == 0 && s.texts <= int(gohan::core::limits::RendererTexts) && s.rects <= int(gohan::core::limits::RendererRects));
    for (const auto &c : s.commands)
        if (c.kind == Command::Text) assert(c.text.find("…") == std::string::npos);
}

// 一覧の行の名前の字の y。名前は行の x 30 にあり、選んだ行の影は x 31 に別に描く。
int RowText(const RecordingSurface &s, u16 item) {
    for (const auto &c : s.commands)
        if (c.kind == Command::Text && c.text == Label(item) && c.bounds.x == 30) return c.bounds.y;
    return -1;
}
// 行 n（0 始まり）の名前の y。持ち上げた行は 2 画素上がる。
int RowY(int n, bool lifted = false) { return 31 + 18 * n - (lifted ? 2 : 0); }

const Command *Slice(const RecordingSurface &s, const char *name, int x) {
    for (const auto &c : s.commands)
        if (c.kind == Command::Slice && c.text == name && c.bounds.x == x) return &c;
    return nullptr;
}
int IndexOfSlice(const RecordingSurface &s, const char *name, int x) {
    for (std::size_t i = 0; i < s.commands.size(); ++i)
        if (s.commands[i].kind == Command::Slice && s.commands[i].text == name && s.commands[i].bounds.x == x) return int(i);
    return -1;
}
int SpritesAt(const RecordingSurface &s, const char *name, int x, int y) {
    int n = 0;
    for (const auto &c : s.commands) n += c.kind == Command::Sprite && c.text == name && c.bounds.x == x && c.bounds.y == y;
    return n;
}
int SpritesNamed(const RecordingSurface &s, const char *name) {
    int n = 0;
    for (const auto &c : s.commands) n += c.kind == Command::Sprite && c.text == name;
    return n;
}
const Command *Sprite(const RecordingSurface &s, const char *name) {
    for (const auto &c : s.commands)
        if (c.kind == Command::Sprite && c.text == name) return &c;
    return nullptr;
}
// 通常の選択の帯（持ち上げた行とは別）と指の y。行 n（0 始まり）の帯は 28 + 18n、指は帯の y に ca::Cursor::Offset[1] を足した y。
int Band(const RecordingSurface &s) {
    const Command *c = Slice(s, "selrim", 24);
    return c ? c->bounds.y : -1;
}
int Finger(const RecordingSurface &s) {
    const Command *c = Sprite(s, "finger");
    return c ? c->bounds.y : -1;
}
int BandY(int n) { return 28 + 18 * n; }
// 持ち上げた行の影（幅 184）。A・B の跳ねで字と一緒に x がずれても見つける。
bool Shadow(const RecordingSurface &s) {
    for (const auto &c : s.commands)
        if (c.kind == Command::Slice && c.text == "field" && c.bounds.w == 184) return true;
    return false;
}

// メニューを開き、お気に入りの中へ入る。お気に入りは先に付けておく。
void OpenFavorites(Rig &r) {
    r.Open();
    assert(r.engine.Select(menu::kFavoritesFolder));
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Folder() == menu::kFavoritesFolder);
}
void Favorite(Rig &r, u16 item) { assert(r.engine.SetFavorite(item, true)); }

// 行を選び、R で操作のリストを開き、並べ替えの行へ下りて A で持ち上げる。
void OpenReorder(Rig &r, u16 item) {
    assert(r.engine.Select(item));
    r.Tap(key::R);
    r.Advance(200);
    assert(r.engine.Ui().kind == UiKind::Actions);
    menu::ItemAction actions[menu::kMaxActions];
    const std::size_t count = r.engine.Actions(item, actions);
    std::size_t at = count;
    for (std::size_t n = 0; n < count; ++n)
        if (actions[n].kind == ActionKind::Reorder) at = n;
    assert(at < count); // 並べ替えは 2 件以上のお気に入りの中だけ
    // 開くとカーソルは最初の使える操作（先頭）。
    for (std::size_t n = 0; n < at; ++n) r.Tap(key::Down);
    r.Tap(key::A);
    r.Advance(200);
    assert(r.engine.Ui().kind == UiKind::Reorder && r.shell.Modal());
}

// 持ち上げの見た目: 帯は 2 画素上がり、影・▲▼・指がつく。説明の窓は「並べ替え中」。下の帯は 上下 動かす・A 決定・B 取消。
void LayoutAndLift() {
    Rig r(Defs().defs, kTotal);
    Favorite(r, 1); Favorite(r, 2); Favorite(r, 3);
    OpenFavorites(r);
    OpenReorder(r, 2);
    r.Advance(200);
    const auto s = r.Draw(); Budget(s);
    // 持ち上げた行（2 番目）の帯は 2 画素上（y 44）。影は帯の右下 (+2, +1)（y 47、幅 184）。
    assert(Slice(s, "selrim", 24) && Slice(s, "selrim", 24)->bounds.y == 28 + 18 - 2);
    assert(Slice(s, "field", 26) && Slice(s, "field", 26)->bounds.y == 28 + 18 + 1);
    assert(Slice(s, "field", 26)->bounds.w == 184 && Slice(s, "field", 26)->bounds.h == 13);
    // 指は揺らさず（x 12）、帯の上下に ▲▼（指の x + 4）。
    assert(Sprite(s, "finger") && Sprite(s, "finger")->bounds.x == 12);
    assert(SpritesAt(s, "arrow_u", 16, 28 + 18 - 2 - 1) == 1 && SpritesAt(s, "arrow_d", 16, 28 + 18 - 2 + 12) == 1);
    // ◀ ▶ は出さない（左右で値は変わらない）。
    assert(SpritesNamed(s, "arrow_l") == 0 && SpritesNamed(s, "arrow_r") == 0);
    // 持ち上げた行は、ほかの行より後に描く（重なりの順）。
    const int lifted = IndexOfSlice(s, "selrim", 24);
    for (std::size_t i = 0; i < s.commands.size(); ++i) {
        const Command &c = s.commands[i];
        if (c.kind == Command::Text && c.bounds.x == 30 && c.text != Label(2)) assert(int(i) < lifted);
    }
    assert(s.HasText("並べ替え中") && s.HasText("動かす") && s.HasText("決定") && s.HasText("取消"));
    assert(!s.HasText("変更"));
    // 題の道筋と位置（持ち上げた行が 2 番目なので 2/3）。
    assert(s.HasText("GOHAN > お気に入り") && s.HasText("2/3"));
    // 持ち上げた行の名前は持ち上げた位置に、ほかの行は元の位置に並ぶ。
    assert(RowText(s, 1) == RowY(0) && RowText(s, 3) == RowY(2));
    assert(RowText(s, 2) == RowY(1, true));
}

// 上下の入れ替え: 写しの中のとなりと入れ替える。端では止まる（折り返さない）。エンジンの並びは決めるまで変えない。
void MovesWithinCopyAndStops() {
    Rig r(Defs().defs, kTotal);
    Favorite(r, 1); Favorite(r, 2); Favorite(r, 3);
    OpenFavorites(r);
    OpenReorder(r, 2);
    r.Tap(key::Up); r.Advance(120); // 1 つ上へ: 写しは 2,1,3
    assert(r.shell.Modal() && r.engine.Ui().kind == UiKind::Reorder);
    auto s = r.Draw(); Budget(s);
    assert(RowText(s, 2) == RowY(0, true) && RowText(s, 1) == RowY(1));
    r.Tap(key::Up); r.Advance(120); // 先頭で押しても折り返さない
    s = r.Draw();
    assert(RowText(s, 2) == RowY(0, true) && RowText(s, 1) == RowY(1) && RowText(s, 3) == RowY(2));
    assert(r.engine.FavoriteCount() == 3 && r.engine.FavoriteOrder()[0] == 1 && r.engine.FavoriteOrder()[1] == 2);
    // 下へ 3 回: 2,1,3 → 1,2,3 → 1,3,2 → 1,3,2（末尾で止まる）。
    r.Tap(key::Down); r.Advance(120);
    r.Tap(key::Down); r.Advance(120);
    r.Tap(key::Down); r.Advance(120);
    s = r.Draw();
    assert(RowText(s, 1) == RowY(0) && RowText(s, 3) == RowY(1) && RowText(s, 2) == RowY(2, true));
    assert(r.engine.FavoriteOrder()[0] == 1 && r.engine.FavoriteOrder()[1] == 2 && r.engine.FavoriteOrder()[2] == 3);
}

// 押し続けると 1 つずつ進み、端で止まる（リピートも効く）。
void RepeatStopsAtEnds() {
    Rig r(Defs().defs, kTotal);
    Favorite(r, 1); Favorite(r, 2); Favorite(r, 3);
    OpenFavorites(r);
    OpenReorder(r, 1);
    r.Hold(key::Down); r.Advance(1000); // 3 件の末尾まで進み、止まる（折り返さない）
    r.Release(); r.Advance(120);
    auto s = r.Draw(); Budget(s);
    assert(RowText(s, 1) == RowY(2, true) && RowText(s, 2) == RowY(0) && r.engine.Ui().kind == UiKind::Reorder);
    r.Hold(key::Up); r.Advance(1000); // 先頭まで戻り、そこで止まる
    r.Release(); r.Advance(120);
    s = r.Draw();
    assert(RowText(s, 1) == RowY(0, true) && r.engine.Ui().kind == UiKind::Reorder);
    assert(r.engine.FavoriteOrder()[0] == 1 && r.engine.FavoriteOrder()[2] == 3); // まだ決めていない
}

// A は写しを並びにして版の数を 1 増やし、行を下ろす。
void ConfirmWritesOrder() {
    Rig r(Defs().defs, kTotal);
    Favorite(r, 1); Favorite(r, 2); Favorite(r, 3);
    OpenFavorites(r);
    OpenReorder(r, 1);
    r.Tap(key::Down); r.Advance(120);
    r.Tap(key::Down); r.Advance(120); // 2,3,1
    const u32 revision = r.engine.Revision();
    r.Tap(key::A);
    auto d = r.Draw(); Budget(d);
    assert(Shadow(d)); // 下ろす動きの途中（持ち上げた行はまだ見える）
    assert(!d.HasText("並べ替え中")); // 下ろす動きの途中でも「並べ替え中」は出さない
    r.Advance(200);
    assert(r.engine.Ui().kind == UiKind::None && !r.shell.Modal());
    assert(r.engine.FavoriteOrder()[0] == 2 && r.engine.FavoriteOrder()[1] == 3 && r.engine.FavoriteOrder()[2] == 1);
    assert(r.engine.Revision() == revision + 1);
    auto s = r.Draw(); Budget(s);
    assert(RowText(s, 2) == RowY(0) && RowText(s, 3) == RowY(1) && RowText(s, 1) == RowY(2));
    assert(!s.HasText("並べ替え中") && s.HasText("3/3"));
    // 決めた並びは、次に並べ替えを始めた時の写しにもなる。
    OpenReorder(r, 1);
    r.Tap(key::B); r.Advance(200);
    assert(r.engine.FavoriteOrder()[2] == 1 && r.engine.Revision() == revision + 1);
}

// B は写しを捨てて、持ち上げた時の並びに戻す。何も変わらない（版の数も増えない）。
void CancelChangesNothing() {
    Rig r(Defs().defs, kTotal);
    Favorite(r, 1); Favorite(r, 2); Favorite(r, 3);
    OpenFavorites(r);
    OpenReorder(r, 1);
    r.Tap(key::Down); r.Advance(120);
    r.Tap(key::Down); r.Advance(120);
    const u32 revision = r.engine.Revision();
    r.Tap(key::B);
    auto d = r.Draw(); Budget(d);
    assert(Shadow(d)); // 下ろす動きの途中（持ち上げた行はまだ見える）
    assert(!d.HasText("並べ替え中")); // 下ろす動きの途中でも「並べ替え中」は出さない
    r.Advance(200);
    assert(r.engine.Ui().kind == UiKind::None && !r.shell.Modal());
    assert(r.engine.FavoriteOrder()[0] == 1 && r.engine.FavoriteOrder()[1] == 2 && r.engine.FavoriteOrder()[2] == 3);
    assert(r.engine.Revision() == revision);
    auto s = r.Draw(); Budget(s);
    assert(RowText(s, 1) == RowY(0) && RowText(s, 2) == RowY(1) && RowText(s, 3) == RowY(2));
    assert(r.engine.Selected() == 1 && r.engine.FavoriteCount() == 3);
}

// B で取り消した後、選んだ行の帯と指は、選んだ項目の行（先頭）へ戻る。持ち上げた行を下ろす動きの後も食い違わない。
void CancelBandFollowsSelection() {
    Rig r(Defs().defs, kTotal);
    Favorite(r, 1); Favorite(r, 2); Favorite(r, 3);
    OpenFavorites(r);
    OpenReorder(r, 1);
    r.Tap(key::Down); r.Advance(120);
    r.Tap(key::Down); r.Advance(120); // 写しは 2,3,1（選んだ項目は 3 番目）
    r.Tap(key::B); r.Advance(200);
    auto s = r.Draw(); Budget(s);
    assert(r.engine.Selected() == 1 && r.engine.Ui().kind == UiKind::None);
    assert(!s.HasText("並べ替え中"));
    assert(RowText(s, 1) == RowY(0));
    assert(Band(s) == BandY(0) && Finger(s) == BandY(0) + ca::Cursor::Offset[1]);
}

// 一番上の一覧で最初のお気に入りを付けると、先頭に「お気に入り」の行が出て、選んだ行の添え字が 1 つ増える。帯と指もそこへ追う。
void TopListFavoriteMovesBand() {
    Rig r(Defs().defs, kTotal);
    r.Open();
    assert(r.engine.Select(kVillage) && r.engine.Folder() == menu::kNoItem);
    r.Advance(200); // 選ぶ動きを終えてから付ける（選んだ行の添え字だけが変わる）
    assert(Band(r.Draw()) == BandY(1));
    Favorite(r, kFirst);
    r.Advance(200);
    auto s = r.Draw(); Budget(s);
    assert(r.engine.Selected() == kVillage && r.engine.Folder() == menu::kNoItem);
    assert(RowText(s, kVillage) == RowY(2));
    assert(Band(s) == BandY(2) && Finger(s) == BandY(2) + ca::Cursor::Offset[1]);
}

// 一番上の一覧で、最後のお気に入りを外すと「お気に入り」の行が消え、選んだ行の添え字が 1 つ減る。帯と指もそこへ追う。
void TopListUnfavoriteMovesBand() {
    Rig r(Defs().defs, kTotal);
    Favorite(r, kVillage);
    r.Open();
    assert(r.engine.Select(kVillage));
    r.Advance(200);
    auto s = r.Draw(); Budget(s);
    assert(RowText(s, kVillage) == RowY(2) && Band(s) == BandY(2));
    r.Tap(key::R); r.Advance(200);
    menu::ItemAction actions[menu::kMaxActions];
    const std::size_t count = r.engine.Actions(kVillage, actions);
    std::size_t at = count;
    for (std::size_t n = 0; n < count; ++n)
        if (actions[n].kind == ActionKind::RemoveFavorite) at = n;
    assert(at < count);
    for (std::size_t n = 0; n < at; ++n) r.Tap(key::Down);
    r.Tap(key::A);
    r.Advance(400);
    assert(r.engine.Ui().kind == UiKind::None && !r.shell.Modal());
    assert(r.engine.FavoriteCount() == 0 && r.engine.Selected() == kVillage && r.engine.Folder() == menu::kNoItem);
    s = r.Draw(); Budget(s);
    assert(RowText(s, kVillage) == RowY(1));
    assert(Band(s) == BandY(1) && Finger(s) == BandY(1) + ca::Cursor::Offset[1]);
}

// 並べ替えの間は R・X・L・Y・左右・START・SELECT を受けない（モーダルに数える）。
void RejectsOtherButtons() {
    Rig r(Defs().defs, kTotal);
    Favorite(r, 1); Favorite(r, 2); Favorite(r, 3);
    OpenFavorites(r);
    OpenReorder(r, 2);
    r.Tap(key::Down); r.Advance(120); // 写しは 1,3,2（持ち上げた行は 3 番目）
    const u32 revision = r.engine.Revision();
    const u16 others[] = {key::R, key::X, key::L, key::Y, key::Left, key::Right, key::Start};
    for (u16 k : others) {
        r.Tap(k); r.Advance(16);
        assert(r.engine.Ui().kind == UiKind::Reorder && r.shell.Modal());
    }
    r.Tap(key::Select); r.Advance(200); // SELECT では閉じない（メニューの開閉も効かない）
    assert(r.engine.Visible() && r.engine.Ui().kind == UiKind::Reorder && r.shell.Modal());
    assert(r.engine.Revision() == revision && r.engine.FavoriteOrder()[1] == 2);
    auto s = r.Draw(); Budget(s);
    assert(RowText(s, 2) == RowY(2, true) && RowText(s, 3) == RowY(1));
    r.Tap(key::B); r.Advance(200);
    assert(!r.shell.Modal() && r.engine.Visible());
}

// 持ち上げた行が見える範囲の外へ動いたら、一覧と同じくスクロールする。
void ScrollsWithLiftedRow() {
    Rig r(Defs().defs, kTotal);
    for (u16 n = kFirst; n <= kLast; ++n) Favorite(r, n);
    OpenFavorites(r);
    OpenReorder(r, 11); // 11 番目（写しの 10 番目）
    r.Advance(300);
    auto s = r.Draw(); Budget(s);
    const int rows = r.shell.panel().Rows();
    const float view = r.shell.panel().ViewStart(r.now);
    assert(rows > 0 && view <= 10 && 10 < view + rows);
    for (int n = 0; n < 10; ++n) {
        r.Tap(key::Up);
        r.Advance(40);
    }
    r.Advance(300);
    assert(r.shell.panel().ViewStart(r.now) == 0.0f && r.engine.Ui().kind == UiKind::Reorder);
    for (int n = 0; n < 11; ++n) {
        r.Tap(key::Down);
        r.Advance(40);
    }
    r.Advance(300);
    s = r.Draw(); Budget(s);
    assert(r.shell.panel().ViewStart(r.now) == r.shell.panel().MaxViewStart());
    assert(r.engine.Ui().kind == UiKind::Reorder);
    r.Tap(key::B); r.Advance(300);
    assert(!r.shell.Modal() && r.engine.FavoriteOrder()[10] == 11);
}

// 場所の行: お気に入りの中の項目は「場所: 親の道筋」を状態の下に出す。最上位は「一番上の一覧」。
void LocationLine() {
    Rig r(Defs().defs, kTotal);
    Favorite(r, 1); Favorite(r, kVillage);
    OpenFavorites(r);
    assert(r.engine.Select(1));
    auto s = r.Draw(); Budget(s);
    assert(s.HasText("場所: 見本の並び") && s.HasText("GOHAN > お気に入り"));
    assert(r.engine.Select(kVillage));
    s = r.Draw(); Budget(s);
    assert(s.HasText("場所: 一番上の一覧") && !s.HasText("場所: 見本の並び"));
    // 並べ替えの間も場所の行は残り、状態は「並べ替え中」になる。
    Favorite(r, 2);
    OpenReorder(r, 1);
    s = r.Draw(); Budget(s);
    assert(s.HasText("並べ替え中") && s.HasText("場所: 見本の並び"));
    r.Tap(key::B); r.Advance(300); // 並べ替えを取り消す
    r.Tap(key::B); r.Advance(400); // お気に入りから根へ戻る
    assert(r.engine.Folder() == menu::kNoItem);
    assert(r.engine.Select(menu::kFavoritesFolder));
    s = r.Draw(); Budget(s);
    assert(!s.HasText("場所: "));
}

// 題の道筋: お気に入りの中のフォルダへ入ると「GOHAN > お気に入り > 村」。B でお気に入りへ戻る。
void HeadingPath() {
    Rig r(Defs().defs, kTotal);
    Favorite(r, 1); Favorite(r, kVillage);
    OpenFavorites(r);
    auto s = r.Draw(); Budget(s);
    assert(s.HasText("GOHAN > お気に入り") && s.HasText("1/2"));
    assert(r.engine.Select(kVillage));
    r.Tap(key::A); r.Advance(400);
    s = r.Draw(); Budget(s);
    assert(s.HasText("GOHAN > お気に入り > 村") && s.HasText("1/2"));
    assert(r.engine.Depth() == 2 && r.engine.Folder() == kVillage);
    r.Tap(key::B); r.Advance(400);
    s = r.Draw(); Budget(s);
    assert(r.engine.Folder() == menu::kFavoritesFolder && s.HasText("GOHAN > お気に入り") && !s.HasText("GOHAN > お気に入り > 村"));
}

// お気に入りの中のフォルダの行: R は「お気に入りから外す」と「並べ替え」だけ（2 件以上の時）。
void FolderRowActions() {
    Rig r(Defs().defs, kTotal);
    Favorite(r, 1); Favorite(r, kVillage);
    OpenFavorites(r);
    assert(r.engine.Select(kVillage));
    r.Tap(key::R); r.Advance(200);
    auto s = r.Draw(); Budget(s);
    assert(s.HasText("お気に入りから外す") && s.HasText("並べ替え"));
    assert(!s.HasText("値を固定") && !s.HasText("ホットキーを設定") && !s.HasText("ONで固定") && !s.HasText("OFFで固定"));
    r.Tap(key::B); r.Advance(200);
}

// 中の行で「お気に入りから外す」: 行が消え、選択は次の行（最後なら前の行）。最後の 1 件を外したら一番上へ戻る。
void RemoveFromFavorites() {
    Rig r(Defs().defs, kTotal);
    Favorite(r, 1); Favorite(r, 2); Favorite(r, 3);
    OpenFavorites(r);
    auto remove = [&](u16 item) {
        assert(r.engine.Select(item));
        r.Tap(key::R); r.Advance(200);
        menu::ItemAction actions[menu::kMaxActions];
        const std::size_t count = r.engine.Actions(item, actions);
        std::size_t at = count;
        for (std::size_t n = 0; n < count; ++n)
            if (actions[n].kind == ActionKind::RemoveFavorite) at = n;
        assert(at < count);
        for (std::size_t n = 0; n < at; ++n) r.Tap(key::Down);
        r.Tap(key::A);
        r.Advance(400);
        assert(r.engine.Ui().kind == UiKind::None && !r.shell.Modal());
    };
    remove(2); // 真ん中: 選択は次の 3
    assert(!r.engine.IsFavorite(2) && r.engine.FavoriteCount() == 2 && r.engine.Selected() == 3);
    auto s = r.Draw(); Budget(s);
    assert(RowText(s, 2) == -1 && RowText(s, 1) == RowY(0) && RowText(s, 3) == RowY(1));
    remove(3); // 最後の行: 選択は前の 1
    assert(r.engine.FavoriteCount() == 1 && r.engine.Selected() == 1 && r.engine.Depth() == 1);
    remove(1); // 最後の 1 件: 一番上の一覧へ戻り、選択は先頭（見本の並び）
    assert(r.engine.FavoriteCount() == 0 && r.engine.Depth() == 0 && r.engine.Selected() == kList);
    s = r.Draw(); Budget(s);
    assert(r.engine.FirstVisible(menu::kNoItem) == kList && s.HasText("見本の並び"));
}

// 描画の予算: お気に入りの中の 5 つの操作（ホットキーあり）と、その並べ替え。捨てた指示・切った字は 0。
void BudgetOfFiveActions() {
    Rig r(Defs().defs, kTotal);
    for (u16 n = kFirst; n < kFirst + 5; ++n) Favorite(r, n);
    assert(r.engine.SetHotkey(3, menu::key::L | menu::key::Up) && r.engine.Commit(3, 0));
    OpenFavorites(r);
    menu::ItemAction actions[menu::kMaxActions];
    assert(r.engine.Select(3) && r.engine.Actions(3, actions) == 5);
    r.Tap(key::R); r.Advance(200);
    auto s = r.Draw(); Budget(s);
    assert(s.HasText("ホットキーを変更") && s.HasText("ホットキーを外す") && s.HasText("並べ替え"));
    r.Tap(key::B); r.Advance(200);
    // 並べ替え（5 件の並び）。
    OpenReorder(r, 3);
    r.Tap(key::Up); r.Advance(200);
    s = r.Draw(); Budget(s);
    assert(s.HasText("動かす") && s.HasText("並べ替え中") && s.HasText("GOHAN > お気に入り"));
    r.Tap(key::B); r.Advance(200);
}

// 並べ替えの間と決めた時は、通知を出さない（並びが目の前で見えているので）。
void NoNoticeOnReorder() {
    Rig r(Defs().defs, kTotal);
    Favorite(r, 1); Favorite(r, 2);
    OpenFavorites(r);
    r.Advance(4000); // お気に入りに追加の通知（約 3.4 秒）を消してから数える（待ちの列は描画で押し出す）
    r.Draw();
    assert(!r.shell.notices().Alive(r.now));
    OpenReorder(r, 1);
    r.Tap(key::Down); r.Advance(120);
    r.Tap(key::A); r.Advance(300);
    r.Draw();
    assert(r.engine.FavoriteOrder()[0] == 2 && !r.shell.notices().Alive(r.now));
}

} // namespace

int main() {
    LayoutAndLift();
    MovesWithinCopyAndStops();
    RepeatStopsAtEnds();
    ConfirmWritesOrder();
    CancelChangesNothing();
    CancelBandFollowsSelection();
    TopListFavoriteMovesBand();
    TopListUnfavoriteMovesBand();
    RejectsOtherButtons();
    ScrollsWithLiftedRow();
    LocationLine();
    HeadingPath();
    FolderRowActions();
    RemoveFromFavorites();
    BudgetOfFiveActions();
    NoNoticeOnReorder();
    std::printf("お気に入りの並べ替え: 持ち上げ・上下の入れ替え（端・リピート・スクロール）・A の決定と B の取消・受け付けないボタン・場所の行・題の道筋・お気に入りの中の R・外した後の選択・描画の予算\n");
    return 0;
}

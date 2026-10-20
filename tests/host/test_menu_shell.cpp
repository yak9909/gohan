// CA2 の画面・ボタン操作・タッチの無視と通知を、実際の Shell で確かめる。
#include "menu/paint.hpp"
#include "menu/sample_items.hpp"
#include "menu_ids.hpp"
#include "menu_rig.hpp"
#include <cstdio>
#include <algorithm>
using namespace menu_test;
namespace {
u16 Idx(MenuId id) { return static_cast<u16>(id); }
void Budget(const RecordingSurface &s) { assert(s.dropped == 0 && s.texts <= 48 && s.rects <= 144); }
void Enter(Rig &r) { r.Open(); r.Tap(key::A); r.Advance(400); }
bool Part(const RecordingSurface &s, const char *name, int x = -999, int y = -999) {
    for (const auto &c : s.commands) if (c.kind != Command::Text && c.text == name && (x == -999 || c.bounds.x == x) && (y == -999 || c.bounds.y == y)) return true;
    return false;
}
void Scene() {
    sample::Get() = sample::State{};
    Rig r(kMenuItems, kMenuItemCount); assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    assert(r.Draw().commands.empty()); r.Open();
    auto s = r.Draw(); Budget(s);
    assert(Part(s, "window", 4, 4) && Part(s, "window", 236, 4));
    assert(s.HasText("GOHAN") && s.HasText("見本") && s.HasText("1/1"));
    assert(Part(s, "finger", 12, 31) || Part(s, "finger", 13, 31));
    r.TapAt(40, 40); assert(r.engine.Depth() == 0);
    r.Tap(key::A); r.Advance(400); s = r.Draw(); Budget(s);
    assert(r.engine.Depth() == 1 && r.shell.panel().Rows() == 10);
    assert(Part(s, "pill.off") && Part(s, "hotkey.selected", 23, 27));
    assert(s.HasText("見本のフラグ") && s.HasText("OFF") && s.HasText("ホットキー"));
    const u16 flag = Idx(MenuId::SampleFlag);
    r.TapAt(40, 70); assert(r.engine.Selected() == flag);
    r.Tap(key::A); s = r.Draw(); assert(Part(s, "pill.pending_on") && s.HasText("1変更"));
    r.Tap(key::X); s = r.Draw(); assert(s.HasText("待機") && Part(s, "pill.wait"));
    r.Tap(key::Down); r.Tap(key::Down); r.Advance(200); r.Tap(key::A);
    s = r.Draw(); Budget(s); assert(Part(s, "button.reserved") && s.HasText("予約"));
    r.Tap(key::X); s = r.Draw(); assert(s.HasText("OK") && sample::Get().stamps == 1);
    r.Advance(900); assert(!r.Draw().HasText("OK"));
    r.Tap(key::B); r.Tap(key::Select); r.Advance(400);
    assert(!r.engine.Visible());
}
void DialogAndHold() {
    sample::Get() = sample::State{};
    Rig r(kMenuItems, kMenuItemCount); assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount)); Enter(r);
    assert(r.engine.SetValue(Idx(MenuId::SampleFlag), 1));
    r.Hold(key::X); r.Advance(100); auto s = r.Draw(); Budget(s);
    assert(s.HasText("全適用") && s.HasText("はなすと取消"));
    bool early = false;
    for (const auto &c : s.commands) early |= c.kind == Command::Fill && c.color == ca::Color::Hold::Early;
    assert(early);
    r.Advance(300); s = r.Draw(); bool late = false;
    for (const auto &c : s.commands) late |= c.kind == Command::Fill && c.color == ca::Color::Hold::Late;
    assert(late); r.Advance(300); r.Release(); r.Advance(200); s = r.Draw(); Budget(s);
    assert(r.engine.Dialog() == DialogKind::ApplyAll && Part(s, "window", 106, 71));
    assert(s.HasText("確認") && s.HasText("全ての変更を適用しますか？"));
    r.TapAt(140, 126); assert(r.engine.Dialog() != DialogKind::None);
    r.Tap(key::B); assert(r.engine.Dialog() == DialogKind::None);
    r.Tap(key::Select); r.Advance(200); assert(r.engine.Dialog() == DialogKind::Close);
    r.Tap(key::A); r.Advance(400); assert(!r.engine.Visible() && r.engine.Applied(Idx(MenuId::SampleFlag)));
}
void Notification() {
    NoticeQueue q; RecordingSurface s;
    Notice a{}; NoticeEffect(a, "見本のフラグ", true); q.Add(a, 1000);
    NoticeFailed(a, "見本の所持金", Result::NotReady); q.Add(a, 1100);
    NoticeExecuted(a, "見本のスタンプ"); q.Add(a, 1200);
    q.Draw(s, 1800, false); Budget(s);
    int windows = 0, bottom = 0;
    for (const auto &c : s.commands) if (c.kind == Command::Slice && c.text == "window") {
        ++windows; assert(c.bounds.x == 246 && c.bounds.w == 150); bottom = std::max(bottom, c.bounds.y + c.bounds.h);
    }
    assert(windows == 3 && bottom == 236 && Part(s, "check") && Part(s, "bang"));
    assert(!q.Alive(6000));
}
void TrimRules() {
    RecordingSurface s; char b[96];
    assert(DisplayText("選ぶ / GOHAN の操作 / 離す") == "えらぶ / GOHAN のボタン / はなす");
    assert(Trim(s, "長い名前（余分", 48, b, sizeof(b)) == "長い名前…");
    char lines[2][kLineBytes]; bool truncated = false;
    assert(Wrap(s, "長い名前を折り返す本文です", 32, lines, 2, &truncated) == 2 && truncated);
    assert(std::string(lines[1]).ends_with("…"));
}
}
int main() { Scene(); DialogAndHold(); Notification(); TrimRules(); std::puts("menu shell: ok"); }

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
void Budget(const RecordingSurface &s) { assert(s.dropped == 0 && s.texts <= int(gohan::core::limits::RendererTexts) && s.rects <= int(gohan::core::limits::RendererRects)); }
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
    assert(Part(s, "switch.track") && Part(s, "hotkey.selected", 23, 27));
    assert(s.HasText("見本のフラグ") && s.HasText("OFF") && s.HasText("ホットキー"));
    const u16 flag = Idx(MenuId::SampleFlag);
    r.TapAt(40, 70); assert(r.engine.Selected() == flag);
    r.Tap(key::A); s = r.Draw(); assert(Part(s, "switch.track") && s.HasText("1変更"));
    r.Tap(key::X); s = r.Draw(); assert(s.HasText("待機") && Part(s, "switch.track"));
    r.Tap(key::Down); r.Tap(key::Down); r.Advance(200); r.Tap(key::A);
    s = r.Draw(); Budget(s); assert(Part(s, "button.reserved") && s.HasText("予約"));
    assert(s.FindText("予約:")->color == ca::Color::Sub);
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
    assert(Part(s, "button.normal"));
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
    for (const auto &c : s.commands) if (c.kind == Command::Slice && c.text == "notice.window") {
        ++windows; assert(c.bounds.x == 246 && c.bounds.w == 150); bottom = std::max(bottom, c.bounds.y + c.bounds.h);
    }
    assert(windows == 3 && bottom == 236 && Part(s, "check") && Part(s, "bang"));
    assert(!q.Alive(6000));

    NoticeQueue entering;
    NoticeEffect(a, "見本のフラグ", true); entering.Add(a, 1000);
    NoticeExecuted(a, "見本のスタンプ"); entering.Add(a, 1100);
    NoticeFailed(a, "見本の所持金", Result::NotReady); entering.Add(a, 2000);
    RecordingSurface early, settled;
    entering.Draw(early, 2048, false); entering.Draw(settled, 2400, false);
    std::vector<Rect> before, after;
    for (const auto &c : early.commands) if (c.kind == Command::Slice && c.text == "notice.window") before.push_back(c.bounds);
    for (const auto &c : settled.commands) if (c.kind == Command::Slice && c.text == "notice.window") after.push_back(c.bounds);
    assert(before.size() == 3 && after.size() == 3 && before[0].x > after[0].x);
    for (unsigned n = 0; n < 3; ++n) {
        assert(before[n].y == after[n].y && before[n].h == after[n].h);
        if (n) assert(before[n].x == after[n].x);
        if (n + 1 < 3) assert(before[n + 1].y + before[n + 1].h + ca::Notice::Gap == before[n].y);
    }
}
void TrimRules() {
    RecordingSurface s; char b[96];
    assert(DisplayText("選ぶ / GOHAN の操作 / 離す") == "えらぶ / GOHAN のボタン / はなす");
    assert(Trim(s, "GOHAN の操作", 128, b, sizeof(b)) == "GOHAN のボタン");
    char translated[4][kLineBytes];
    const int translatedCount = Wrap(s, "GOHAN の操作", 24, translated, 4, nullptr);
    std::string joined;
    for (int n = 0; n < translatedCount; ++n) joined += translated[n];
    assert(joined == "GOHAN のボタン");
    assert(Trim(s, "長い名前（余分", 48, b, sizeof(b)) == "長い名前…");
    char lines[2][kLineBytes]; bool truncated = false;
    assert(Wrap(s, "長い名前を折り返す本文です", 32, lines, 2, &truncated) == 2 && truncated);
    assert(std::string(lines[1]).ends_with("…"));
    // 句読点・閉じ括弧・長音・小書きはぶら下げ、連続する時は前の字も次行へ送る。
    for (const char *text : {"あいうえ、ホットキー。", "あいうえ。", "あいうえ」かきくけーさしすせっそ",
                             "あいうえ。」かきくけ", "あいうえゃかきくけョさしすせ）そ"}) {
        char wrapped[12][kLineBytes];
        const int n = Wrap(s, text, 32, wrapped, 12, &truncated);
        assert(!truncated);
        std::string original;
        for (int i = 0; i < n; ++i) {
            const std::string_view line(wrapped[i]);
            assert(!line.empty() && s.Measure(line) <= 40);
            assert(std::string_view(ca::Trim::LineStartForbidden).find(line.substr(0, 3)) == std::string_view::npos);
            original += line;
        }
        assert(original == text);
    }
    char hung[2][kLineBytes];
    assert(Wrap(s, "あいうえ。", 32, hung, 2, &truncated) == 1 && !truncated);
    assert(std::string(hung[0]) == "あいうえ。");
}

void SpacingAndSelection() {
    Rig r(kMenuItems, kMenuItemCount); Enter(r);
    auto s = r.Draw();
    const char *keys[] = {"key.bar.a", "key.bar.b", "key.bar.x", "key.bar.l", "key.bar.y"};
    const char *words[] = {"決定", "戻る", "適用", "戻し", "キー"};
    constexpr int iconX[] = {12, 52, 92, 130, 172};
    for (unsigned n = 0; n < 5; ++n) {
        const Command *icon = nullptr;
        for (const auto &c : s.commands) if (c.text == keys[n]) icon = &c;
        const Command *word = nullptr;
        for (const auto &c : s.commands) if (c.kind == Command::Text && c.text == words[n]) word = &c;
        assert(icon && word);
        const int width = n == 3 ? 13 : 11;
        assert(word->bounds.x == icon->bounds.x + width + 2);
        assert(icon->bounds.x == iconX[n]);
    }
    for (bool bar : {false, true}) {
        RecordingSurface icons;
        assert(KeyIcons(icons, 0, 0, key::L | key::Up, bar) == 33);
        assert(icons.commands.size() == 3);
        assert(icons.commands[0].bounds.x == 0 && icons.commands[1].bounds.x == 15 && icons.commands[2].bounds.x == 22);
    }
    r.Hold(key::Down); s = r.Draw();
    const auto *moving = s.FindText("見本の押下中");
    assert(moving && moving->color == ca::Color::Text);
    int copies = 0;
    for (const auto &c : s.commands) copies += c.text == "見本の押下中" && c.bounds.x < ca::List::Rect[2];
    assert(copies == 1);
    r.Release(); r.Advance(150); s = r.Draw();
    assert(s.FindText("見本の押下中")->color == ca::Color::Sel::Shadow);
}

void DisabledAndViewport() {
    ItemDef defs[24];
    for (u16 n = 0; n < 24; ++n) {
        defs[n] = kMenuItems[Idx(MenuId::SampleAmount)];
        defs[n].parent = defs[n].first_child = kNoItem;
        defs[n].next = n < 23 ? n + 1 : kNoItem;
        defs[n].cond = {0, CondOp::Eq, -999};
    }
    Rig r(defs, 24); r.engine.Open(r.now); r.engine.Select(13); r.Frame(); r.Advance(400);
    auto s = r.Draw(); Budget(s);
    assert(!Part(s, "arrow_l") && !Part(s, "arrow_r"));
    int rows = 0, rules = 0;
    for (const auto &c : s.commands) {
        if (c.clipped) assert(c.clip.y + c.clip.h == 203);
        if (c.clipped && c.kind == Command::Text && c.bounds.x == ca::Row::X + ca::Row::Label[0]) ++rows;
        if (c.clipped && c.kind == Command::Pattern && c.text == "dash") { ++rules; assert(c.bounds.y < 203); }
        if (c.clipped) assert(c.bounds.y < 203);
    }
    assert(rows == 10 && rules == 9);
}
void ValueGap() {
    ItemDef def = kMenuItems[Idx(MenuId::SampleAmount)];
    def.label = "見本の名前を長く長く長く長くした行";
    def.parent = def.first_child = def.next = kNoItem; def.cond = {};
    def.format = Format::Decimal; def.def = 9999; def.min = 0; def.max = 99999;
    Rig r(&def, 1); r.Open(); const auto s = r.Draw(); Budget(s);
    const Command *label = nullptr, *value = s.FindText("9999");
    for (const auto &c : s.commands) if (c.kind == Command::Text && c.bounds.x == ca::Row::X + ca::Row::Label[0] && c.bounds.y == ca::Row::Top + ca::Row::Label[1]) label = &c;
    assert(label && value && Part(s, "arrow_l"));
    assert(label->bounds.x + label->bounds.w <= value->bounds.x + ca::Row::Value::ArrowLeft - ca::Row::Gap::Min);
}

void DeviceCorrections() {
    Rig r(kMenuItems, kMenuItemCount); Enter(r);
    const u16 flag = Idx(MenuId::SampleFlag);
    auto knob = [](const RecordingSurface &s) {
        for (const auto &c : s.commands) if (c.text == "switch.knob") return c.bounds.x;
        return -1;
    };
    const int off = knob(r.Draw());
    r.engine.SetValue(flag, 1); r.Frame(); const int start = knob(r.Draw());
    r.Advance(32); const auto fading = r.Draw();
    int wordAt = -1, knobAt = -1;
    for (unsigned n = 0; n < fading.commands.size(); ++n) {
        const auto &c = fading.commands[n];
        if (c.kind == Command::Text && c.bounds.y == ca::Row::Top + ca::Row::Label[1] && c.text == "OFF") {
            wordAt = n; assert((c.color >> 24) > 0 && (c.color >> 24) < 255);
            assert(c.bounds.x == ca::Row::X + ca::Row::W + ca::Row::Value::Right - ca::Row::Pill::Size[0] + ca::Row::Pill::Text[0] + (ca::Row::Pill::Text[2] - c.bounds.w) / 2);
        }
        if (c.text == "switch.knob" && knobAt < 0) knobAt = n;
    }
    assert(wordAt >= 0 && knobAt > wordAt);
    r.Advance(16); const auto middle = r.Draw(); const int mid = knob(middle);
    r.Advance(160); const int on = knob(r.Draw());
    assert(start == off && off < mid && mid < on);
    r.engine.SetValue(flag, 0); r.Frame();
    assert(knob(r.Draw()) == on); r.Advance(48);
    assert(off < knob(r.Draw()) && knob(r.Draw()) < on);
    r.engine.DiscardAll(); r.engine.RequestClose(r.now); r.Frame(0);
    auto closing = r.Draw();
    assert(Part(closing, "bar", ca::Bar::Rect[0], ca::Bar::Rect[1]));
    r.Advance(48); closing = r.Draw();
    for (const auto &c : closing.commands) if (c.text == "bar")
        assert(c.bounds.x == ca::Bar::Rect[0] && (c.color >> 24) > 0 && (c.color >> 24) < 255);
    r.Advance(256); closing = r.Draw();
    for (const auto &c : closing.commands) if (c.text == "window" && c.bounds.w == ca::List::Rect[2])
        assert(c.bounds.x + c.bounds.w <= 0);

    // 閉じた入力状態でも、画面内を退場中の字は新しい状態へフェードする。
    Rig leaving(kMenuItems, kMenuItemCount); Enter(leaving);
    leaving.engine.RequestClose(leaving.now); leaving.Frame(0);
    assert(leaving.engine.SetValue(flag, 1)); leaving.Frame(0);
    assert(leaving.Draw().HasText("OFF") && !leaving.Draw().HasText("ON"));
    leaving.Advance(80); const auto duringExit = leaving.Draw();
    assert(duringExit.HasText("ON"));
    const auto *newWord = duringExit.FindText("ON");
    assert((newWord->color >> 24) > 0 && (newWord->color >> 24) < 255);

    NoticeQueue q; Notice a{}; NoticeExecuted(a, "見本のスタンプ");
    q.Add(a, 0); RecordingSurface initial; q.Draw(initial, 500);
    const int oldY = initial.FindText("ACTION")->bounds.y;
    q.Add(a, 600); RecordingSurface beginning, halfway, end;
    q.Draw(beginning, 600); q.Draw(halfway, 780); q.Draw(end, 960);
    // 新しい通知の次に、古い通知が描かれる。
    auto lastY = [](const RecordingSurface &s) {
        int y = 0;
        for (const auto &c : s.commands) if (c.kind == Command::Text && c.text == "ACTION") y = c.bounds.y;
        return y;
    };
    assert(lastY(beginning) == oldY && lastY(end) < lastY(halfway) && lastY(halfway) < oldY);
    for (int i = 0; i < 8; ++i) q.Add(a, 1000 + i * 40);
    RecordingSurface seven; q.Draw(seven, 1800); Budget(seven);
    int windows = 0;
    for (const auto &c : seven.commands) if (c.text == "notice.window" && c.kind == Command::Slice) {
        ++windows; assert(c.bounds.y >= ca::Notice::Top && c.bounds.y + c.bounds.h <= ca::Notice::Bottom[0]);
    }
    assert(windows == 8); // 7件の積み上げと退場中の1件

    NoticeQueue overflow;
    for (int i = 0; i < 7; ++i) {
        Notice numbered{};
        std::snprintf(numbered.title, sizeof(numbered.title), "N%d", i + 1);
        std::snprintf(numbered.message, sizeof(numbered.message), "message %d", i + 1);
        overflow.Add(numbered, i * 200);
        RecordingSurface placed; overflow.Draw(placed, i * 200);
    }
    Notice eighth{}; std::snprintf(eighth.title, sizeof(eighth.title), "N8");
    std::snprintf(eighth.message, sizeof(eighth.message), "message 8");
    overflow.Add(eighth, 1400);
    RecordingSurface early, midway, finished;
    overflow.Draw(early, 1440); overflow.Draw(midway, 1520); overflow.Draw(finished, 1760);
    Budget(early); Budget(midway); Budget(finished);
    assert(early.FindText("N1") && midway.FindText("N1") && !finished.FindText("N1"));
    assert(early.FindText("N1")->bounds.x < midway.FindText("N1")->bounds.x);
    assert((early.FindText("N1")->color >> 24) > (midway.FindText("N1")->color >> 24));
    assert(early.FindText("N2")->bounds.y > midway.FindText("N2")->bounds.y);

    NoticeQueue timed; timed.Add(eighth, 0);
    RecordingSurface normalEarly, normalLate;
    timed.Draw(normalEarly, 3000); timed.Draw(normalLate, 3120);
    assert(normalEarly.FindText("N8") && normalLate.FindText("N8"));
    assert(normalEarly.FindText("N8")->bounds.x < normalLate.FindText("N8")->bounds.x);
    assert((normalEarly.FindText("N8")->color >> 24) > (normalLate.FindText("N8")->color >> 24));

    Rig folders(kMenuItems, kMenuItemCount);
    assert(folders.engine.BindAll(sample::kBindings, sample::kBindingCount));
    folders.Open(); folders.Hold(key::A); folders.Release();
    auto enteringFolder = folders.Draw(); Budget(enteringFolder);
    assert(enteringFolder.HasText("見本のフラグ") && enteringFolder.HasText("見本"));
    folders.Advance(64); folders.Hold(key::B); folders.Release();
    auto reversingFolder = folders.Draw(); Budget(reversingFolder);
    assert(reversingFolder.HasText("見本のフラグ") && reversingFolder.HasText("見本"));
    folders.Advance(240); assert(folders.engine.Depth() == 0 && !folders.Draw().HasText("見本のフラグ"));
}
}
int main() { Scene(); DialogAndHold(); Notification(); TrimRules(); SpacingAndSelection(); DisabledAndViewport(); ValueGap(); DeviceCorrections(); std::puts("menu shell: ok"); }

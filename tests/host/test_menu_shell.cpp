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
    r.Tap(key::B); assert(r.engine.Dialog() == DialogKind::None && !r.engine.DialogYes());
    // 取り消しは「キャンセル」を選んだ状態で閉じる。選んだ状態を見せてから、確認を退場させる。
    const Rect cancel = DialogOption(Bounds(ca::Dialog::Rect), 1);
    s = r.Draw(); Budget(s);
    assert(s.HasText("全ての変更を適用しますか？") && s.HasText("キャンセル") && Part(s, "button.normal"));
    assert(Part(s, "finger", cancel.x + ca::Cursor::Offset[0], cancel.y + ca::Cursor::Offset[1] - 1));
    r.Advance(128); s = r.Draw();
    const Command *fading = s.FindText("全ての変更を");
    assert(fading && (fading->color >> 24) > 0 && (fading->color >> 24) < 0xFF);
    r.Advance(400); assert(!r.Draw().HasText("全ての変更を"));
    r.Tap(key::Select); r.Advance(200); assert(r.engine.Dialog() == DialogKind::Close);
    // 「キャンセル」を選んだ A も同じ取り消し。「はい」の A も選んだ答えを残して退場する。
    r.Tap(key::Right); r.Tap(key::A); s = r.Draw();
    assert(r.engine.Visible() && s.HasText("変更を適用しますか？") && s.HasText("キャンセル"));
    r.Advance(400); r.Tap(key::Select); r.Advance(200); assert(r.engine.Dialog() == DialogKind::Close);
    r.Tap(key::A); assert(r.Draw().HasText("キャンセル"));
    r.Advance(400); assert(!r.engine.Visible() && r.engine.Applied(Idx(MenuId::SampleFlag)));
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
    assert(windows == 6); // 連続追加の2件は初回の積み替え待ち。既存5件と退場中の1件を描く。

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

void FolderViewport() {
    Rig r(kMenuItems, kMenuItemCount);
    assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    r.Open();
    unsigned frames = 0, moving_switches = 0, crossing_edges = 0;
    auto check = [&] {
        const auto s = r.Draw();
        std::size_t last_row = 0, finger = 0;
        for (std::size_t i = 0; i < s.commands.size(); ++i) {
            const auto &c = s.commands[i];
            if (c.text == "finger") { finger = i; assert(!c.clipped); }
            if (!c.clipped) continue;
            last_row = i;
            assert(c.clip.x == ca::Row::Clip::X[0] && c.clip.w == ca::Row::Clip::X[1] - ca::Row::Clip::X[0]);
            assert(c.clip.y == ca::Row::Top + ca::Hotkey::Mark::SelectedOffset[1]);
            assert(c.clip.y + c.clip.h == ca::Row::Top + (r.shell.panel().Rows() - 1) * ca::Row::Pitch + ca::Row::H);
            crossing_edges += c.bounds.x < c.clip.x || c.bounds.x + c.bounds.w > c.clip.x + c.clip.w;
            if (c.text != "switch.track" || c.bounds.y != ca::Row::Top + ca::Row::Pill::Y) continue;
            // 新しい選択行のスイッチと帯は、横移動中も同じ相対位置を保つ。
            const int band_x = c.bounds.x - (ca::Row::W + ca::Row::Value::Right - ca::Row::Pill::Size[0]);
            assert(Part(s, "selrim", band_x));
            moving_switches += band_x != ca::Row::X;
        }
        assert(finger > last_row);
        ++frames;
    };
    r.Hold(key::A); r.Release();
    for (unsigned t = 0; t < 224; t += 16) { check(); r.Frame(); }
    r.Hold(key::B); r.Release();
    for (unsigned t = 0; t < 224; t += 16) { check(); r.Frame(); }
    r.Hold(key::A); r.Release(); r.Advance(48); r.Hold(key::B); r.Release();
    for (unsigned t = 0; t < 224; t += 16) { check(); r.Frame(); }
    assert(moving_switches && crossing_edges);
    std::printf("フォルダの表示範囲と帯: %u コマ、切り抜き・指の前後・帯とスイッチの相対位置を確認\n", frames);
}

void NoticeIncomingRects() {
    auto notice = [](int number) {
        Notice n{};
        std::snprintf(n.title, sizeof(n.title), "N%d", number);
        std::snprintf(n.message, sizeof(n.message), "message %d", number);
        return n;
    };
    auto intersects = [](Rect a, Rect b) {
        return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
    };
    unsigned frames = 0, comparisons = 0;
    for (const u32 first_draw_delay : {0u, 80u}) {
        NoticeQueue q;
        for (int n = 1; n <= 7; ++n) {
            q.Add(notice(n), (n - 1) * 200);
            RecordingSurface placed; q.Draw(placed, (n - 1) * 200);
        }
        const u32 added = 1400, first_draw = added + first_draw_delay;
        q.Add(notice(8), added);
        for (u32 elapsed = 0; elapsed <= timing::kNoticeStackMs + timing::kNoticeEnterMs; ++elapsed) {
            RecordingSurface s; q.Draw(s, first_draw + elapsed);
            std::vector<Rect> windows;
            Rect incoming{}, current{};
            std::size_t incoming_index = 0;
            for (const auto &c : s.commands) {
                if (c.kind == Command::Slice && c.text == "notice.window") { current = c.bounds; windows.push_back(current); }
                if (c.kind == Command::Text && c.text == "N8") { incoming = current; incoming_index = windows.size() - 1; }
            }
            if (elapsed < timing::kNoticeStackMs) assert(!incoming.w);
            else {
                assert(incoming.w && incoming.y == ca::Notice::Bottom[0] - incoming.h);
                for (std::size_t i = 0; i < windows.size(); ++i) {
                    if (i == incoming_index) continue;
                    ++comparisons; assert(!intersects(incoming, windows[i]));
                }
            }
            ++frames;
        }
    }
    std::printf("新規通知の矩形: %u コマ・%u 組、ほかの通知との重なり 0\n", frames, comparisons);
}
}
void InputDemand() {
    namespace block = gohan::core::input_block;
    sample::Get() = sample::State{};
    Rig r(kMenuItems, kMenuItemCount);
    assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
    const auto want = [&] { assert(r.shell.InputDemand() == (block::Claim{block::group::Buttons, 0})); };
    const auto empty = [&] { assert(r.shell.InputDemand() == block::Claim{}); };
    empty();
    // 開く動きが始まる時点で要求する。タッチ・スライドパッド・押し切りは要求しない。
    r.shell.Toggle(r.now); want();
    r.Frame(); r.Advance(400); want();
    assert(r.engine.SetValue(Idx(MenuId::SampleFlag), 1));
    assert(!r.engine.RequestClose(r.now));
    assert(r.engine.Dialog() == DialogKind::Close); want();
    r.Tap(key::B); want();
    r.engine.DiscardAll();
    assert(r.engine.RequestClose(r.now)); empty();
    r.Frame(0); assert(r.shell.NeedsDraw(r.now)); empty(); // 退場中の描画は要求にしない。
    r.Advance(500); empty();
    // 閉じたままホットキーで確認を開く場合も要求する。
    r.Tap(key::L | key::Down);
    assert(!r.engine.Visible() && r.engine.Dialog() == DialogKind::Action); want();
    r.Tap(key::B); empty();
    assert(r.shell.NeedsDraw(r.now)); // 通知・確認の退場も遮断しない。
}
// 取り消しの写しが退場している間に新しい確認が出たら、古い写しは出さない。
void CancelCopyYields() {
    sample::Get() = sample::State{};
    Rig r(kMenuItems, kMenuItemCount); assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount)); Enter(r);
    assert(r.engine.SetValue(Idx(MenuId::SampleFlag), 1));
    r.Hold(key::X); r.Advance(100); r.Advance(300); r.Advance(300); r.Release(); r.Advance(200);
    assert(r.engine.Dialog() == DialogKind::ApplyAll);
    r.Tap(key::B); assert(!r.engine.DialogYes());
    r.Tap(key::Select); assert(r.engine.Dialog() == DialogKind::Close); // 退場の途中に新しい確認
    r.Tap(key::A); auto s = r.Draw(); Budget(s);                        // はい
    assert(!r.engine.Visible() && !s.HasText("全ての変更を") && s.HasText("変更を適用しますか？"));
}

// 閉じる途中のスクロールも固定し、開き直す時に選択と表示範囲を復元する。
void ReopenViewport() {
    ItemDef defs[26]{};
    for (u16 n = 0; n < 26; ++n) {
        auto &d = defs[n];
        d.key = "keep"; d.label = "見本"; d.desc = "説明";
        d.control = n == 0 || n == 25 ? Control::Folder : Control::Toggle;
        d.parent = n > 0 && n < 25 ? 0 : kNoItem;
        d.first_child = n == 0 ? 1 : kNoItem;
        d.next = n == 0 ? 25 : n < 24 ? n + 1 : kNoItem;
        d.dev = n >= 16 && n < 25;
    }
    Rig r(defs, 26); r.engine.SetDevVisible(true); Enter(r);
    assert(r.engine.Select(14)); r.Frame(0); r.Frame(32);
    const float start = r.shell.panel().ViewStart(r.now);
    assert(start > 4.f && start < 8.f); // 中央へ向かう途中。再計算では同じ位置にならない。
    r.Frame(0, true); assert(!r.engine.Visible());
    r.Advance(500);
    assert(r.shell.panel().ViewStart(r.now) == start);
    r.Open(); Budget(r.Draw());
    assert(r.engine.Folder() == 0 && r.engine.Selected() == 14);
    assert(r.shell.panel().ViewStart(r.now) == start);
    // 退場が終わる前の反転も同じ表示範囲から始める。
    r.Frame(0, true); r.Frame(32); r.Frame(0, true);
    assert(r.shell.panel().ViewStart(r.now) == start);
    r.Advance(400);
    // 表示項目が減った時はスクロールの上限へ寄せる。
    assert(r.engine.Select(24)); r.Frame(); r.Advance(200);
    r.Frame(0, true); r.engine.SetDevVisible(false); r.Advance(500); r.Open();
    assert(r.engine.Selected() == 15 && r.shell.panel().Count() == 15);
    assert(r.shell.panel().ViewStart(r.now) <= r.shell.panel().MaxViewStart());
    // 行数が変わっても選択が画面内に入る。
    Panel resized; resized.Sync(r.engine, r.now); resized.SetRows(4, r.now);
    const float view = resized.ViewStart(r.now);
    assert(view <= 14 && 14 < view + resized.Rows());
    assert(r.engine.RequestClose(r.now)); resized.Sync(r.engine, r.now);
    resized.SetRows(2, r.now); r.engine.Open(r.now); resized.Sync(r.engine, r.now);
    assert(resized.ViewStart(r.now) <= 14 && 14 < resized.ViewStart(r.now) + resized.Rows());
    r.Tap(key::B); assert(r.engine.Selected() == 0);
    r.Advance(400); r.Frame(0, true); r.Advance(400); r.Open();
    assert(r.engine.Depth() == 0 && r.engine.Selected() == 0);
}

// B、キャンセルの A、実行の A は操作案内を保ったまま同じ位相で退場する。
void DialogExitHints() {
    unsigned frames = 0;
    for (bool menu_open : {false, true}) for (unsigned answer = 0; answer < 3; ++answer) {
        sample::Get() = sample::State{};
        Rig r(kMenuItems, kMenuItemCount);
        assert(r.engine.BindAll(sample::kBindings, sample::kBindingCount));
        if (menu_open) {
            Enter(r); assert(r.engine.SetValue(Idx(MenuId::SampleFlag), 1));
            r.Hold(key::X); r.Advance(timing::kHoldMs); r.Release();
        } else r.Tap(key::L | key::Down);
        assert(r.engine.Dialog() != DialogKind::None); r.Advance(200);
        if (answer == 1) r.Tap(key::Right);
        r.Hold(answer == 0 ? key::B : key::A);
        const u32 closed = r.now;
        assert(r.engine.Dialog() == DialogKind::None);
        for (u32 elapsed = 0; elapsed <= timing::kSelectionMoveMs + timing::kDialogMs; ++elapsed) {
            r.Frame(elapsed ? 1 : 0);
            const auto s = r.Draw(); Budget(s);
            const auto *hint = s.FindText("えらぶ");
            const auto *title = s.FindText("確認");
            const auto footer = [&](const char *word) {
                return std::any_of(s.commands.begin(), s.commands.end(), [&](const Command &c) {
                    return c.kind == Command::Text && c.text == word && c.bounds.y == ca::Bar::Rect[1] + ca::Bar::Changed[1];
                });
            };
            assert(r.now == closed + elapsed);
            if (elapsed < timing::kSelectionMoveMs + timing::kDialogMs) {
                // 最後の数 ms は 8bit の不透明度が 0 に丸まるので、同時に消えればよい。
                assert(bool(hint) == bool(title));
                if (hint && title) {
                    assert((hint->color >> 24) == (title->color >> 24));
                    assert(!footer("戻る") && !footer("キー") && !footer("1変更") && !footer("0変更"));
                }
            } else {
                assert(!hint && !title);
                if (menu_open) assert(footer("戻る"));
                else assert(!Part(s, "bar") && !footer("戻る") && !footer("キー"));
            }
            ++frames;
        }
        if (menu_open) assert(r.engine.Applied(Idx(MenuId::SampleFlag)) == (answer == 2 ? 1 : 0));
        else assert(sample::Get().stamps == (answer == 2 ? 1u : 0u));
    }
    std::printf("確認の退場中の操作案内: %u コマ、内容・不透明度・消える時刻を確認\n", frames);
}
int main() { Scene(); DialogAndHold(); CancelCopyYields(); DialogExitHints(); ReopenViewport(); Notification(); TrimRules(); SpacingAndSelection(); DisabledAndViewport(); ValueGap(); DeviceCorrections(); FolderViewport(); NoticeIncomingRects(); InputDemand(); std::puts("menu shell: ok"); }

// P4a-1: 下画面の土台の純粋な部分（WriteLog・VtableOverride・bclyt 節編集・Lease の段・入力注入・ListModel・十字の送り・trace）。
#include "core/trace.hpp"
#include "game/lyt/bclyt_edit.hpp"
#include "game/vtable_override.hpp"
#include "game/write_log.hpp"
#include "ui/lease/lease_machine.hpp"
#include "ui/lease/bottom_screen_lease.hpp"
#include "game/lyt/arc_layout.hpp"
#include "game/scene_guard.hpp"
#include "game/heap.hpp"
#include "ui/lists/list_model.hpp"
#include "ui/native_input.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace gohan;
using gohan::ui::LeaseCommand;
using gohan::ui::LeaseStage;

static void put32(std::vector<std::uint8_t> &v, std::uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(x >> (8 * i)); }
static std::vector<std::uint8_t> Section(const char *tag, const char *name, float x) {
    std::vector<std::uint8_t> s(tag, tag + 4);
    put32(s, 8 + 0x44);
    std::vector<std::uint8_t> body(0x44, 0);
    std::strncpy(reinterpret_cast<char *>(&body[4]), name, 16);
    std::memcpy(&body[28], &x, 4);
    s.insert(s.end(), body.begin(), body.end());
    return s;
}
static std::vector<std::uint8_t> File(int n) {
    std::vector<std::uint8_t> f = {'C', 'L', 'Y', 'T', 0xFF, 0xFE, 0x14, 0x00, 0, 0, 0, 0};
    put32(f, 0);
    f.push_back(n); f.push_back(0); f.push_back(0); f.push_back(0);
    for (int i = 0; i < n; ++i) { auto s = Section("pan1", i ? "B" : "A", 1.0f); f.insert(f.end(), s.begin(), s.end()); }
    const std::uint32_t total = f.size(); std::memcpy(&f[0x0C], &total, 4);
    return f;
}

static void TestWriteLog() {
    std::uintptr_t a = 1, b = 2, c = 3;
    game::WriteLog<2> log;
    assert(log.Write(&a, 10) && log.Write(&b, 20));
    assert(!log.Write(&c, 30) && c == 3);          // 容量超えは書かない
    assert(a == 10 && b == 20 && log.Count() == 2);
    log.Write(nullptr, 1);
    log.Restore();
    assert(a == 1 && b == 2 && log.Count() == 0);
    // 同じ欄を 2 回書いても逆順で元へ戻る
    game::WriteLog<4> l2; std::uintptr_t d = 5;
    l2.Write(&d, 6); l2.Write(&d, 7); l2.Restore(); assert(d == 5);
}

static void TestVtable() {
    std::uintptr_t storage[32]; for (int i = 0; i < 32; ++i) storage[i] = 98 + i;
    auto *original = storage + 2;
    std::uintptr_t object_word = reinterpret_cast<std::uintptr_t>(original);
    game::VtableOverride<30> ov;
    assert(ov.Install(&object_word, original));
    assert(ov.Set(13, 1) && ov.Set(16, 2) && ov.Set(24, 3) && ov.Set(25, 4) && !ov.Set(30, 9));
    assert(ov.Activate() && ov.Active());
    const auto *copy = reinterpret_cast<const std::uintptr_t *>(object_word);
    assert(copy != original && copy[0] == 100 && copy[1] == 101 && copy[13] == 1 && copy[25] == 4 && copy[12] == 112);
    assert(copy[-2] == 98 && copy[-1] == 99);
    assert(original[13] == 113);                     // 元の表は触らない
    ov.Restore();
    assert(object_word == reinterpret_cast<std::uintptr_t>(original) && !ov.Active());
}

static void TestBclyt() {
    using game::lyt::BclytEdit;
    const auto raw = File(2);
    BclytEdit e;
    assert(e.Load(raw.data(), raw.size()) && e.SectionCount() == 2 && e.TotalSize() == raw.size());
    assert(e.Find("pan1") == 0 && e.Find("pan1", 1) == 1 && e.Find("pan1", 2) == -1 && e.Find("txt1") == -1);
    assert(e.FindByName("B") == 1 && e.FindByName("Z") == -1);
    const int n = e.Duplicate(0, "Copy");
    assert(n == 1 && e.SectionCount() == 3 && e.FindByName("Copy") == 1 && e.FindByName("A") == 0);
    assert(!e.Rename(0, "0123456789012345678901234"));        // 長すぎる名前は断る
    assert(e.SetRect(n, 1, 2, 3, 4));
    float r[4]; assert(e.GetRect(n, r) && r[0] == 1 && r[3] == 4);
    assert(e.GetRect(0, r) && r[0] == 1.0f);                  // 複製元は変わらない
    assert(!e.SetU32(n, 0x42, 1) && e.SetU32(n, 0x3C, 7));
    const auto out = e.Build();
    assert(out.size() == raw.size() + 8 + 0x44 && e.TotalSize() == out.size());
    BclytEdit again; assert(again.Load(out.data(), out.size()) && again.SectionCount() == 3);
    // 壊れた入力
    auto bad = raw; bad[0] = 'X'; assert(!again.Load(bad.data(), bad.size()));
    bad = raw; bad[0x10] = 3; assert(!again.Load(bad.data(), bad.size()));          // 節の数が合わない
    bad = raw; bad[0x14 + 4] = 0x44; assert(!again.Load(bad.data(), bad.size()));   // 節の大きさが合わない
    bad = raw; bad.pop_back(); assert(!again.Load(bad.data(), bad.size()));         // 全体の大きさが合わない
    assert(!again.Load(raw.data(), 8) && !again.Load(nullptr, 0));
    assert(again.Build().empty());
    bad = raw; bad[5] = 0; assert(!again.Load(bad.data(), bad.size()));
    bad = raw; bad[18] = 1; assert(!again.Load(bad.data(), bad.size()));
}

static ui::LeaseProbe Idle() {
    ui::LeaseProbe p; p.room = 1; p.owners = 1; p.tab_settled = true; p.commands_consumed = true;
    p.map_present = true; p.resources_returned = true; return p;
}

// 全段とメニュー閉鎖待ちで同じ復帰経路を試す。
static ui::LeaseMachine AtStage(unsigned stage, ui::LeaseProbe &p) {
    ui::LeaseMachine m; p = Idle();
    if (stage == 4) { p.menu_open = p.menu_entered = true; }
    if (stage == 0) { m.Step(p); return m; }
    m.Step(p); m.Step(p);
    if (stage == 4 || stage == 1) return m;
    p.map_present = false; p.hide_done = true;
    m.Step(p); m.Step(p);
    if (stage == 2) return m;
    p.owners = 0; assert(m.Step(p) == LeaseCommand::Restore);
    return m;
}
static void TestLease() {
    for (unsigned stage = 0; stage < 5; ++stage) {
        for (unsigned event = 0; event < 5; ++event) {
            ui::LeaseProbe p; auto m = AtStage(stage, p);
            if (event == 0) {
                p.game_restored = true;
                if (stage == 4) { p.menu_gone = true; p.menu_open = false; }
            } else if (event == 1) p.owners = 0;
            else if (event == 2) p.chat_over = true;
            else if (event == 3) { ++p.room; p.available = false; }
            else m.Shutdown();
            bool restore = false;
            for (unsigned i = 0; i < ui::LeaseMachine::TimeoutFrames + 3; ++i) {
                if (event == 3 && i == 2) p.available = true;
                const auto c = m.Step(p);
                if (event == 3 && i < 2) assert(c != LeaseCommand::Restore);
                if (event == 0) {
                    assert(c != LeaseCommand::Restore);
                    if (m.Stage() == LeaseStage::Shown) break;
                } else {
                    if (c == LeaseCommand::Restore) { restore = true; break; }
                    if (stage == 0) break; // まだ隠していないので何も戻さない。
                }
            }
            if (event == 0) assert(m.Stage() == LeaseStage::Shown && !m.Requested());
            else if (stage != 0) assert(restore);
        }
    }
    // 返却待ちは復帰命令を出さず hidden を落として取り外しを促す。
    ui::LeaseProbe p; auto m = AtStage(1, p);
    p.owners = 0; p.resources_returned = false;
    for (unsigned i = 0; i < 150; ++i) assert(m.Step(p) == LeaseCommand::None);
    assert(!m.Hidden() && !m.Requested() && !m.Drained());
    p.resources_returned = true;
    assert(m.Step(p) == LeaseCommand::Restore && m.Drained());
    p.restore_done = true; m.Step(p); assert(m.Restored());
    // 期限切れは閉鎖待ち・退場待ち・復帰待ちで同じ Restore。再試行は期限ごと。
    for (unsigned stage : {1u, 3u, 4u}) {
        auto timed = AtStage(stage, p);
        bool restored = false;
        for (unsigned i = 0; i <= ui::LeaseMachine::TimeoutFrames; ++i)
            if (timed.Step(p) == LeaseCommand::Restore) { restored = true; break; }
        assert(restored);
    }
    auto hidden = AtStage(2, p);
    for (unsigned i = 0; i < 300; ++i) assert(hidden.Step(p) == LeaseCommand::None);
    assert(hidden.Hidden()); // 貸し出しの利用時間には期限を設けない。
    ui::LeaseMachine chat; p = Idle(); p.chat_over = true;
    for (unsigned i = 0; i < 150; ++i) assert(chat.Step(p) == LeaseCommand::None);
    p.chat_over = false; assert(chat.Step(p) == LeaseCommand::HideMap);
}

static void TestInput() {
    std::uintptr_t touch[3] = {1, 2, 3}, keys[2] = {4, 5};
    {
        ui::NativeInputInjection inj;
        for (auto &t : touch) inj.Put(&t, 0);
        inj.Put(&keys[0], 99);
        assert(touch[0] == 0 && keys[0] == 99);
        inj.End();
        assert(touch[1] == 2 && keys[0] == 4);
        inj.Put(&keys[1], 77);   // デストラクタでも戻る
    }
    assert(keys[1] == 5);
    auto t = ui::MakeTouch(-5, 500, true); assert(t.down && t.x == 0 && t.y == 239);
    t = ui::MakeTouch(100, 50, false); assert(!t.down && t.x == 0 && t.y == 0);
    t = ui::MakeTouch(320, 10, true); assert(t.x == 319 && t.y == 10);
}

static void TestList() {
    using namespace ui::lists;
    ListModel m;
    std::vector<Row> rows;
    for (int i = 0; i < 40; ++i) rows.push_back({"row" + std::to_string(i), 7u + i, "p", i % 2 ? "extra" : ""});
    m.Set(std::move(rows));
    assert(m.Size() == 40 && m.Generation() == 1 && m.At(3).message == 10 && m.At(1).extra == "extra");
    m.Move(-1); assert(m.Selected() == 0);
    m.Move(100); assert(m.Selected() == 39);
    m.Set({{"a", 0, "", ""}}); assert(m.Selected() == 0 && m.Generation() == 2);
    m.Close(); assert(m.Size() == 0 && m.Generation() == 3);

    DpadRepeat d;
    assert(d.Step(DpadRepeat::Down, 1000, 8) == 1);       // 押した時 1 行
    assert(d.Step(DpadRepeat::Down, 1399, 8) == 0);
    assert(d.Step(DpadRepeat::Down, 1400, 8) == 1);       // 400 ms 後
    assert(d.Step(DpadRepeat::Down, 1499, 8) == 0);
    assert(d.Step(DpadRepeat::Down, 1500, 8) == 1);       // 100 ms ごと
    assert(d.Step(0, 1501, 8) == 0);
    assert(d.Step(DpadRepeat::Right, 2000, 8) == 8);      // 左右は見える行数ぶん
    assert(d.Step(0, 2001, 8) == 0 && d.Step(DpadRepeat::Left, 2002, 6) == -6);
    assert(d.Step(0, 2003, 8) == 0 && d.Step(DpadRepeat::Up, 2004, 8) == -1);
}

static void TestTrace() {
    core::trace::SetFrame(5); core::trace::Mark("a", 1, 2);
#ifdef GOHAN_DEV
    assert(core::trace::Count() == 1 && core::trace::At(0).frame == 5 && core::trace::At(0).b == 2);
    for (int i = 0; i < 100; ++i) core::trace::Mark("x", i);
    assert(core::trace::Count() == core::trace::Capacity && core::trace::At(core::trace::Count() - 1).a == 99);
#else
    assert(core::trace::Count() == 0);
#endif
}


static void TestNativeTargets() {
    game::NativeTouch touch;
    assert(touch.raw_x == -1 && touch.released_frames == 200);
    touch.Step(-5, 999, true);
    assert(touch.down && !touch.previous && touch.changed && touch.x == 0 && touch.y == 239 && touch.held_frames == 1);
    const auto encoded = game::EncodeNativeTouch(touch);
    game::generated::AcnlTouchState state{};
    std::memcpy(&state, encoded.data(), sizeof(state));
    assert(state.held && state.changed && state.layout_x == -160 && state.layout_y == -119);
    std::array<std::uint8_t, 40> destination; destination.fill(0xAD);
    const auto original = destination;
    std::uint32_t pressed = 7, released = 0x8001, flags = 8, held = 3, trig = 99, repeat = 100;
    float x = -4, y = -5;
    const game::NativeInputTargets target{&destination, &pressed, &released, &flags, &held, &x, &y, &trig, &repeat};
    {
        ui::NativeInputInjection inject;
        assert(inject.Begin(target, touch, 4, 8));
        assert(destination == encoded && pressed == 0x8007 && released == 1);
        assert(flags == 9 && held == 0x8003 && x == 0 && y == 239 && trig == 4 && repeat == 8);
        assert(!inject.Begin(target, touch, 0, 0)); // 二重 Begin では控えを失わない
    }
    assert(destination == original && pressed == 7 && released == 0x8001);
    assert(flags == 8 && held == 3 && x == -4 && y == -5 && trig == 99 && repeat == 100);
    touch.Step(12, 34, true);
    assert(touch.previous && !touch.changed && touch.start_x == 0 && touch.previous_y == 239);
    for (int i = 0; i < 250; ++i) touch.Step(12, 34, true);
    assert(touch.held_frames == 200);
    touch.Step(0, 0, false);
    assert(touch.changed && touch.released_frames == 1 && touch.x == 12 && touch.y == 34);
    { ui::NativeInputInjection inject; assert(inject.Begin(target, touch, 0, 0));
      assert(!(pressed & 0x8000) && (released & 0x8000) && !(held & 0x8000)); }
    assert(destination == original);
}
static void TestLeaseBytes() {
    using namespace game::generated;
    game::BottomScreenImage image;
    image.manager = image.tab = image.map = image.other = true;
    image.calc = BsMenuTab_TabSelectUpdate_addr; image.room = 5; image.shown = 1;
    image.flags = MenuFlags_MapEnterDone;
    auto decoded = game::DecodeBottomScreen(image);
    assert(decoded.available && decoded.tab_settled && decoded.commands_consumed && !decoded.menu_open);
    image.menu_state = 0; assert(game::DecodeBottomScreen(image).menu_open); // 0 はアイドルではない
    image.menu_state = 1; image.flags = MenuFlags_EnterStarted;
    assert(game::DecodeBottomScreen(image).menu_open && !game::DecodeBottomScreen(image).menu_entered);
    image.flags |= MenuFlags_EnterDone;
    assert(game::DecodeBottomScreen(image).menu_entered);
    image.flags = MenuFlags_MapEnterDone;
    image.other_command = 0;
    ui::BottomScreenLease lease; lease.Acquire(1);
    lease.Step(game::DecodeBottomScreen(image));
    assert(lease.Step(game::DecodeBottomScreen(image)) == LeaseCommand::None);
    image.other_command = 2;
    assert(lease.Step(game::DecodeBottomScreen(image)) == LeaseCommand::HideMap);
    game::HideBottomScreen({&image.tab_command, &image.map_command, &image.other_command, image.map, image.other});
    assert(image.tab_command == 7 && image.map_command == 0 && image.other_command == 0);
    assert(!game::DecodeBottomScreen(image).commands_consumed);
    // 命令が消費されても、出入り中のタブがあればまだ隠れきっていない。
    image.tab_command = 11; image.map_command = image.other_command = 2;
    image.shown = 0; image.map = image.other = false; image.moving = 1;
    lease.Step(game::DecodeBottomScreen(image)); lease.Step(game::DecodeBottomScreen(image));
    assert(!lease.Status().hidden);
    image.moving = 0; lease.Step(game::DecodeBottomScreen(image));
    assert(lease.Status().hidden);
    lease.Release(1); lease.ResourcesReturned(1);
    assert(lease.Step(game::DecodeBottomScreen(image)) == LeaseCommand::Restore);
}
static void TestMenuThenLease() {
    ui::BottomScreenLease lease;
    game::BottomScreenSnapshot s;
    s.available = s.tab_settled = s.commands_consumed = s.map_present = s.tabs_present = true;
    s.menu_open = s.menu_entered = true;
    lease.Acquire(1); lease.Step(s);
    assert(lease.Step(s) == LeaseCommand::CloseMenu);
    s.tab_settled = false; s.menu_open = s.menu_entered = false; s.menu_gone = true;
    assert(lease.Step(s) == LeaseCommand::FinishMenuClose);
    assert(lease.TakeCancelled() == 0);
    s.tab_settled = true;
    assert(lease.Step(s) == LeaseCommand::None);
    assert(lease.Step(s) == LeaseCommand::HideMap);
}
static bool ReturnedImmediately() { return true; }
static void TestLeaseRecovery() {
    ui::BottomScreenLease lease;
    assert(lease.SetCleanup(0, ReturnedImmediately));
    game::BottomScreenSnapshot s;
    s.available = s.tab_settled = s.commands_consumed = s.tabs_present = s.map_present = true;
    lease.Acquire(1); lease.Step(s);
    assert(lease.Step(s) == LeaseCommand::HideMap);
    // 隠す命令の後でゲームが戻した。要求元も取り消し、再び勝手に隠さない。
    lease.Step(s); lease.Step(s);
    assert(lease.Status().stage == LeaseStage::Shown && lease.TakeCancelled() == 1);
    for (unsigned i = 0; i < 5; ++i) assert(lease.Step(s) == LeaseCommand::None);
    assert(lease.Status().drained);
    lease.Acquire(1);
    assert(lease.Step(s) == LeaseCommand::HideMap);
    // 退場完了が来なくても、期限切れ後は cleanup を通して Restore に進む。
    s.tabs_present = s.map_present = s.tab_settled = s.commands_consumed = false;
    bool restore = false;
    for (unsigned i = 0; i < ui::LeaseMachine::TimeoutFrames + 3; ++i)
        if (lease.Step(s) == LeaseCommand::Restore) { restore = true; break; }
    assert(restore && lease.Status().drained && lease.TakeCancelled() == 1);
}
static void TestLeaseChat() {
    game::BottomScreenImage image;
    image.manager = image.tab = image.map = true;
    image.shown = 1; image.calc = game::generated::BsMenuTab_TabSelectUpdate_addr;
    image.flags = 0x82; // 実機報告の通常表示。bit7 単独はチャット判定にしない。
    assert(!game::DecodeBottomScreen(image).chat_over);
    image.chat_window = true;
    ui::BottomScreenLease lease; lease.Acquire(1);
    for (unsigned i = 0; i < 150; ++i)
        assert(lease.Step(game::DecodeBottomScreen(image)) == LeaseCommand::None);
    image.chat_window = false; image.calc = 0x006D5510;
    assert(game::DecodeBottomScreen(image).chat_over);
    assert(lease.Step(game::DecodeBottomScreen(image)) == LeaseCommand::None);
    image.calc = game::generated::BsMenuTab_TabSelectUpdate_addr;
    assert(lease.Step(game::DecodeBottomScreen(image)) == LeaseCommand::HideMap);
}
static void TestLeaseOwners() {
    ui::BottomScreenLease lease;
    game::BottomScreenSnapshot s;
    s.available = s.tab_settled = s.commands_consumed = s.tabs_present = s.map_present = s.map_enter_done = true;
    lease.Acquire(1); lease.Acquire(2, true);
    assert(lease.Step(s) == LeaseCommand::None);
    s.commands_consumed = false; assert(lease.Step(s) == LeaseCommand::None);
    s.commands_consumed = true;
    assert(lease.Step(s) == LeaseCommand::HideMap);
    s.tabs_present = s.map_present = false;
    lease.Step(s); lease.Step(s); assert(lease.Status().hidden);
    lease.Release(1); lease.ResourcesReturned(1);
    lease.Step(s); assert(lease.Step(s) == LeaseCommand::None); // チャット側の資源も待つ
    lease.Release(2); lease.ResourcesReturned(2);
    assert(lease.Step(s) == LeaseCommand::Restore);
    s.tabs_present = s.map_present = true; lease.Step(s); lease.Step(s);
    assert(lease.Status().restored);
    ui::BottomScreenLease chat;
    chat.Acquire(4, true); chat.Step(s);
    assert(chat.Step(s) == LeaseCommand::None);
    chat.RequestStop(); chat.ResourcesReturned(4); chat.Step(s);
    assert(chat.Status().drained);
}
static void TestPaneAndGuard() {
    using namespace game::lyt;
    game::generated::NwLytPane src{}, dst{}, parent{};
    src.translate[0] = 4; src.scale[0] = 2; src.width = 50; src.alpha = 90; src.flags = 0x31;
    dst.flags = 0x30; dst.scale[0] = 1;
    PaneLog log;
    assert(MirrorPane(&src, &dst, &log));
    assert(dst.translate[0] == 4 && dst.scale[0] == 2 && dst.alpha == 90 && dst.flags == 1);
    log.Restore(); assert(dst.translate[0] == 0 && dst.scale[0] == 1 && dst.flags == 0x30);
    parent.children.next = parent.children.prev = &parent.children;
    assert(Reparent(&dst, &parent, &log));
    assert(parent.children.next == &dst.sibling_link && dst.parent == &parent);
    assert(!Reparent(&parent, &dst, &log));
    log.Restore(); assert(dst.parent == nullptr && parent.children.next == &parent.children);
    game::SceneGuard guard; guard.Capture(&src, 7);
    assert(guard.Matches(&src, 7) && !guard.Matches(&src, 8) && !guard.Matches(&dst, 7));
    game::heap::Room room{100, 50, true};
    assert(room.Allows(100, 50) && !room.Allows(101, 50) && !room.Allows(100, 51));
}

int main() {
    TestNativeTargets(); TestMenuThenLease(); TestLeaseRecovery(); TestLeaseChat(); TestLeaseBytes(); TestLeaseOwners(); TestPaneAndGuard();
    TestWriteLog(); TestVtable(); TestBclyt(); TestLease(); TestInput(); TestList(); TestTrace();
    std::puts("test_p4a1: OK");
}

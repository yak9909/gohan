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
    ui::LeaseProbe p; p.room = 1; p.owners = 1; p.tab_settled = true;
    p.commands_consumed = p.tab_command_consumed = true;
    p.map_present = true; p.resources_returned = true; return p;
}

static void TestLease() {
    ui::LeaseMachine m;
    auto p = Idle();
    p.tab_settled = false; assert(m.Step(p) == LeaseCommand::None);
    p.tab_settled = true; p.commands_consumed = false; assert(m.Step(p) == LeaseCommand::None);
    p.commands_consumed = true;
    assert(m.Step(p) == LeaseCommand::HideMap);
    p.game_restored = true; // 退場中に地図が残っていても再出現と扱わない
    m.Step(p); assert(m.Stage() == LeaseStage::Exiting);
    p.game_restored = false; p.hide_done = true; m.Step(p);
    assert(m.Hidden());
    p.owners = 0; p.resources_returned = false;
    m.Step(p); assert(m.Stage() == LeaseStage::Hidden);
    p.resources_returned = true;
    p.commands_consumed = false; assert(m.Step(p) == LeaseCommand::None);
    p.commands_consumed = true;
    assert(m.Step(p) == LeaseCommand::Restore && m.Restoring() && !m.Drained());
    // 復帰中の最後の要求を保持し、復帰が済むまで命令を重ねない。
    p.owners = 1; assert(m.Step(p) == LeaseCommand::None && !m.Drained());
    p.restore_done = true; m.Step(p); assert(m.Restored() && !m.Restoring());
    assert(m.Step(p) == LeaseCommand::HideMap);

    ui::LeaseMachine again; p = Idle();
    assert(again.Step(p) == LeaseCommand::HideMap);
    p.hide_done = true; again.Step(p); again.Step(p);
    p.game_restored = true;
    assert(again.Step(p) == LeaseCommand::None && again.Stage() == LeaseStage::Shown);
    p.game_restored = false;
    assert(again.Step(p) == LeaseCommand::HideMap);
    p.room = 2;
    assert(again.Step(p) == LeaseCommand::None && again.Stage() == LeaseStage::Shown);

    ui::LeaseMachine menu; p = Idle(); p.menu_open = true;
    assert(menu.Step(p) == LeaseCommand::None);
    p.menu_entered = true; p.tab_command_consumed = false;
    assert(menu.Step(p) == LeaseCommand::None);
    // メニュー終了の入口はタブ命令だけを見る。地図や部屋の消費待ちは追加しない。
    p.tab_command_consumed = true; p.commands_consumed = false;
    assert(menu.Step(p) == LeaseCommand::CloseMenu);
    p.owners = 0; menu.Shutdown();
    assert(menu.Step(p) == LeaseCommand::None && !menu.Drained());
    p.menu_gone = true; p.menu_open = false;
    assert(menu.Step(p) == LeaseCommand::FinishMenuClose && menu.Drained());

    // Shown では部屋変更自体による段・印のリセットを行わない。
    ui::LeaseMachine shown; p = Idle(); p.owners = 0;
    assert(shown.Step(p) == LeaseCommand::None);
    ++p.room;
    assert(shown.Step(p) == LeaseCommand::None && shown.Stage() == LeaseStage::Shown);
    assert(shown.Restored() && shown.Drained() && !shown.Requested() && !shown.Hidden() && !shown.Restoring());
    // 部屋変更が通常の Shown の処理を一フレーム止めることもない。
    ++p.room; p.owners = 1;
    assert(shown.Step(p) == LeaseCommand::HideMap);

    for (bool gone_on_change : {false, true}) {
        ui::LeaseMachine closing; p = Idle(); p.menu_open = p.menu_entered = true;
        assert(closing.Step(p) == LeaseCommand::CloseMenu);
        ++p.room; p.menu_gone = gone_on_change;
        assert(closing.Step(p) == (gone_on_change ? LeaseCommand::FinishMenuClose : LeaseCommand::None));
        assert(closing.Stage() == LeaseStage::Shown && closing.Requested() && !closing.Restored());
        if (!gone_on_change) {
            assert(!closing.Drained());
            // 閉じ始めた後に所有者が消えても、部屋変更で後始末を捨てない。
            p.owners = 0; p.menu_gone = true;
            assert(closing.Step(p) == LeaseCommand::FinishMenuClose);
        }
        p.owners = 0; p.menu_open = false;
        assert(closing.Step(p) == LeaseCommand::None && closing.Drained());
        assert(closing.Step(p) == LeaseCommand::None); // 後始末は一度だけ
    }

    ui::LeaseMachine stop; p = Idle(); stop.Step(p);
    stop.Shutdown(); p.owners = 0; p.resources_returned = false;
    stop.Step(p); assert(!stop.Drained());
    p.hide_done = true; stop.Step(p); assert(stop.Hidden());
    p.resources_returned = true;
    assert(stop.Step(p) == LeaseCommand::Restore && !stop.Drained());
    assert(stop.Step(p) == LeaseCommand::None && !stop.Drained());
    p.restore_done = true; stop.Step(p); assert(stop.Drained());

    ui::LeaseMachine chat; p = Idle(); p.chat_over = true;
    assert(chat.Step(p) == LeaseCommand::None && chat.Drained());
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
    image.menu_state = 0; assert(game::DecodeBottomScreen(image).menu_open);
    image.menu_state = 1; image.flags = MenuFlags_EnterStarted;
    assert(game::DecodeBottomScreen(image).menu_open && !game::DecodeBottomScreen(image).menu_entered);
    image.flags |= MenuFlags_EnterDone;
    assert(game::DecodeBottomScreen(image).menu_entered);
    // 開始通知を持たない画面の扱いも従来の MenuOpen に揃える。
    image.flags = 0; image.screen_list_empty = false;
    assert(!game::DecodeBottomScreen(image).menu_open && !game::DecodeBottomScreen(image).menu_gone);
    image.screen_list_empty = true; image.flags = MenuFlags_MapEnterDone;
    image.other_command = 0;
    ui::BottomScreenLease lease; lease.Acquire(1);
    assert(lease.Step(game::DecodeBottomScreen(image)) == LeaseCommand::HideMap);
    game::HideBottomScreen({&image.tab_command, &image.map_command, &image.other_command, image.map, image.other});
    assert(image.tab_command == 7 && image.map_command == 0 && image.other_command == 0);
    assert(!game::DecodeBottomScreen(image).commands_consumed);
    // タブと地図は11/2へ。部屋は受付待ちの0・参照ありのままでも追加で待たない。
    image.tab_command = 11; image.map_command = 2;
    image.shown = 0; image.map = false; image.moving = 1;
    lease.Step(game::DecodeBottomScreen(image)); lease.Step(game::DecodeBottomScreen(image));
    assert(!lease.Status().hidden);
    image.moving = 0; lease.Step(game::DecodeBottomScreen(image));
    assert(lease.Status().hidden);
    lease.Release(1); lease.ResourcesReturned(1);
    assert(lease.Step(game::DecodeBottomScreen(image)) == LeaseCommand::Restore);
    // 状態18: 地図/部屋命令1、0x01待ち（タブ選択以外のcalcは代役0）。
    image.tab_command = 2; image.map_command = image.other_command = 1; image.calc = 0;
    image.flags = MenuFlags_MapOutDone;
    lease.Step(game::DecodeBottomScreen(image)); assert(!lease.Status().drained);
    image.tab_command = 11; image.map_command = image.other_command = 2;
    lease.Step(game::DecodeBottomScreen(image)); assert(!lease.Status().restored);
    // 地図が入場開始を通知してから、状態7でアニメと0x02を待つ。
    image.map = true; image.flags = MenuFlags_MapEnterStarted;
    image.calc = BsMenuTab_TabsInFinishUpdate_addr; image.moving = 1; image.shown = 1;
    lease.Step(game::DecodeBottomScreen(image)); assert(!lease.Status().restored);
    image.flags = MenuFlags_MapEnterDone;
    lease.Step(game::DecodeBottomScreen(image)); assert(!lease.Status().restored);
    image.moving = 0; image.calc = BsMenuTab_TabSelectUpdate_addr;
    lease.Step(game::DecodeBottomScreen(image)); assert(lease.Status().restored && lease.Status().drained);

    // ゲームが地図を戻したら、二重の復帰命令を出さない。
    ui::BottomScreenLease automatic; automatic.Acquire(1);
    assert(automatic.Step(game::DecodeBottomScreen(image)) == LeaseCommand::HideMap);
    image.map = false; image.shown = 0;
    automatic.Step(game::DecodeBottomScreen(image)); automatic.Step(game::DecodeBottomScreen(image));
    automatic.Release(1); automatic.ResourcesReturned(1);
    image.map = true;
    assert(automatic.Step(game::DecodeBottomScreen(image)) == LeaseCommand::None);
    assert(automatic.Status().stage == LeaseStage::Shown);

    // 部屋変更は各遷移段でゲームへ制御を返し、命令を追加しない。
    for (unsigned phase = 0; phase < 3; ++phase) {
        ui::BottomScreenLease moved; image.map = true; image.shown = 1;
        moved.Acquire(1); moved.Step(game::DecodeBottomScreen(image));
        if (phase) {
            image.map = false; image.shown = 0;
            moved.Step(game::DecodeBottomScreen(image)); moved.Step(game::DecodeBottomScreen(image));
        }
        if (phase == 2) {
            moved.Release(1); moved.ResourcesReturned(1);
            assert(moved.Step(game::DecodeBottomScreen(image)) == LeaseCommand::Restore);
        }
        ++image.room;
        assert(moved.Step(game::DecodeBottomScreen(image)) == LeaseCommand::None);
        assert(moved.Status().stage == LeaseStage::Shown && !moved.Status().hidden);
    }
}
static void TestLeaseOwners() {
    ui::BottomScreenLease lease;
    game::BottomScreenSnapshot s;
    s.available = s.tab_settled = s.commands_consumed = s.tab_command_consumed = true;
    s.tabs_present = s.map_present = s.map_enter_done = true;
    lease.Acquire(1); lease.Acquire(2, true);
    assert(lease.Step(s) == LeaseCommand::HideMap);
    s.tabs_present = s.map_present = false;
    lease.Step(s); lease.Step(s); assert(lease.Status().hidden);
    lease.Release(1); lease.ResourcesReturned(1);
    assert(lease.Step(s) == LeaseCommand::None); // チャット側の資源も待つ
    lease.Release(2); lease.ResourcesReturned(2);
    assert(lease.Step(s) == LeaseCommand::Restore);
    s.tabs_present = s.map_present = true; lease.Step(s); lease.Step(s);
    assert(lease.Status().restored);
    ui::BottomScreenLease chat;
    // メニュー（チャット）が開いていても、重ねる持ち主は退場・終了を命じない。
    s.menu_open = s.menu_entered = true;
    chat.Acquire(4, true);
    assert(chat.Step(s) == LeaseCommand::None && chat.Status().restored);
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
    TestNativeTargets(); TestLeaseBytes(); TestLeaseOwners(); TestPaneAndGuard();
    TestWriteLog(); TestVtable(); TestBclyt(); TestLease(); TestInput(); TestList(); TestTrace();
    std::puts("test_p4a1: OK");
}

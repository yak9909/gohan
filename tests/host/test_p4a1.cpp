// P4a-1: 下画面の土台の純粋な部分（WriteLog・VtableOverride・bclyt 節編集・Lease の段・入力注入・ListModel・十字の送り・trace）。
#include "core/trace.hpp"
#include "game/lyt/bclyt_edit.hpp"
#include "game/vtable_override.hpp"
#include "game/write_log.hpp"
#include "ui/lease/lease_machine.hpp"
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
    put32(s, 8 + 0x40);
    std::vector<std::uint8_t> body(0x40, 0);
    std::strncpy(reinterpret_cast<char *>(&body[8]), name, 24);
    std::memcpy(&body[0x2C], &x, 4);
    s.insert(s.end(), body.begin(), body.end());
    return s;
}
static std::vector<std::uint8_t> File(int n) {
    std::vector<std::uint8_t> f = {'F', 'L', 'Y', 'T', 0xFF, 0xFE, 0x14, 0x00, 0, 0, 0, 0};
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
    std::uintptr_t original[30]; for (int i = 0; i < 30; ++i) original[i] = 100 + i;
    std::uintptr_t object_word = reinterpret_cast<std::uintptr_t>(original);
    game::VtableOverride<30> ov;
    assert(ov.Install(&object_word, original));
    assert(ov.Set(13, 1) && ov.Set(16, 2) && ov.Set(24, 3) && ov.Set(25, 4) && !ov.Set(30, 9));
    assert(ov.Activate() && ov.Active());
    const auto *copy = reinterpret_cast<const std::uintptr_t *>(object_word);
    assert(copy != original && copy[0] == 100 && copy[1] == 101 && copy[13] == 1 && copy[25] == 4 && copy[12] == 112);
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
    assert(!e.SetU32(n, 0x3E, 1) && e.SetU32(n, 0x3C, 7));
    const auto out = e.Build();
    assert(out.size() == raw.size() + 8 + 0x40 && e.TotalSize() == out.size());
    BclytEdit again; assert(again.Load(out.data(), out.size()) && again.SectionCount() == 3);
    // 壊れた入力
    auto bad = raw; bad[0] = 'X'; assert(!again.Load(bad.data(), bad.size()));
    bad = raw; bad[0x10] = 3; assert(!again.Load(bad.data(), bad.size()));          // 節の数が合わない
    bad = raw; bad[0x14 + 4] = 0x44; assert(!again.Load(bad.data(), bad.size()));   // 節の大きさが合わない
    bad = raw; bad.pop_back(); assert(!again.Load(bad.data(), bad.size()));         // 全体の大きさが合わない
    assert(!again.Load(raw.data(), 8) && !again.Load(nullptr, 0));
}

static ui::LeaseProbe Idle() {
    ui::LeaseProbe p; p.room = 1; p.owners = 1; p.tab_settled = true; p.commands_consumed = true;
    p.map_present = true; p.resources_returned = true; return p;
}

static void TestLease() {
    ui::LeaseMachine m;
    auto p = Idle();
    // 落ち着くまで書かない
    p.tab_settled = false; assert(m.Step(p) == LeaseCommand::None && m.Stage() == LeaseStage::Shown);
    p.tab_settled = true; p.commands_consumed = false; assert(m.Step(p) == LeaseCommand::None);
    p.commands_consumed = true;
    assert(m.Step(p) == LeaseCommand::HideMap && m.Stage() == LeaseStage::Exiting && m.Requested() && !m.Hidden());
    // 最低 2 フレーム
    p.hide_done = true;
    m.Step(p); assert(m.Stage() == LeaseStage::Exiting);
    m.Step(p); assert(m.Stage() == LeaseStage::Hidden && m.Hidden());
    // 資源が返るまで戻さない（持ち主が無くなっても）
    p.owners = 0; p.resources_returned = false;
    m.Step(p); m.Step(p); m.Step(p); assert(m.Stage() == LeaseStage::Hidden);
    p.resources_returned = true;
    assert(m.Step(p) == LeaseCommand::Restore && m.Stage() == LeaseStage::Restoring && m.Restoring() && !m.Restored());
    p.restore_done = true;
    m.Step(p); assert(m.Stage() == LeaseStage::Restoring);
    m.Step(p); assert(m.Stage() == LeaseStage::Shown && m.Restored());

    // 地図が無く他の画面がある時・どちらも無い時
    ui::LeaseMachine m2; p = Idle(); p.map_present = false; p.other_present = true;
    assert(m2.Step(p) == LeaseCommand::HideOther);
    ui::LeaseMachine m3; p = Idle(); p.map_present = false;
    assert(m3.Step(p) == LeaseCommand::HideTab);
    // チャットの上: 隠さない
    ui::LeaseMachine m4; p = Idle(); p.chat_over = true;
    assert(m4.Step(p) == LeaseCommand::None && m4.Stage() == LeaseStage::Shown);

    // ゲームが自分で戻した: Shown に戻り、落ち着いたらもう一度隠す
    ui::LeaseMachine m5; p = Idle();
    m5.Step(p); p.game_restored = true; m5.Step(p);
    assert(m5.Stage() == LeaseStage::Shown);
    p.game_restored = false; p.tab_settled = false; assert(m5.Step(p) == LeaseCommand::None);
    p.tab_settled = true; assert(m5.Step(p) == LeaseCommand::HideMap);

    // 場面が変わった: Shown に戻し、命令は出さない
    ui::LeaseMachine m6; p = Idle(); m6.Step(p); p.hide_done = true; m6.Step(p); m6.Step(p);
    assert(m6.Stage() == LeaseStage::Hidden);
    p.room = 2; assert(m6.Step(p) == LeaseCommand::None && m6.Stage() == LeaseStage::Shown && !m6.Hidden());

    // 持ち物を開いたまま: 入場し終えるまで待ち、閉じさせる命令は 1 回だけ。閉じてから隠す
    ui::LeaseMachine m7; p = Idle(); p.menu_open = true; p.menu_entered = false; p.tab_settled = false;
    assert(m7.Step(p) == LeaseCommand::None);
    p.menu_entered = true; assert(m7.Step(p) == LeaseCommand::CloseMenu);
    assert(m7.Step(p) == LeaseCommand::None && m7.Stage() == LeaseStage::Shown);
    p.menu_open = false; p.tab_settled = true; assert(m7.Step(p) == LeaseCommand::HideMap);

    // Shutdown: 隠れている時は出し直しを 1 回だけ。戻りきりは待たない
    ui::LeaseMachine m8; p = Idle(); m8.Step(p);
    assert(m8.Shutdown() == LeaseCommand::Restore && m8.Shutdown() == LeaseCommand::None && !m8.Restored());
    ui::LeaseMachine m9; assert(m9.Shutdown() == LeaseCommand::None);
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

int main() {
    TestWriteLog(); TestVtable(); TestBclyt(); TestLease(); TestInput(); TestList(); TestTrace();
    std::puts("test_p4a1: OK");
}

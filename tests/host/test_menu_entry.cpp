// 入力の枠（数値・選択・複数選択・ボタンの割り当て・実行の入力）を、試験用の spec で試験する。
// ボタンとタッチの両方で操作でき、結果がエンジンに返ることと、描画指示の中身を確かめる。
#include "menu/shell.hpp"
#include "menu_ids.hpp"
#include "menu_rig.hpp"
#include <cstdio>

using namespace menu_test;

namespace {
void Demand(const Rig &rig, bool wanted) {
    const auto claim = rig.shell.InputDemand();
    assert(claim.block == (wanted ? gohan::core::input_block::group::Buttons : 0));
    assert(claim.defer == 0);
}
u16 Idx(MenuId id) { return static_cast<u16>(id); }

struct Rec {
    s32 weather = 1;
    int weather_writes = 0;
    u32 pocket_input = 0;
    int pocket_runs = 0;
} g;

bool WeatherRead(s32 *value) {
    *value = g.weather;
    return true;
}
Result WeatherWrite(s32 value) {
    g.weather = value;
    ++g.weather_writes;
    return Result::Ok;
}
Result PocketRun(u32 input) {
    g.pocket_input = input;
    ++g.pocket_runs;
    return Result::Ok;
}
const Bound kBinds[] = {
    GOHAN_ITEM(MenuId::Weather, GameValue{.read = WeatherRead, .write = WeatherWrite}),
    GOHAN_ITEM(MenuId::Pocket, Trigger{.run_with = PocketRun}),
};

// 入力の枠の大きい数字（倍率 2）。行の値と区別する。
const Command *Large(const RecordingSurface &s, const std::string &text) {
    for (const Command &c : s.commands)
        if (c.kind == Command::Text && c.scale == 1 && c.numeric && c.bounds.x >= 236 && c.text == text) return &c;
    return nullptr;
}

struct Fixture {
    Rig rig{kMenuItems, kMenuItemCount};
    Fixture() {
        g = Rec{};
        assert(rig.engine.BindAll(kBinds, sizeof(kBinds) / sizeof(kBinds[0])));
        rig.Open();
    }
    Engine &e() { return rig.engine; }
    // 現在のフォルダで目的の項目まで下へ進める（キー）。
    void Select(MenuId id) {
        int guard = 0;
        while (e().Selected() != Idx(id)) {
            rig.Tap(key::Down);
            assert(++guard < 40);
        }
    }
    void EnterFolder() {
        Select(MenuId::Folder);
        rig.Tap(key::A);
    }
    // 枠が出きる（160ms）まで進めて描く。
    RecordingSurface Draw() {
        rig.Advance(300);
        return rig.Draw();
    }
};

void NumberEntry() {
    Fixture f;
    f.EnterFolder();
    f.Select(MenuId::Num);
    f.rig.Tap(key::A);
    assert(f.e().Ui().kind == UiKind::Number && f.rig.shell.Modal());
    Demand(f.rig, true);
    RecordingSurface s = f.Draw();
    // 数字用の書体で大きく出し、ボタンの枠と決定・取消がある。説明や足は出さない。
    const Command *value = Large(s, "10");
    assert(value && value->numeric && value->scale == 1 && value->color == gohan::core::design::ca2::Color::Text);
    assert(s.HasText("決定") && s.HasText("取消") && !s.HasText("HOTKEY"));
    assert(s.dropped == 0 && s.texts <= static_cast<int>(gohan::core::limits::RendererTexts) && s.rects <= static_cast<int>(gohan::core::limits::RendererRects));
    // 右で step（5）、上で 10 倍。A で編集値に入り、適用待ちになる。
    f.rig.Tap(key::Right);
    f.rig.Tap(key::Up);
    f.rig.Tap(key::A);
    assert(f.e().Ui().kind == UiKind::None && !f.rig.shell.Modal());
    assert(f.e().State(Idx(MenuId::Num)).value == 65 && f.e().Applied(Idx(MenuId::Num)) == 10);
    assert(f.e().Dirty(Idx(MenuId::Num)));
    // B で取り消す。
    f.rig.Tap(key::A);
    f.rig.Tap(key::Left);
    f.rig.Tap(key::B);
    assert(f.e().State(Idx(MenuId::Num)).value == 65 && !f.rig.shell.Modal());
    // 長押しの繰り返し（200ms 後から 60ms ごと）。範囲で止まる。
    f.rig.Tap(key::A);
    f.rig.Hold(key::Up);
    f.rig.Advance(1200);
    f.rig.Release();
    f.rig.Tap(key::A);
    assert(f.e().State(Idx(MenuId::Num)).value == 100);
}

void NumberIgnoresTouch() {
    Fixture f; f.EnterFolder(); f.Select(MenuId::Num); f.rig.Tap(key::A);
    f.rig.TapAt(280, 60); f.rig.TapAt(360, 30);
    assert(f.e().Ui().kind == UiKind::Number);
    f.rig.Tap(key::A);
    assert(f.e().State(Idx(MenuId::Num)).value == 10);
}

void DecimalAndHex() {
    Fixture f;
    f.EnterFolder();
    f.Select(MenuId::Amount);
    f.rig.Tap(key::A);
    RecordingSurface s = f.Draw();
    assert(Large(s, "5.0"));
    f.rig.Tap(key::Right);
    s = f.Draw();
    assert(Large(s, "5.5"));
    f.rig.Tap(key::B);
    f.Select(MenuId::Hexv);
    f.rig.Tap(key::A);
    s = f.Draw();
    assert(Large(s, "0xFF"));
    f.rig.Tap(key::B);
}

void ListEntry() {
    Fixture f;
    f.EnterFolder();
    f.Select(MenuId::Weather);
    f.rig.Tap(key::A);
    assert(f.e().Ui().kind == UiKind::List);
    Demand(f.rig, true);
    RecordingSurface s = f.Draw();
    assert(s.HasText("晴れ") && s.HasText("雨") && s.HasText("雪") && !s.HasText("HOTKEY"));
    assert(s.dropped == 0 && s.texts <= static_cast<int>(gohan::core::limits::RendererTexts));
    f.rig.Tap(key::Down);
    f.rig.Tap(key::A); // 雪
    assert(f.e().State(Idx(MenuId::Weather)).value == 2 && f.e().Ui().kind == UiKind::None);
    f.rig.Tap(key::A);
    f.rig.TapAt(260, 30);
    assert(f.e().Ui().kind == UiKind::List);
    f.rig.Tap(key::Up); f.rig.Tap(key::Up); f.rig.Tap(key::A);
    assert(f.e().State(Idx(MenuId::Weather)).value == 0);
    assert(f.e().Commit(Idx(MenuId::Weather), f.rig.now) && g.weather == 0 && g.weather_writes == 1);
}

void CheckListEntry() {
    Fixture f;
    f.Select(MenuId::Multi);
    f.rig.Tap(key::A);
    assert(f.e().Ui().kind == UiKind::CheckList);
    Demand(f.rig, true);
    RecordingSurface s = f.Draw();
    assert(s.HasText("[ ] a") && s.HasText("[X] b") && s.HasText("決定") && s.HasText("取消"));
    f.rig.Tap(key::A); // a を入れる
    s = f.Draw();
    assert(s.HasText("[X] a"));
    f.rig.Tap(key::X);
    assert(f.e().State(Idx(MenuId::Multi)).value == 3 && f.e().Ui().kind == UiKind::None);
}

void KeyCapture() {
    Fixture f; f.EnterFolder(); f.Select(MenuId::Keyc); f.rig.Hold(key::A);
    assert(f.e().Ui().kind == UiKind::Key);
    Demand(f.rig, true);
    f.rig.Hold(key::A); f.rig.Release();
    assert(f.Draw().HasText("ボタンを押す"));
    f.rig.Hold(key::L); f.rig.Hold(key::L | key::R); f.rig.Release();
    assert(f.e().Ui().kind == UiKind::None && f.e().State(Idx(MenuId::Keyc)).value == (key::L | key::R));
    f.rig.Tap(key::A); f.rig.Release();
    f.rig.Hold(key::Select); f.rig.Advance(650); f.rig.Release();
    assert(f.e().Ui().kind == UiKind::None && f.e().State(Idx(MenuId::Keyc)).value == 0);
}

void HotkeyOfItem() {
    Fixture f;
    f.EnterFolder();
    f.Select(MenuId::How);
    f.rig.Tap(key::Y);
    assert(f.e().Ui().kind == UiKind::Key);
    f.rig.Release();
    f.rig.Hold(key::ZL | key::X);
    f.rig.Release();
    assert(f.e().State(Idx(MenuId::How)).hotkey == (key::ZL | key::X));
    f.rig.Tap(key::Y); f.rig.Tap(key::Select);
    assert(f.e().Ui().kind == UiKind::None && f.e().State(Idx(MenuId::How)).hotkey == (key::ZL | key::X));
}

void TriggerInputWhileClosed() {
    Fixture f;
    f.rig.Tap(key::Select);
    f.rig.Advance(400);
    assert(!f.e().Visible());
    Demand(f.rig, false);
    // 閉じたまま、ホットキー（R+X）で十六進の入力が下画面の中央に出る。
    f.rig.Hold(key::R | key::X);
    f.rig.Release();
    assert(f.e().Ui().kind == UiKind::TriggerInput && f.rig.shell.Modal());
    Demand(f.rig, true);
    RecordingSurface s = f.Draw();
    assert(Large(s, "0x0") && s.dropped == 0);
    bool centered = false;
    for (const Command &c : s.commands) centered = centered || (c.kind == Command::Slice && c.bounds.x == 236 && c.bounds.w == 160);
    assert(centered);
    f.rig.Tap(key::Up); // 十六進は 16 ずつ
    f.rig.Tap(key::Right);
    f.rig.Tap(key::A);
    assert(g.pocket_runs == 1 && g.pocket_input == 0x11);
    assert(!f.rig.shell.Modal());
    Demand(f.rig, false);
}
} // namespace

int main() {
    NumberEntry();
    NumberIgnoresTouch();
    DecimalAndHex();
    ListEntry();
    CheckListEntry();
    KeyCapture();
    HotkeyOfItem();
    TriggerInputWhileClosed();
    std::puts("menu entry: ok");
    return 0;
}

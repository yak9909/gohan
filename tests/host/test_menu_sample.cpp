// 収録している見本の spec（spec/menu.toml）と見本の処理の結び付けを、エンジンで動かして確かめる。
#include "menu/engine.hpp"
#include "menu/sample_items.hpp"
#include "menu_ids.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace gohan::menu;

namespace {
u16 Idx(MenuId id) { return static_cast<u16>(id); }
bool Is(const char *a, const char *b) { return std::strcmp(a, b) == 0; }
} // namespace

int main() {
    sample::State &s = sample::Get();
    Engine e(kMenuItems, kMenuItemCount);
    // spec と結び付けの全部が合っている（合わない物はコンパイルで止まる。ここは重複・範囲の確認）。
    assert(e.BindAll(sample::kBindings, sample::kBindingCount));
    u32 now = 1000;
    e.Open(now);

    // 状態: ホットキー付きで ON にすると待機し、ホットキーで効果が入る。
    assert(e.SetValue(Idx(MenuId::SampleFlag), 1) && e.Commit(Idx(MenuId::SampleFlag), now));
    assert(!s.flag && Is(e.View(Idx(MenuId::SampleFlag)).value, "待機"));
    assert(e.RequestClose(now));
    e.Frame(now += 16, key::L | key::Up);
    assert(s.flag && Is(e.View(Idx(MenuId::SampleFlag)).value, "ON"));
    e.Frame(now += 16, 0);

    // 押下中: 有効にして、ホットキーを押している間だけ数える。
    e.Open(now);
    assert(e.SetValue(Idx(MenuId::SampleHold), 1) && e.Commit(Idx(MenuId::SampleHold), now));
    assert(e.RequestClose(now));
    e.Frame(now += 16, key::R | key::A);
    e.Frame(now += 16, key::R | key::A);
    assert(s.holding && s.hold_frames == 2);
    e.Frame(now += 16, 0);
    assert(!s.holding);

    // 実行: 適用で実行し、ホットキーでは確認する。
    e.Open(now);
    assert(e.SetValue(Idx(MenuId::SampleStamp), 1) && e.Commit(Idx(MenuId::SampleStamp), now));
    assert(s.stamps == 1);
    assert(e.RequestClose(now));
    e.Frame(now += 16, key::L | key::Down);
    assert(e.Dialog() == DialogKind::Action && s.stamps == 1);
    e.Confirm(true, now);
    assert(s.stamps == 2);
    e.Frame(now += 16, 0);

    // 画面: 同じ group の画面は同時に 1 つだけ。
    e.Open(now);
    e.Press(key::A, now); // 先頭の項目（フォルダ）に入る
    while (e.Selected() != Idx(MenuId::SampleEditorA)) e.Press(key::Down, now);
    e.Press(key::A, now);
    assert(s.editor_open[0]);
    e.Press(key::Down, now);
    e.Press(key::A, now);
    assert(s.editor_open[1] && !s.editor_open[0]);

    // 値（設定）と値（ゲーム）。移動方法がマス単位の間は量が無効。
    assert(!e.Disabled(Idx(MenuId::SampleAmount)));
    assert(e.SetValue(Idx(MenuId::SampleMode), 1) && e.Disabled(Idx(MenuId::SampleAmount)));
    assert(e.SetValue(Idx(MenuId::SampleMode), 0) && !e.Disabled(Idx(MenuId::SampleAmount)));
    assert(Is(e.View(Idx(MenuId::SampleAmount)).value, "5.0"));
    s.wallet = 1234;
    assert(e.RequestClose(now));
    e.Open(now);
    assert(e.State(Idx(MenuId::SampleWallet)).value == 1234);
    assert(e.SetValue(Idx(MenuId::SampleWallet), 99) && e.Commit(Idx(MenuId::SampleWallet), now));
    assert(s.wallet == 99 && s.wallet_writes == 1);
    s.wallet_readable = false;
    assert(e.RequestClose(now));
    e.Open(now);
    assert(e.Disabled(Idx(MenuId::SampleWallet)));
    std::puts("menu sample: ok");
    return 0;
}

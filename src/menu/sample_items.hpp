#pragma once
#include "menu/binding.hpp"
#include <cstddef>

// 見本の処理。spec/menu.toml の見本の木（sample.*）に結ぶ。ゲームには触れない。
// 5 つの振る舞い（状態・押下中・実行・画面・値）と値の 2 つの出どころ（設定・ゲーム）を 1 つずつ使う。
namespace gohan::menu::sample {
struct State {
    bool flag = false;            // 状態: 効果が入っているか
    bool holding = false;         // 押下中: 今押しているか
    unsigned hold_frames = 0;     // 押下中: 押していたフレーム数
    unsigned stamps = 0;          // 実行: 実行した回数
    bool editor_open[2] = {};     // 画面: 開いているか
    unsigned editor_steps = 0;    // 画面: 起動中のフレーム数
    s32 wallet = 0;               // 値（ゲーム）の代わりの変数
    bool wallet_readable = true;  // false にすると読み取りの失敗を再現できる
    unsigned wallet_writes = 0;   // 書いた回数
};

State &Get();
inline constexpr std::size_t kBindingCount = 6;
extern const Bound kBindings[kBindingCount];
} // namespace gohan::menu::sample

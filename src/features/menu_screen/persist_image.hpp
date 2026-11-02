#pragma once
#include "core/limits.hpp"
#include "core/mailbox.hpp"
#include "menu/types.hpp"
#include <vector>

// 次の起動に持ち越す欄の写し（menu::persist::Encode の結果）と、書き手が最新の写しを受け取る手順。
// CTRPF に依存しない（PC の試験で確かめる）。写しを作るのはゲームのスレッド、受け取るのは書き手（作業のスレッド、
// または CTRPF のメニューのスレッド）。
namespace gohan::features::menu_screen {
// Mailbox で渡すので、そのままの値（trivially copyable）。
struct Image {
    menu::u32 size = 0;
    menu::u8 bytes[core::limits::PersistBytes]{};
};
using ImageBox = core::Mailbox<Image>;

// 最新の写し（ゲームのスレッドが出し、書き手が受け取る）。
ImageBox &Latest();

// 書き手: 最新の写しを out へ写す。写しが無い・Busy が attempts 回続いた時は偽（out は空）。
// Busy の間は sleep_ms で 1 ms ずつ待つ。写しを受ける作業の領域は、呼ぶたびにヒープから借りる（静的領域を増やさない）。
bool CopyLatest(ImageBox &box, std::vector<menu::u8> &out, void (*sleep_ms)(), unsigned attempts);
} // namespace gohan::features::menu_screen

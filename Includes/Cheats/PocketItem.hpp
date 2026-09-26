#pragma once

// PocketItem — ポケットアイテム（gohan.md §6 / §17.5）。入力した ID を持ち物の一番近い空き枠へ入れる。
// ゲームの関数はゲームのスレッドで呼ぶ（グリッドカーソルの毎フレームのフックへ相乗り）。

#include <3ds/types.h>

namespace PocketItem {

enum class Result : u8 {
    Ok,
    Full,           // 空き枠が無い
    Invalid,        // 空のアイテム（0x7FFE）
    NoPlayer,       // 今のプレイヤーのセーブが読めない（読み書き中）
    Busy,
    HookFailed,
    Unsupported,
    TimedOut,
};

// メニューのスレッドから。ゲームのスレッドが処理するまで最大 1 秒待つ。slot = 入れた枠（0〜15）
Result Request(u16 id, int &slot, bool &iconUpdated);

// 没アイテム表示（Inventory_GetSlot 0x7238C0 の BEQ を NOP）。切り替えたら、持ち物を開いていれば 16 枠のアイコンを作り直す。
bool HiddenShown(void);
Result SetHiddenShown(bool show);
const char *ResultName(Result result);

}  // namespace PocketItem

namespace CTRPluginFramework
{
    namespace Cheats
    {
        void    WirePocketItem(void);
    }
}

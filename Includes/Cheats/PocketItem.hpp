#pragma once

// PocketItem — ポケットアイテム（gohan.md §6 / §17.5）。入力した ID を持ち物の一番近い空き枠へ入れる。
// ゲームの関数はゲームのスレッドで呼ぶ（グリッドカーソルの毎フレームのフックへ相乗り）。

#include <3ds/types.h>

namespace PocketItem {

enum class Result : u8 {
    Ok,
    Full,           // 空き枠が無い
    Invalid,        // ゲームのアイテムの表で持ち物に入れられない（存在しない ID など）
    NoPlayer,       // 今のプレイヤーのセーブが読めない（読み書き中）
    Busy,
    HookFailed,
    Unsupported,
    TimedOut,
};

// メニューのスレッドから。ゲームのスレッドが処理するまで最大 1 秒待つ。slot = 入れた枠（0〜15）
Result Request(u16 id, int &slot, bool &iconUpdated);
const char *ResultName(Result result);

}  // namespace PocketItem

namespace CTRPluginFramework
{
    namespace Cheats
    {
        void    WirePocketItem(void);
    }
}

// ============================================================================
// PlayerResources — 所持金・貯金・ふるさとチケット・メダル（gohan.md §4）と店のカブ価（§12.5）
// ============================================================================
//
// どれもゲームの「暗号化された値」（8 バイト。u32 本体 / u16 / u8 シフト / u8 検査和）。
// 読み書きはゲーム自身の関数で行う（連動型: メニューを開いた時に 1 回読み、適用で書く）。
//
// 番地（JPN 無印＋更新版。2026-09-17 の静的な読み取り。実機未確認）:
//   0x00303530 DecryptValue(EncVal*) -> int     Vapecord MONEYGET を JPN 列で変換
//   0x00303404 EncryptValue(EncVal*, int)        Vapecord MONEYSET を JPN 列で変換
//   0x002FEB60 GetSaveOffset(4) -> 自分のプレイヤーのセーブ   Vapecord PSOFFSET
//              （自分の番号なら [0x00AA914C]、ほかは 0x002FB920 のセーブ枠。どちらも同じ構造）
//   0x002FB354 セーブ全体の先頭 [0x00955F8C]                  Vapecord D_GARDEN
// 欄（プレイヤーのセーブからの距離）:
//   所持金 +0x6F08 / 貯金 +0x6B8C / メダル +0x6B9C / ふるさとチケット +0x8D1C
//   ゲーム自身が同じ距離で DecryptValue を呼んでいる（例: 0x0019C9AC の sub_2FB900()+0x6F08）。
//   Vapecord の構造体注記（0x6FA8 / 0x6C2C / 0x6C3C / 0x8DBC）は全体の先頭からの距離で、
//   1 人目のセーブ枠が全体の +0xA0 にある（0x002FAF48 +0x20）ぶんだけずれて一致する。
// カブ価（店のカブの値段。プレイヤーの資産ではない）: 全体の先頭 +0x6ADE0 から 12 個（午前 6 / 午後 6）。
//   書くときは 12 個とも同じ値、読むのは先頭。

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "Cheats.hpp"
#include "GuiMenu.hpp"

namespace CTRPluginFramework
{
    namespace Cheats
    {
        namespace
        {
            const u32   kDecrypt       = 0x00303530;
            const u32   kEncrypt       = 0x00303404;
            const u32   kGetSaveOffset = 0x002FEB60;
            const u32   kGardenPtr     = 0x00955F8C;
            const u32   kOwnPlayer     = 4;         // GetSaveOffset: 4 以上は自分の番号

            const u32   kOffWallet     = 0x6F08;
            const u32   kOffBank       = 0x6B8C;
            const u32   kOffMedals     = 0x6B9C;
            const u32   kOffCoupons    = 0x8D1C;
            const u32   kOffTurnips    = 0x6ADE0;   // セーブ全体の先頭から
            const int   kTurnipCount   = 12;
            const u32   kEncValBytes   = 8;

            typedef int  (*DecryptFn)(u8 *value);
            typedef void (*EncryptFn)(u8 *value, int amount);
            typedef u32  (*SaveOffsetFn)(u32 index);

            struct Resource
            {
                const char *label;
                bool        town;       // true: セーブ全体の先頭から / false: 自分のプレイヤーから
                u32         offset;
                s32         maximum;
                int         index;
            };

            Resource g_resources[] = {
                { kWallet,  false, kOffWallet,  99999,     -1 },
                { kBank,    false, kOffBank,    999999999, -1 },
                { kCoupons, false, kOffCoupons, 9999,      -1 },
                { kMedals,  false, kOffMedals,  9999,      -1 },
                { kTurnips, true,  kOffTurnips, 99999,     -1 },
            };

            const int kResourceCount = (int)(sizeof(g_resources) / sizeof(g_resources[0]));

            Resource *Find(int index)
            {
                for (int i = 0; i < kResourceCount; i++)
                    if (g_resources[i].index == index)
                        return &g_resources[i];
                return nullptr;
            }

            // 値の先頭。セーブが読み込まれていなければ 0
            u8      *Base(const Resource &r)
            {
                const u32 base = r.town ? *(u32 *)kGardenPtr : ((SaveOffsetFn)kGetSaveOffset)(kOwnPlayer);

                return base == 0 ? nullptr : (u8 *)(base + r.offset);
            }

            // DecryptValue（0x00303530）と同じ検査和。合わない値は読めないものとして扱う
            // （DecryptValue は不一致でも 0 を返すので、0 と区別できない）。
            bool    ChecksumOk(const u8 *value)
            {
                const u32 lo = (u32)value[0] | ((u32)value[1] << 8) | ((u32)value[2] << 16) | ((u32)value[3] << 24);
                const u8  sum = (u8)(lo + (lo >> 24) + (lo >> 16) + (lo >> 8) + 0xBA);

                return value[7] == sum;
            }

            bool    Read(int index, s32 *value)
            {
                const Resource *r = Find(index);
                u8             *p = r != nullptr ? Base(*r) : nullptr;

                if (p == nullptr || !ChecksumOk(p))
                    return false;

                const int v = ((DecryptFn)kDecrypt)(p);

                if (v < 0 || v > r->maximum)
                    return false;
                *value = (s32)v;
                return true;
            }

            void    Write(int index, s32 value)
            {
                const Resource *r = Find(index);
                u8             *p = r != nullptr ? Base(*r) : nullptr;

                if (p == nullptr || value < 0 || value > r->maximum)
                    return;

                const int count = r->town ? kTurnipCount : 1;

                for (int i = 0; i < count; i++)
                    ((EncryptFn)kEncrypt)(p + i * kEncValBytes, (int)value);
            }
        }

        void    WirePlayerResources(void)
        {
            for (int i = 0; i < kResourceCount; i++)
            {
                g_resources[i].index = GuiMenu::FindItem(g_resources[i].label);
                if (g_resources[i].index >= 0)
                    GuiMenu::RegisterLinked(g_resources[i].index, Read, Write);
            }
        }
    }
}

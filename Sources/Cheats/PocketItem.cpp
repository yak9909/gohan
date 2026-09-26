// ============================================================================
// PocketItem — ポケットアイテム（gohan.md §6 / §17.5）
// ============================================================================
//
// 入力したアイテム ID を、持ち物の index 0 から一番近い空き枠へ入れる。持ち物を開いていれば、その枠のアイコンも作り直す。
// ★ゲーム自身の処理だけを使う（解析 IDA-opus-5.5-F047）:
//   - 入れる: Inventory_AddItem 0x2BFCD4(持ち物, &アイテム, 印 0, 図鑑登録 0)。16 枠を先頭から見て最初の空き枠へ書く。
//     書く前にゲームのアイテムの表で確かめ（sub_6B9598: 表に記録があり「持てる」印 bit 0x10 があること）、だめなら書かずに 0。
//     プレイヤーが物を拾うときもこれを通る（vc_WRITEITEM 0x64E8E4）。図鑑登録（第 4 引数）は拾ったときの扱いなので 0 にする。
//   - 空き枠: Inventory_FindEmptySlot 0x723604（上と同じ判定。無ければ −1）。いっぱいと無効を見分けるのに使う。
//   - 持ち物 = 今のプレイヤー（sub_2FB900。セーブの読み書き中は null）+ 27600（16 枠 x 4 B）。
//   - アイコン: vc_LOADICON 0x26DB1C(BsMenuItem + 7872, 枠)。ゲームも枠を書いたあとに呼ぶ（sub_199550 など）。
//     下画面の画面 = *(*(0x986500) + 0xC)。vtable が BsMenuItem（0x8E5220）でメニュー番号（+0x24）が 0 のときだけ呼ぶ。
// ゲームの関数はゲームのスレッド（グリッドカーソルの毎フレームのフック）から呼ぶ。メニューのスレッドは頼んで最大 1 秒待つ。

#include "PocketItem.hpp"

#include "Cheats.hpp"
#include "GridCursor.hpp"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstdio>

namespace PocketItem {

namespace {

const u32 kInventoryAdd      = 0x002BFCD4;  // Inventory_AddItem(inv, &item, lockByte, register)
const u32 kInventoryAddOrig  = 0xE92D40F8;
const u32 kFindEmptySlot     = 0x00723604;  // Inventory_FindEmptySlot(inv) -> 枠 / -1
const u32 kFindEmptySlotOrig = 0xE92D41F0;
const u32 kLoadIcon          = 0x0026DB1C;  // vc_LOADICON(grid, 枠)
const u32 kLoadIconOrig      = 0xE92D4070;
const u32 kCurrentPlayer     = 0x002FB900;  // -> 今のプレイヤーのセーブ（読み書き中は null）
const u32 kCurrentPlayerOrig = 0xE92D4010;
const u32 kItemInit          = 0x002FCBB4;  // Item_InitWithId(&item, id)
const u32 kItemInitOrig      = 0xE92D4010;
const u32 kInventoryOffset   = 27600;       // 0x6BD0
const u32 kScreenListPtr     = 0x00986500;  // vc_INVMENU（0 = 下画面の画面なし）
const u32 kScreenOffset      = 0x0C;
const u32 kScreenMenuId      = 0x24;        // u8: 0 = 持ち物
const u32 kVtBsMenuItem      = 0x008E5220;
const u32 kIconGridOffset    = 7872;        // 0x1EC0（BsMenuItem_Ctor が sub_270110 で作る）
const u16 kEmptyId           = 0x7FFE;

typedef int (*AddFn)(u32 inv, u32 *item, u32 lockByte, u32 registerCatalog);
typedef int (*FindFn)(u32 inv);
typedef int (*LoadIconFn)(u32 grid, u32 slot);
typedef u32 (*PlayerFn)(void);
typedef u32 *(*InitFn)(u32 *item, u32 id);

inline u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
inline u8 R8(u32 a) { return *reinterpret_cast<const volatile u8 *>(a); }

bool CodeMatches(void) {
    static const u32 kWords[][2] = {
        { kInventoryAdd, kInventoryAddOrig }, { kFindEmptySlot, kFindEmptySlotOrig }, { kLoadIcon, kLoadIconOrig },
        { kCurrentPlayer, kCurrentPlayerOrig }, { kItemInit, kItemInitOrig },
    };
    if (CTRPluginFramework::Process::GetTitleID() != 0x0004000000086200ULL)
        return false;
    for (const auto &w : kWords)
        if (R32(w[0]) != w[1])
            return false;
    return true;
}

// 要求（メニューのスレッドが書き、ゲームのスレッドが拾う）
volatile u32 s_reqSeq;
volatile u32 s_doneSeq;
volatile u32 s_reqId;
volatile Result s_result;
volatile int s_slot;
volatile bool s_iconUpdated;
bool s_hooked;

Result Execute(u16 id, int &slot, bool &icon) {
    slot = -1;
    icon = false;
    if ((id & 0x7FFF) == kEmptyId)
        return Result::Invalid;

    const u32 player = reinterpret_cast<PlayerFn>(kCurrentPlayer)();

    if (player == 0)
        return Result::NoPlayer;

    const u32 inv = player + kInventoryOffset;
    const int empty = reinterpret_cast<FindFn>(kFindEmptySlot)(inv);

    if (empty < 0 || empty >= 16)
        return Result::Full;

    u32 item = 0;

    reinterpret_cast<InitFn>(kItemInit)(&item, id);
    if (reinterpret_cast<AddFn>(kInventoryAdd)(inv, &item, 0, 0) == 0)
        return Result::Invalid;                 // 空きはあった = ゲームの表の確かめで落ちた
    slot = empty;                               // 入れる処理も同じ判定で先頭から探す

    // 持ち物を開いていれば、その枠のアイコンをゲームの処理で作り直す
    const u32 holder = R32(kScreenListPtr);

    if (holder >= 0x08000000u && holder < 0x40000000u) {
        const u32 screen = R32(holder + kScreenOffset);

        if (screen >= 0x08000000u && screen < 0x40000000u && R32(screen) == kVtBsMenuItem
            && R8(screen + kScreenMenuId) == 0) {
            reinterpret_cast<LoadIconFn>(kLoadIcon)(screen + kIconGridOffset, (u32)slot);
            icon = true;
        }
    }
    return Result::Ok;
}

// ゲームのスレッド（グリッドカーソルのフック）から毎フレーム
void FrameStep(void) {
    const u32 seq = s_reqSeq;

    if (seq == s_doneSeq)
        return;

    int slot = -1;
    bool icon = false;

    s_result = Execute((u16)s_reqId, slot, icon);
    s_slot = slot;
    s_iconUpdated = icon;
    s_doneSeq = seq;
}

}  // namespace

Result Request(u16 id, int &slot, bool &iconUpdated) {
    slot = -1;
    iconUpdated = false;
    if (!CodeMatches())
        return Result::Unsupported;
    if (!s_hooked) {
        if (!GridCursor::InstallFrameHook() || !GridCursor::AddExtraFrameStep(FrameStep))
            return Result::HookFailed;
        s_hooked = true;
    }
    if (s_reqSeq != s_doneSeq)
        return Result::Busy;
    s_reqId = id;
    const u32 seq = s_reqSeq + 1;
    s_reqSeq = seq;                             // 最後に書く。これでゲームのスレッドが拾う
    // 30fps なので 1〜2 フレームで終わる。念のため 1 秒まで待つ（PublicWorks::Request と同じ）
    for (u32 i = 0; i < 60; ++i) {
        svcSleepThread(16666667LL);
        if (s_doneSeq == seq) {
            slot = s_slot;
            iconUpdated = s_iconUpdated;
            return s_result;
        }
    }
    return Result::TimedOut;
}

const char *ResultName(Result result) {
    switch (result) {
    case Result::Ok: return u8"入れました";
    case Result::Full: return u8"持ち物に空きがありません";
    case Result::Invalid: return u8"持ち物に入れられないアイテムです";
    case Result::NoPlayer: return u8"プレイヤーのデータを読めません";
    case Result::Busy: return u8"前の要求を処理中です";
    case Result::HookFailed: return u8"フックを入れられません";
    case Result::Unsupported: return u8"対応していない版です";
    case Result::TimedOut: return u8"ゲームが応答しません";
    }
    return u8"?";
}

}  // namespace PocketItem

// ---------------------------------------------------------------------------------------
// gohan のメニューとの結び付け（root/アイテム/ポケットアイテム）
// ---------------------------------------------------------------------------------------

namespace CTRPluginFramework
{
    namespace Cheats
    {
        namespace
        {
            void    PocketApply(int index, s32 value)
            {
                int         slot = -1;
                bool        icon = false;
                const u16   id = (u16)(value & 0xFFFF);
                const PocketItem::Result r = PocketItem::Request(id, slot, icon);
                char        buf[96];

                (void)index;
                if (r == PocketItem::Result::Ok)
                {
                    std::snprintf(buf, sizeof(buf), u8"0x%04X をスロット %d に入れました", (unsigned)id, slot);
                    GuiNotification::Notify(kPocketItem, buf);
                }
                else
                {
                    std::snprintf(buf, sizeof(buf), u8"0x%04X: %s", (unsigned)id, PocketItem::ResultName(r));
                    GuiNotification::NotifyRed(kPocketItem, buf);
                }
            }
        }

        void    WirePocketItem(void)
        {
            const int index = GuiMenu::FindItem(kPocketItem);

            if (index >= 0)
                GuiMenu::RegisterApply(index, PocketApply);
        }
    }
}

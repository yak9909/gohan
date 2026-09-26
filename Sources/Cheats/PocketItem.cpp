// ============================================================================
// PocketItem — ポケットアイテム（gohan.md §6 / §17.5）と没アイテム表示（§6、Issue #15）
// ============================================================================
//
// ポケットアイテム: 入力したアイテム ID を、持ち物の index 0 から一番近い空き枠へそのまま書く。持ち物を開いていれば、その枠のアイコンも作り直す。
//   ★ゲームの「持ち物に入れる」処理（Inventory_AddItem 0x2BFCD4）は使わない（2026-09-27 利用者の報告: 0xFF 以下などが入らないのに
//     「入れました」と出た）。書く前の Item_NormalizeForPocket 0x6B9598 → sub_6B8EB4 が 0xFD 以下の ID を別の ID か空（0x7FFE）へ写し、
//     空へ写しても「書けた」を返すため（IDA-opus-5.5-F048）。持ち物に表示されない物（没アイテム）も入れたいので、ID をそのまま書く。
//   - 空き枠: 没アイテム表示が「表示」のときは生の中身が空（Item_IsEmpty 0x2FCB24）の枠（見えている没アイテムを上書きしない）。
//     「非表示」のときはゲームの空き判定 Inventory_FindEmptySlot 0x723604（持てない物が入った枠も空とみなす。物を拾ったときと同じ。
//     利用者の指示 2026-09-27）。
//   - 書くもの: アイテム 4 B（Item_InitWithId 0x2FCBB4 で ID から組む）と、枠ごとの印（持ち物 +64 + 枠）= 0。
//   - 持ち物 = 今のプレイヤー（Save_GetCurrentPlayer 0x2FB900。セーブの読み書き中は null）+ 27600（16 枠 x 4 B）。
//   - アイコン: vc_LOADICON 0x26DB1C(BsMenuItem + 7872, 枠)。下画面の画面 = *(*(0x986500) + 0xC)。vtable が BsMenuItem（0x8E5220）で
//     メニュー番号（+0x24）が 0 のときだけ呼ぶ（IDA-opus-5.5-F047）。
// 没アイテム表示: Inventory_GetSlot 0x723878 が「持てない物が入った枠」に空（ItemToPlace）を返す分岐 0x7238C0（BEQ 0A000001）を NOP にする
//   （公開チートと同じ 1 語。IDA-opus-5.5-F048）。切り替えたとき持ち物を開いていれば 16 枠のアイコンを作り直す。
// ゲームの関数はゲームのスレッド（グリッドカーソルの毎フレームのフック）から呼ぶ。メニューのスレッドは頼んで最大 1 秒待つ。

#include "PocketItem.hpp"

#include "ItemNames.hpp"

#include "Cheats.hpp"
#include "GridCursor.hpp"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstdio>

namespace PocketItem {

namespace {

const u32 kLoadIcon          = 0x0026DB1C;  // vc_LOADICON(grid, 枠)
const u32 kLoadIconOrig      = 0xE92D4070;
const u32 kCurrentPlayer     = 0x002FB900;  // Save_GetCurrentPlayer -> 今のプレイヤーのセーブ（読み書き中は null）
const u32 kCurrentPlayerOrig = 0xE92D4010;
const u32 kItemInit          = 0x002FCBB4;  // Item_InitWithId(&item, id)
const u32 kItemInitOrig      = 0xE92D4010;
const u32 kItemIsEmpty       = 0x002FCB24;  // Item_IsEmpty(&item)
const u32 kItemIsEmptyOrig   = 0xE1D010B0;  // LDRH R1,[R0]
const u32 kFindEmptySlot     = 0x00723604;  // Inventory_FindEmptySlot(inv) -> 枠 / -1（拾ったときと同じ判定）
const u32 kFindEmptySlotOrig = 0xE92D41F0;
const u32 kHiddenBranch      = 0x007238C0;  // Inventory_GetSlot の BEQ（持てない物 → 空を返す）
const u32 kHiddenBranchOrig  = 0x0A000001;
const u32 kHiddenBranchShow  = 0xE1A00000;  // NOP
const u32 kInventoryOffset   = 27600;       // 0x6BD0
const u32 kSlotFlagOffset    = 64;          // 持ち物 +64 + 枠 = 枠ごとの印
const u32 kSlots             = 16;
const u32 kScreenListPtr     = 0x00986500;  // vc_INVMENU（0 = 下画面の画面なし）
const u32 kScreenOffset      = 0x0C;
const u32 kScreenMenuId      = 0x24;        // u8: 0 = 持ち物
const u32 kVtBsMenuItem      = 0x008E5220;
const u32 kIconGridOffset    = 7872;        // 0x1EC0（BsMenuItem_Ctor が sub_270110 で作る）
const u16 kEmptyId           = 0x7FFE;

typedef int (*LoadIconFn)(u32 grid, u32 slot);
typedef u32 (*PlayerFn)(void);
typedef u32 *(*InitFn)(u32 *item, u32 id);
typedef int (*IsEmptyFn)(u32 item);
typedef int (*FindFn)(u32 inv);

inline u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
inline u8 R8(u32 a) { return *reinterpret_cast<const volatile u8 *>(a); }

bool CodeMatches(void) {
    static const u32 kWords[][2] = {
        { kLoadIcon, kLoadIconOrig }, { kCurrentPlayer, kCurrentPlayerOrig }, { kItemInit, kItemInitOrig },
        { kItemIsEmpty, kItemIsEmptyOrig }, { kFindEmptySlot, kFindEmptySlotOrig },
    };
    if (CTRPluginFramework::Process::GetTitleID() != 0x0004000000086200ULL)
        return false;
    for (const auto &w : kWords)
        if (R32(w[0]) != w[1])
            return false;
    const u32 branch = R32(kHiddenBranch);
    return branch == kHiddenBranchOrig || branch == kHiddenBranchShow;
}

enum Op : u32 { OP_ADD = 1, OP_REFRESH = 2 };

// 要求（メニューのスレッドが書き、ゲームのスレッドが拾う）
volatile u32 s_reqSeq;
volatile u32 s_doneSeq;
volatile u32 s_reqOp;
volatile u32 s_reqId;
volatile Result s_result;
volatile int s_slot;
volatile bool s_iconUpdated;
bool s_hooked;

// 持ち物を開いていれば、その画面のアイコンの格子（無ければ 0）
u32 IconGrid(void) {
    const u32 holder = R32(kScreenListPtr);

    if (holder < 0x08000000u || holder >= 0x40000000u)
        return 0;

    const u32 screen = R32(holder + kScreenOffset);

    if (screen < 0x08000000u || screen >= 0x40000000u || R32(screen) != kVtBsMenuItem || R8(screen + kScreenMenuId) != 0)
        return 0;
    return screen + kIconGridOffset;
}

Result Add(u16 id, int &slot, bool &icon) {
    slot = -1;
    icon = false;
    if ((id & 0x7FFF) == kEmptyId)
        return Result::Invalid;

    const u32 player = reinterpret_cast<PlayerFn>(kCurrentPlayer)();

    if (player == 0)
        return Result::NoPlayer;

    const u32 inv = player + kInventoryOffset;

    if (R32(kHiddenBranch) == kHiddenBranchShow) {
        for (u32 i = 0; i < kSlots && slot < 0; ++i)
            if (reinterpret_cast<IsEmptyFn>(kItemIsEmpty)(inv + 4 * i))
                slot = (int)i;
    } else
        slot = reinterpret_cast<FindFn>(kFindEmptySlot)(inv);
    if (slot < 0 || slot >= (int)kSlots)
        return Result::Full;

    u32 item = 0;

    reinterpret_cast<InitFn>(kItemInit)(&item, id);
    *reinterpret_cast<volatile u32 *>(inv + 4 * (u32)slot) = item;
    *reinterpret_cast<volatile u8 *>(inv + kSlotFlagOffset + (u32)slot) = 0;

    const u32 grid = IconGrid();

    if (grid != 0) {
        reinterpret_cast<LoadIconFn>(kLoadIcon)(grid, (u32)slot);
        icon = true;
    }
    return Result::Ok;
}

bool RefreshIcons(void) {
    const u32 grid = IconGrid();

    if (grid == 0)
        return false;
    for (u32 i = 0; i < kSlots; ++i)
        reinterpret_cast<LoadIconFn>(kLoadIcon)(grid, i);
    return true;
}

// ゲームのスレッド（グリッドカーソルのフック）から毎フレーム
void FrameStep(void) {
    const u32 seq = s_reqSeq;

    if (seq == s_doneSeq)
        return;

    int slot = -1;
    bool icon = false;

    if (s_reqOp == OP_ADD)
        s_result = Add((u16)s_reqId, slot, icon);
    else {
        icon = RefreshIcons();
        s_result = Result::Ok;
    }
    s_slot = slot;
    s_iconUpdated = icon;
    s_doneSeq = seq;
}

Result Run(u32 op, u16 id, int &slot, bool &iconUpdated) {
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
    s_reqOp = op;
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

}  // namespace

Result Request(u16 id, int &slot, bool &iconUpdated) {
    return Run(OP_ADD, id, slot, iconUpdated);
}

// ほかのパッチ型チートと同じ書き方（GuiMenu::PatchList*。書いたあとキャッシュを無効にする）
const CTRPluginFramework::GuiMenu::TogglePatch kHiddenPatch[] = {
    { kHiddenBranch, 4, kHiddenBranchShow, kHiddenBranchOrig },
};

bool HiddenShown(void) {
    return CTRPluginFramework::GuiMenu::PatchListIsActive(kHiddenPatch, 1);
}

Result SetHiddenShown(bool show) {
    if (!CodeMatches())
        return Result::Unsupported;
    if (HiddenShown() == show)
        return Result::Ok;
    CTRPluginFramework::GuiMenu::PatchListSetActive(kHiddenPatch, 1, show);

    // 持ち物を開いていれば 16 枠のアイコンを作り直す（開いていなければ何もしない）
    int slot = -1;
    bool icon = false;

    Run(OP_REFRESH, 0, slot, icon);
    return Result::Ok;
}

const char *ResultName(Result result) {
    switch (result) {
    case Result::Ok: return u8"入れました";
    case Result::Full: return u8"持ち物に空きがありません";
    case Result::Invalid: return u8"空のアイテム（0x7FFE）は入れられません";
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
// gohan のメニューとの結び付け（root/アイテム/ポケットアイテム・没アイテム表示）
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
                char        buf[160];

                (void)index;
                char        name[64];

                if (!ItemNames::NameUtf8(id, name, sizeof(name)))
                    name[0] = '\0';
                if (r == PocketItem::Result::Ok)
                {
                    if (name[0] != '\0')
                        std::snprintf(buf, sizeof(buf), u8"0x%04X %s をスロット %d に入れました", (unsigned)id, name, slot);
                    else
                        std::snprintf(buf, sizeof(buf), u8"0x%04X をスロット %d に入れました", (unsigned)id, slot);
                    GuiNotification::Notify(kPocketItem, buf);
                }
                else
                {
                    std::snprintf(buf, sizeof(buf), u8"0x%04X: %s", (unsigned)id, PocketItem::ResultName(r));
                    GuiNotification::NotifyRed(kPocketItem, buf);
                }
            }

            // 没アイテム表示（連動型リスト: 0 非表示 / 1 表示 / 2 自前表示 = 表示 + 自前の名前。アイコンは未実装）
            bool    HiddenRead(int index, s32 *value)
            {
                (void)index;
                *value = !PocketItem::HiddenShown() ? 0 : ItemNames::CustomNames() ? 2 : 1;
                return true;
            }

            void    HiddenWrite(int index, s32 value)
            {
                (void)index;

                const PocketItem::Result r = PocketItem::SetHiddenShown(value != 0);

                if (r != PocketItem::Result::Ok)
                {
                    GuiNotification::NotifyRed(kHiddenItems, PocketItem::ResultName(r));
                    return;
                }
                if (!ItemNames::SetCustomNames(value == 2))
                    GuiNotification::NotifyRed(kHiddenItems, u8"名前のフックを入れられません");
            }
        }

        void    WirePocketItem(void)
        {
            const int index = GuiMenu::FindItem(kPocketItem);
            const int hidden = GuiMenu::FindItem(kHiddenItems);

            if (index >= 0)
                GuiMenu::RegisterApply(index, PocketApply);
            if (hidden >= 0)
                GuiMenu::RegisterLinked(hidden, HiddenRead, HiddenWrite);
        }
    }
}

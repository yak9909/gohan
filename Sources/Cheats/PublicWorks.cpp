#include "PublicWorks.hpp"

#include "Cheats.hpp"
#include "GridCursor.hpp"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>

namespace PublicWorks {

namespace {

// ---- ゲーム側（JPN 無印 更新版。IDA-opus-5.5-F001 / F002）--------------------------------
// セーブの建物表。Save_GetTownBase 0x002FAF48 = [0x00955F8C] + 0x80 で、設置関数は
// TownBase + 0x4BE00（= garden + 0x4BE80）を渡す。sub_5CCE4C がそこから +8 の
// 4 B x 56 を走査し +4 の数を増やすので、Vapecord の ACNL_BuildingData と一致する。
const u32 kGarden = 0x00955F8C;             // u32: garden の先頭
const u32 kBuildingData = 0x4BE80;
const u32 kCountOffset = 0x04;              // u8 NormalPWPsAmount
const u32 kSlotsOffset = 0x08;              // ACNL_Building[56]

const u32 kCurrentRoom = 0x0095133A;        // u8: 0 = 村の屋外
const u32 kU0Data = 0x0096FC06;             // Vapecord U0DATA（JPN 列）

typedef void (*PlaceFn)(u32 x, u32 y, u32 id);                     // 0x002424F4
typedef void (*VoidFn)(void);
typedef const char *(*NameFn)(u32 id);                             // 0x005C8B44
typedef u32 (*GetPlayerFn)(u32 index, u32 one);                    // 0x005C27D8
typedef u8 (*GetIndexFn)(void);                                    // 0x00305F6C
typedef bool (*WorldCoordsFn)(u32 *x, u32 *y, u32 index, u32 one); // 0x005BFCE4
typedef void (*TileToWorldFn)(float *out, u32 x, u32 y);           // 0x006A6364
typedef u32 (*RoomDataFn)(void);                                   // 0x00308110
typedef u8 (*RoomIdFn)(void);                                      // 0x002F75CC
// Vapecord は (部屋データ, 部屋, 座標, U0DATA, 6, 0, 0, 1, 1) を**全部整数語**で渡している。
// IDA は第 5・第 9 引数を float と推定しているが、実績のある渡し方をそのまま写す。
typedef u32 (*ReloadFn)(u32 roomData, u32 room, const float *coords, const u32 *u0,
                        u32 a5, u32 a6, u32 a7, u32 a8, u32 a9);    // 0x005B4F98

const PlaceFn BuildingPlace = reinterpret_cast<PlaceFn>(0x002424F4);
// 区画の地形属性を素に戻して全建物を押し直す（ModuleIndoor / ModulePrologue が使う）
const VoidFn RebuildAttributes = reinterpret_cast<VoidFn>(0x00242878);
// 占有マップ 4 層を消して全建物分を書き直す（本体 15 か所が使う）
const VoidFn RebuildOccupancy = reinterpret_cast<VoidFn>(0x0024220C);
const NameFn BuildingGetName = reinterpret_cast<NameFn>(0x005C8B44);
const GetPlayerFn GetPlayer = reinterpret_cast<GetPlayerFn>(0x005C27D8);
const GetIndexFn GetOnlineIndex = reinterpret_cast<GetIndexFn>(0x00305F6C);
const WorldCoordsFn GetWorldCoords = reinterpret_cast<WorldCoordsFn>(0x005BFCE4);
const TileToWorldFn TileToWorld = reinterpret_cast<TileToWorldFn>(0x006A6364);
const RoomDataFn RoomData = reinterpret_cast<RoomDataFn>(0x00308110);
const RoomIdFn RoomId = reinterpret_cast<RoomIdFn>(0x002F75CC);
const ReloadFn ReloadRoom = reinterpret_cast<ReloadFn>(0x005B4F98);

const u32 kPlayerPosition = 0x14;           // float x, y, z

// ---- 要求（メニュー → 描画スレッド）------------------------------------------------------
enum class Op : u32 { None, Place, Remove, Move, Rebuild };

volatile Op s_op = Op::None;
volatile u32 s_argId;
volatile u32 s_argSlot;
volatile u32 s_argX;
volatile u32 s_argY;
volatile u32 s_seq;
volatile u32 s_doneSeq;
volatile Result s_result = Result::Ok;
bool s_hooked;

u8 *BuildingData(void) {
    const u32 garden = *reinterpret_cast<volatile u32 *>(kGarden);
    if (garden < 0x08000000u)
        return nullptr;
    return reinterpret_cast<u8 *>(garden + kBuildingData);
}

Slot *SlotAt(u32 index) {
    u8 *data = BuildingData();
    if (data == nullptr || index >= kSlots)
        return nullptr;
    return reinterpret_cast<Slot *>(data + kSlotsOffset + 4 * index);
}

bool IsPublicWorks(u16 id) {
    return id >= kFirstId && id <= kLastId;
}

bool IsDesignStand(u16 id) {
    return id == 0xDC || id == 0xDD;
}

const float *PlayerPosition(void) {
    const u32 player = GetPlayer(GetOnlineIndex(), 1);
    if (player == 0)
        return nullptr;
    return reinterpret_cast<const float *>(player + kPlayerPosition);
}

// 描画スレッド。見た目は部屋を読み直さないと変わらないので、最後に必ず読み直す。
void Reload(const float *coords) {
    if (coords == nullptr)
        return;
    ReloadRoom(RoomData(), RoomId(), coords, reinterpret_cast<const u32 *>(kU0Data),
               6, 0, 0, 1, 1);
}

Result Execute(Op op) {
    if (*reinterpret_cast<volatile u8 *>(kCurrentRoom) != 0)
        return Result::NotInVillage;
    u8 *data = BuildingData();
    if (data == nullptr)
        return Result::NoSaveData;
    const float *here = PlayerPosition();
    if (here == nullptr)
        return Result::NoPlayer;

    switch (op) {
    case Op::Place: {
        // ゲームの設置関数。足元の属性・セーブの表・占有まで自分でやる。
        BuildingPlace(s_argX, s_argY, s_argId);
        // 足元に建ったものの中へ閉じ込めないよう、Vapecord と同じく 2 マス手前へ出す。
        float out[3];
        TileToWorld(out, s_argX, s_argY + 2);
        Reload(out);
        return Result::Ok;
    }
    case Op::Remove: {
        Slot *slot = SlotAt(s_argSlot);
        if (slot == nullptr)
            return Result::NoSaveData;
        if (!IsPublicWorks(slot->id))
            return Result::NotPublicWorks;
        if (IsDesignStand(slot->id))
            return Result::DesignStand;
        slot->id = kEmptyId;
        slot->x = 0;
        slot->y = 0;
        if (data[kCountOffset] != 0)
            --data[kCountOffset];
        break;
    }
    case Op::Move: {
        Slot *slot = SlotAt(s_argSlot);
        if (slot == nullptr)
            return Result::NoSaveData;
        if (!IsPublicWorks(slot->id))
            return Result::NotPublicWorks;
        if (IsDesignStand(slot->id))
            return Result::DesignStand;
        slot->x = (u8)s_argX;
        slot->y = (u8)s_argY;
        break;
    }
    case Op::Rebuild:
        break;
    default:
        return Result::Ok;
    }

    // ★ここが Vapecord に無い部分。表を書き換えただけでは古い場所の属性と占有が残る。
    RebuildAttributes();
    RebuildOccupancy();
    Reload(here);
    return Result::Ok;
}

bool EnsureHook(void) {
    if (s_hooked)
        return true;
    if (!GridCursor::InstallFrameHook())
        return false;
    if (!GridCursor::AddExtraFrameStep(FrameStep))
        return false;
    s_hooked = true;
    return true;
}

Result Request(Op op) {
    if (!EnsureHook())
        return Result::HookFailed;
    if (s_op != Op::None)
        return Result::Busy;
    const u32 seq = s_seq + 1;
    s_seq = seq;
    s_op = op;                                      // 最後に書く。これで描画スレッドが拾う
    // 30fps なので 1〜2 フレームで終わる。念のため 1 秒まで待つ。
    for (u32 i = 0; i < 60; ++i) {
        svcSleepThread(16666667LL);
        if (s_doneSeq == seq)
            return s_result;
    }
    return Result::TimedOut;
}

}  // namespace

void FrameStep(void) {
    const Op op = s_op;
    if (op == Op::None)
        return;
    s_result = Execute(op);
    s_doneSeq = s_seq;
    s_op = Op::None;
}

bool ReadSlot(u32 index, Slot &out) {
    const Slot *slot = SlotAt(index);
    if (slot == nullptr)
        return false;
    out = *slot;
    return true;
}

const char *NameOf(u16 id) {
    if (id >= kEmptyId)
        return "";
    const char *name = BuildingGetName(id);
    return name != nullptr ? name : "";
}

bool PlayerTile(u32 &x, u32 &y) {
    x = 0;
    y = 0;
    return GetWorldCoords(&x, &y, GetOnlineIndex(), 1);
}

s32 Nearest(void) {
    u32 px = 0, py = 0;
    if (!PlayerTile(px, py))
        return -1;
    s32 best = -1;
    u32 bestDistance = 0xFFFFFFFFu;
    for (u32 i = 0; i < kSlots; ++i) {
        const Slot *slot = SlotAt(i);
        if (slot == nullptr || !IsPublicWorks(slot->id))
            continue;
        const s32 dx = (s32)slot->x - (s32)px;
        const s32 dy = (s32)slot->y - (s32)py;
        const u32 distance = (u32)(dx * dx + dy * dy);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = (s32)i;
        }
    }
    return best;
}

Result Place(u8 id) {
    if (!IsPublicWorks(id))
        return Result::InvalidId;
    if (IsDesignStand(id))
        return Result::DesignStand;
    u8 *data = BuildingData();
    if (data == nullptr)
        return Result::NoSaveData;
    bool free = false;
    for (u32 i = 0; i < kSlots && !free; ++i) {
        const Slot *slot = SlotAt(i);
        free = slot != nullptr && slot->id >= kEmptyId;
    }
    if (!free)
        return Result::NoFreeSlot;
    u32 x = 0, y = 0;
    if (!PlayerTile(x, y))
        return Result::NoPlayer;
    s_argId = id;
    s_argX = x;
    s_argY = y;
    return Request(Op::Place);
}

Result Remove(u32 slot) {
    if (slot >= kSlots)
        return Result::NoSelection;
    s_argSlot = slot;
    return Request(Op::Remove);
}

Result MoveToPlayer(u32 slot) {
    if (slot >= kSlots)
        return Result::NoSelection;
    u32 x = 0, y = 0;
    if (!PlayerTile(x, y))
        return Result::NoPlayer;
    s_argSlot = slot;
    s_argX = x;
    s_argY = y;
    return Request(Op::Move);
}

Result Rebuild(void) {
    return Request(Op::Rebuild);
}

const char *ResultName(Result result) {
    switch (result) {
    case Result::Ok: return u8"完了";
    case Result::NotInVillage: return u8"村の屋外ではありません";
    case Result::NoSaveData: return u8"建物表が読めません";
    case Result::NoPlayer: return u8"プレイヤーが取れません";
    case Result::InvalidId: return u8"公共事業ではありません";
    case Result::NoFreeSlot: return u8"建物の空きがありません";
    case Result::NoSelection: return u8"建物を選んでいません";
    case Result::NotPublicWorks: return u8"選んだものは公共事業ではありません";
    case Result::DesignStand: return u8"マイデザインの看板は未対応です";
    case Result::HookFailed: return u8"フックが入れられません";
    case Result::Busy: return u8"前の処理が終わっていません";
    case Result::TimedOut: return u8"描画スレッドが応答しません";
    }
    return u8"?";
}

}  // namespace PublicWorks

// ---------------------------------------------------------------------------------------
// gohan のメニューとの結び付け（gohan.md「テスト」フォルダ）
// ---------------------------------------------------------------------------------------

namespace CTRPluginFramework
{
    namespace Cheats
    {
        namespace
        {
            const u32   kCount = PublicWorks::kLastId - PublicWorks::kFirstId + 1;

            const char *g_names[kCount];
            int         g_pickIndex = -1;
            s32         g_selected = -1;

            void    Report(const char *title, PublicWorks::Result result)
            {
                if (result == PublicWorks::Result::Ok)
                    GuiNotification::Notify(title, PublicWorks::ResultName(result));
                else
                    GuiNotification::NotifyRed(title, PublicWorks::ResultName(result));
            }

            void    PlaceExecute(int index)
            {
                (void)index;
                const int pick = g_pickIndex >= 0 ? GuiMenu::ItemApplied(g_pickIndex) : -1;

                if (pick < 0 || (u32)pick >= kCount)
                    return;
                Report(kPwPlace, PublicWorks::Place((u8)(PublicWorks::kFirstId + pick)));
            }

            void    NearestExecute(int index)
            {
                (void)index;
                static char message[96];

                g_selected = PublicWorks::Nearest();
                PublicWorks::Slot slot;
                if (g_selected < 0 || !PublicWorks::ReadSlot((u32)g_selected, slot))
                {
                    GuiNotification::NotifyRed(kPwNearest, u8"近くに公共事業がありません");
                    return;
                }
                const char *name = PublicWorks::NameOf(slot.id);
                if (std::strncmp(name, "fobj_", 5) == 0)
                    name += 5;
                std::snprintf(message, sizeof(message), u8"%ld番 %s (%u,%u)",
                              (long)g_selected, name, (unsigned)slot.x, (unsigned)slot.y);
                GuiNotification::Notify(kPwNearest, message);
            }

            void    RemoveExecute(int index)
            {
                (void)index;
                if (g_selected < 0)
                {
                    Report(kPwRemove, PublicWorks::Result::NoSelection);
                    return;
                }
                const PublicWorks::Result result = PublicWorks::Remove((u32)g_selected);
                if (result == PublicWorks::Result::Ok)
                    g_selected = -1;
                Report(kPwRemove, result);
            }

            void    MoveExecute(int index)
            {
                (void)index;
                if (g_selected < 0)
                {
                    Report(kPwMove, PublicWorks::Result::NoSelection);
                    return;
                }
                Report(kPwMove, PublicWorks::MoveToPlayer((u32)g_selected));
            }

            void    RebuildExecute(int index)
            {
                (void)index;
                Report(kPwRebuild, PublicWorks::Rebuild());
            }
        }

        void    WirePublicWorks(void)
        {
            // 名前はゲーム自身の表（0x00955700）から引く。"fobj_" を外して見せる。
            for (u32 i = 0; i < kCount; ++i)
            {
                const char *name = PublicWorks::NameOf((u16)(PublicWorks::kFirstId + i));
                if (std::strncmp(name, "fobj_", 5) == 0)
                    name += 5;
                g_names[i] = name[0] != '\0' ? name : u8"?";
            }
            g_pickIndex = GuiMenu::FindItem(kPwPick);
            if (g_pickIndex >= 0)
                GuiMenu::SetItemOptions(g_pickIndex, g_names, (int)kCount);

            const int place = GuiMenu::FindItem(kPwPlace);
            const int nearest = GuiMenu::FindItem(kPwNearest);
            const int remove = GuiMenu::FindItem(kPwRemove);
            const int move = GuiMenu::FindItem(kPwMove);
            const int rebuild = GuiMenu::FindItem(kPwRebuild);

            if (place >= 0)
                GuiMenu::RegisterExecute(place, PlaceExecute);
            if (nearest >= 0)
                GuiMenu::RegisterExecute(nearest, NearestExecute);
            if (remove >= 0)
                GuiMenu::RegisterExecute(remove, RemoveExecute);
            if (move >= 0)
                GuiMenu::RegisterExecute(move, MoveExecute);
            if (rebuild >= 0)
                GuiMenu::RegisterExecute(rebuild, RebuildExecute);
        }
    }
}

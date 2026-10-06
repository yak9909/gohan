// FurnitureTest — 家具をその場で消す・置く試験（T022 の下調べ。2026-10-06、IDA-opus-5.5-F102 / F103）
//
// 部屋のマップの品物を書くだけでは、家具は部屋を読み直すまで変わらなかった（利用者の実機、2026-10-06）。
// そこで、ゲームの品物の操作の入口 Item_PlaceItem 0x59E5B4（Vapecord PLACEITEMOFFSET、旧 F-14 / F-15）を
// ゲームのスレッド（GridCursor の毎フレームの相乗り）から呼び、その場で反映されるかを見る。
//   操作 6 = 消す（置く品物 = 空。家の中では空の代わりに 0x2001 を渡す。Vapecord Dropper の決まり）
//   操作 0xA = 置く
// 対象のマス = プレイヤーの目の前（Player_TryPickUpItemAt 0x66001C と同じ sub_762D80 + sub_317090）。
// 使った枠（0x9AE15C の 4 枠）は sub_596C84 で状態が 2 / 3 になったら空きへ戻す（プレイヤーの状態が普段やっていること）。

#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <cstdarg>
#include <cstdio>

#include "GridCursor.hpp"
#include "GridCursorGameApi.hpp"

namespace FurnitureTest {

using namespace CTRPluginFramework;

namespace {

struct Item {
    u16 id;
    u16 flags;
};

typedef void (*FrontPosFn)(void *actor, float *pos);
typedef void (*PosToCellFn)(int *x, int *z, const float *pos);
typedef void *(*MapMgrFn)(void);
typedef Item *(*ItemAtFn)(void *mgr, int x, int z, int layer);
typedef int (*PlaceItemFn)(int type, const Item *replace, const Item *place, const Item *show, u8 x, u8 z, u8 inside,
                           u8 side, u8 fruit, u8 crash, u8 hits);
typedef int (*DropStateFn)(int slot);

const FrontPosFn FrontPos = reinterpret_cast<FrontPosFn>(0x00762D80);
const PosToCellFn PosToCell = reinterpret_cast<PosToCellFn>(0x00317090);
const MapMgrFn MapMgr = reinterpret_cast<MapMgrFn>(0x006A53DC);       // Field_GetMapManager
const ItemAtFn ItemAt = reinterpret_cast<ItemAtFn>(0x002FEE38);       // Field_GetItemAtXY
const PlaceItemFn PlaceItem = reinterpret_cast<PlaceItemFn>(0x0059E5B4);   // Item_PlaceItem
const DropStateFn DropState = reinterpret_cast<DropStateFn>(0x00596C84);

const u16 kEmpty = 0x7FFE, kIndoorEmpty = 0x2001, kChair = 0x2AE9;   // rmk_smp_chairS（試験 1 で消した椅子と同じ）

volatile int s_request;      // 0 なし / 1 消す / 2 置く（メニューのスレッドが書く）
int s_slot = -1;             // 待っている枠
int s_wait;
bool s_hooked;
char s_result[160];
volatile bool s_resultReady;

void Report(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(s_result, sizeof(s_result), fmt, ap);
    va_end(ap);
    s_resultReady = true;
}

void FrameStep(void) {
    if (s_slot >= 0) {                                   // 前の操作の枠を片付ける
        const int st = DropState(s_slot);
        if (st == 0 || st == 2 || st == 3 || ++s_wait > 120) {
            Report("slot %d state %d after %d frames", s_slot, st, s_wait);
            s_slot = -1;
        }
        return;
    }
    const int req = s_request;
    if (req == 0)
        return;
    s_request = 0;
    void *actor = GridCursor::Game::LocalPlayer();
    void *mgr = MapMgr();
    if (actor == nullptr || mgr == nullptr) {
        Report("no actor %d or map %d", actor == nullptr, mgr == nullptr);
        return;
    }
    float pos[3] = { 0, 0, 0 };
    int x = -1, z = -1;
    FrontPos(actor, pos);
    PosToCell(&x, &z, pos);
    Item *at = ItemAt(mgr, x, z, 0);
    if (at == nullptr || x < 0 || z < 0) {
        Report("cell (%d,%d) has no item slot", x, z);
        return;
    }
    const Item before = *at;
    int slot;
    if (req == 1) {
        const Item empty = { kIndoorEmpty, 0 };
        slot = PlaceItem(6, &before, &empty, &empty, (u8)x, (u8)z, 0, 0, 0, 0, 0);
    } else {
        const Item replace = before.id == kEmpty ? Item{ kIndoorEmpty, 0 } : before;
        const Item chair = { kChair, 0 };
        slot = PlaceItem(0xA, &replace, &chair, &chair, (u8)x, (u8)z, 0, 0, 0, 0, 0);
    }
    s_slot = slot;
    s_wait = 0;
    Report("op %d at (%d,%d) item %04X -> slot %d", req == 1 ? 6 : 10, x, z, before.id, slot);
    if (slot < 0)
        s_slot = -1;
}

bool EnsureHook(void) {
    if (!s_hooked)
        s_hooked = GridCursor::InstallFrameHook() && GridCursor::AddExtraFrameStep(FrameStep);
    return s_hooked;
}

void Ask(int req) {
    if (!EnsureHook()) {
        OSD::Notify("FurnitureTest: frame hook failed");
        return;
    }
    s_resultReady = false;
    s_request = req;
    for (int i = 0; i < 100 && !s_resultReady; ++i)          // 最大 1 秒ほど結果を待つ
        Sleep(Milliseconds(10));
    OSD::Notify(s_resultReady ? s_result : "FurnitureTest: no result yet");
}

}  // namespace

void RemoveInFront(MenuEntry *) { Ask(1); }
void PlaceChairInFront(MenuEntry *) { Ask(2); }

}  // namespace FurnitureTest

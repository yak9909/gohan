// FurnitureTest — 家具をその場で消す・置く試験（T022 の下調べ。2026-10-06、IDA-opus-5.5-F102 / F103）
//
// 試験 1（部屋のマップの品物を書くだけ）は部屋を読み直すまで反映されなかった。
// 試験 2（Item_PlaceItem 0x59E5B4 の操作 0xA）は「落とす」になり、葉っぱの見た目で足元に落ちた（利用者、2026-10-06）。
// 試験 3（この版）:
//   置く = ゲームの「かざる」と同じ部品。ポケットから飾る sub_1A003C は
//     sub_690D24 → sub_691AD4（場所探し）→ 1 マスの試し sub_6920CC(記録, x, z, 向き, 品物, 層, プレイヤー x, z, 向き, 型) が 0 なら
//     sub_68FA84(記録)（g_BsFtrMgr の下へ家具のオブジェクトを Actor_SpawnByProfile）。
//     ここでは場所探しだけ自前（プレイヤーのマスから外へ、空いたマスを順に）にして、1 マスの試しと作成はゲームのまま。
//   消す = プレイヤーに一番近い家具の基準のマスへ Item_PlaceItem の操作 6（空 = 家の中では 0x2001。Vapecord Dropper の決まり）。
// どちらもゲームのスレッド（GridCursor の毎フレームの相乗り）で 1 回だけ実行し、結果を OSD に出す。

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

typedef void (*PosToCellFn)(int *x, int *z, const float *pos);
typedef void *(*MapMgrFn)(void);
typedef Item *(*ItemAtFn)(void *mgr, int x, int z, int layer);
typedef int (*PlaceItemFn)(int type, const Item *replace, const Item *place, const Item *show, u8 x, u8 z, u8 inside,
                           u8 side, u8 fruit, u8 crash, u8 hits);
typedef int (*DropStateFn)(int slot);
typedef int (*TryPutFn)(void *record, int x, int z, int rot, const Item *item, int layer, int px, int pz, int prot, int pattern);
typedef int (*SpawnFtrFn)(void *record);

const PosToCellFn PosToCell = reinterpret_cast<PosToCellFn>(0x00317090);
const MapMgrFn MapMgr = reinterpret_cast<MapMgrFn>(0x006A53DC);            // Field_GetMapManager
const ItemAtFn ItemAt = reinterpret_cast<ItemAtFn>(0x002FEE38);            // Field_GetItemAtXY
const PlaceItemFn PlaceItem = reinterpret_cast<PlaceItemFn>(0x0059E5B4);   // Item_PlaceItem
const DropStateFn DropState = reinterpret_cast<DropStateFn>(0x00596C84);
const TryPutFn TryPut = reinterpret_cast<TryPutFn>(0x006920CC);            // 1 マスの試し（置く記録を埋める）
const SpawnFtrFn SpawnFtr = reinterpret_cast<SpawnFtrFn>(0x0068FA84);      // 記録から家具のオブジェクトを作る

const u16 kEmpty = 0x7FFE, kOccupied = 0x7FFC, kIndoorEmpty = 0x2001, kChair = 0x2AE9;   // rmk_smp_chairS

volatile int s_request;      // 0 なし / 1 消す / 2 置く（メニューのスレッドが書く）
int s_slot = -1;             // 片付けを待っている落とし物の枠
int s_wait;
bool s_hooked;

void Say(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OSD::Notify(buf);
}

bool PlayerCell(void *actor, int &x, int &z, int &rot) {
    const float *pos = reinterpret_cast<const float *>(reinterpret_cast<u8 *>(actor) + GridCursor::Game::kPlayerPositionOffset);
    PosToCell(&x, &z, pos);
    const u16 angle = *reinterpret_cast<const u16 *>(reinterpret_cast<u8 *>(actor) + 46);
    rot = (int)((((u32)angle >> 12 << 28) + 0x20000000u) >> 30);      // sub_691AD4 と同じ式
    return x >= 0 && z >= 0;
}

void Remove(void *mgr, int px, int pz) {
    int best = -1, bx = 0, bz = 0;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            Item *it = ItemAt(mgr, x, z, 0);
            if (it == nullptr || it->id == kEmpty || it->id == kOccupied)
                continue;
            const int d = (x - px) * (x - px) + (z - pz) * (z - pz);
            if (best < 0 || d < best) {
                best = d;
                bx = x;
                bz = z;
            }
        }
    if (best < 0) {
        Say("remove: no furniture in the room map");
        return;
    }
    const Item before = *ItemAt(mgr, bx, bz, 0);
    const Item empty = { kIndoorEmpty, 0 };
    const int slot = PlaceItem(6, &before, &empty, &empty, (u8)bx, (u8)bz, 0, 0, 0, 0, 0);
    s_slot = slot;
    s_wait = 0;
    Say("remove (%d,%d) item %04X:%04X -> slot %d (player %d,%d)", bx, bz, before.id, before.flags, slot, px, pz);
}

void Place(int px, int pz, int prot) {
    const Item chair = { kChair, 0 };
    u8 record[12] = {};
    for (int r = 1; r <= 8; ++r)
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                if (dx != -r && dx != r && dz != -r && dz != r)
                    continue;                                // 輪の上だけ
                const int x = px + dx, z = pz + dz;
                if (x < 0 || z < 0 || x > 15 || z > 15)
                    continue;
                const int res = TryPut(record, x, z, prot, &chair, 0, px, pz, prot, 0);
                if (res != 0)
                    continue;
                const int spawned = SpawnFtr(record);
                Say("place (%d,%d) rot %d -> spawn %d (player %d,%d)", x, z, prot, spawned, px, pz);
                return;
            }
    Say("place: no free cell around (%d,%d)", px, pz);
}

void FrameStep(void) {
    if (s_slot >= 0) {                                       // 落とし物の枠を片付ける
        const int st = DropState(s_slot);
        if (st == 0 || st == 2 || st == 3 || ++s_wait > 120) {
            Say("remove: slot %d state %d after %d frames", s_slot, st, s_wait);
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
    int px = -1, pz = -1, prot = 0;
    if (actor == nullptr || mgr == nullptr || !PlayerCell(actor, px, pz, prot)) {
        Say("no player / map / cell (%d,%d)", px, pz);
        return;
    }
    if (req == 1)
        Remove(mgr, px, pz);
    else
        Place(px, pz, prot);
}

void Ask(int req) {
    if (!s_hooked)
        s_hooked = GridCursor::InstallFrameHook() && GridCursor::AddExtraFrameStep(FrameStep);
    if (!s_hooked) {
        OSD::Notify("FurnitureTest: frame hook failed");
        return;
    }
    s_request = req;                                         // メニューを閉じてゲームが動くと実行される
}

}  // namespace

void RemoveInFront(MenuEntry *) { Ask(1); }
void PlaceChairInFront(MenuEntry *) { Ask(2); }

}  // namespace FurnitureTest

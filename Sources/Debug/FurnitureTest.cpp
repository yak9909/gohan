// FurnitureTest — 家具をその場で消す・置く試験（T022 の下調べ。2026-10-06、IDA-opus-5.5-F102 / F103）
//
// 試験 1（部屋のマップの品物を書くだけ）は部屋を読み直すまで反映されなかった。
// 試験 2（Item_PlaceItem 0x59E5B4 の操作 0xA）は「落とす」になり、葉っぱの見た目で足元に落ちた（利用者、2026-10-06）。
// 試験 3（この版）:
//   置く = ゲームの「かざる」と同じ部品。ポケットから飾る sub_1A003C は
//     sub_690D24 → sub_691AD4（場所探し）→ 1 マスの試し sub_6920CC(記録, x, z, 向き, 品物, 層, プレイヤー x, z, 向き, 型) が 0 なら
//     sub_68FA84(記録)（g_BsFtrMgr の下へ家具のオブジェクトを Actor_SpawnByProfile）。
//     ここでは場所探しだけ自前（プレイヤーのマスから外へ、空いたマスを順に）にして、1 マスの試しと作成はゲームのまま。
//   消す（試験 3 は Item_PlaceItem 操作 6 で、その場で消えず読み直すとリンゴ 0x2001 になった）→ 試験 4:
//     ゲームが部屋の家具を消すときの手順（sub_4E4FD0）をそのまま使う。
//     マス → 家具のオブジェクト sub_4E8CF4(0x9C1AB4, x, z, 層) → vtable +144 が真なら sub_56FD10(家具, 6)。
//     片付け AcFtr_DestroyBegin（状態 6/7）→ sub_575F30 → sub_4E6240 が大きさの全マスを部屋のマップから空にする。
//   試験 4（利用者）: 即反映・8 フレームでマップ 7FFE。ただし上に載った物は浮いて残った
//     （片付けは層 1 を消さない: AcFtr_DestroyBegin 0x570554 で R2=0）。
//   試験 5（この版）: 土台のマスの層 1 にある家具のオブジェクトも先に状態 6 にする（各自の片付けが層 1 のデータを消す）。
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
typedef int (*TryPutFn)(void *record, int x, int z, int rot, const Item *item, int layer, int px, int pz, int prot, int pattern);
typedef int (*SpawnFtrFn)(void *record);
typedef void *(*FtrAtFn)(void *grid, int x, int z, int layer);
typedef int (*FtrSetStateFn)(void *ftr, unsigned state);
typedef int (*FtrReadyFn)(void *ftr);

const PosToCellFn PosToCell = reinterpret_cast<PosToCellFn>(0x00317090);
const MapMgrFn MapMgr = reinterpret_cast<MapMgrFn>(0x006A53DC);            // Field_GetMapManager
const ItemAtFn ItemAt = reinterpret_cast<ItemAtFn>(0x002FEE38);            // Field_GetItemAtXY
const TryPutFn TryPut = reinterpret_cast<TryPutFn>(0x006920CC);            // 1 マスの試し（置く記録を埋める）
const SpawnFtrFn SpawnFtr = reinterpret_cast<SpawnFtrFn>(0x0068FA84);      // 記録から家具のオブジェクトを作る
const FtrAtFn FtrAt = reinterpret_cast<FtrAtFn>(0x004E8CF4);                // マス → 家具のオブジェクト（無ければ 0）
const FtrSetStateFn FtrSetState = reinterpret_cast<FtrSetStateFn>(0x0056FD10);   // 家具の状態を替える（0..12）
void *const kFtrGrid = reinterpret_cast<void *>(0x009C1AB4);                // マス → 家具の番号の表

const u16 kEmpty = 0x7FFE, kChair = 0x2AE9;   // rmk_smp_chairS

volatile int s_request;      // 0 なし / 1 消す / 2 置く（メニューのスレッドが書く）
int s_watchX = -1, s_watchZ = -1;   // 消したあと部屋のマップが空になるのを見ているマス
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

bool Ready(void *ftr) {
    const FtrReadyFn ready = reinterpret_cast<FtrReadyFn>((*reinterpret_cast<u32 **>(ftr))[144 / 4]);
    return ready(ftr) != 0;
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
    void *ftr = nullptr;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            void *f = FtrAt(kFtrGrid, x, z, 0);
            if (f == nullptr)
                continue;
            const int d = (x - px) * (x - px) + (z - pz) * (z - pz);
            if (best < 0 || d < best) {
                best = d;
                bx = x;
                bz = z;
                ftr = f;
            }
        }
    if (ftr == nullptr) {
        Say("remove: no furniture object in the room");
        return;
    }
    const Item *it = ItemAt(mgr, bx, bz, 0);
    const Item before = it ? *it : Item{ 0, 0 };
    if (!Ready(ftr)) {
        Say("remove (%d,%d) item %04X: not ready (vtable+144 = 0)", bx, bz, before.id);
        return;
    }
    // 上に載っている物（土台のマスの層 1）を先に消す
    void *tops[16];
    int ntop = 0, nfail = 0;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            if (FtrAt(kFtrGrid, x, z, 0) != ftr)
                continue;
            void *t = FtrAt(kFtrGrid, x, z, 1);
            if (t == nullptr || t == ftr)
                continue;
            bool seen = false;
            for (int i = 0; i < ntop; ++i)
                seen = seen || tops[i] == t;
            if (seen || ntop >= 16)
                continue;
            tops[ntop++] = t;
        }
    for (int i = 0; i < ntop; ++i)
        if (!Ready(tops[i]) || !FtrSetState(tops[i], 6))
            ++nfail;
    const int res = FtrSetState(ftr, 6);
    s_watchX = bx;
    s_watchZ = bz;
    s_wait = 0;
    Say("remove (%d,%d) item %04X:%04X -> state 6 %d, on top %d (failed %d) (player %d,%d)", bx, bz, before.id,
        before.flags, res, ntop, nfail, px, pz);
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
    if (s_watchX >= 0) {                                     // 消したマスが部屋のマップから空になるまで見る
        void *m = MapMgr();
        const Item *it = m ? ItemAt(m, s_watchX, s_watchZ, 0) : nullptr;
        const Item *up = m ? ItemAt(m, s_watchX, s_watchZ, 1) : nullptr;
        const void *f = FtrAt(kFtrGrid, s_watchX, s_watchZ, 0);
        const void *fu = FtrAt(kFtrGrid, s_watchX, s_watchZ, 1);
        ++s_wait;
        if ((it != nullptr && it->id == kEmpty && f == nullptr && fu == nullptr) || s_wait > 180) {
            Say("remove: (%d,%d) map %04X / top %04X, object %s / top %s after %d frames", s_watchX, s_watchZ,
                it ? it->id : 0, up ? up->id : 0, f ? "still" : "gone", fu ? "still" : "gone", s_wait);
            s_watchX = s_watchZ = -1;
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

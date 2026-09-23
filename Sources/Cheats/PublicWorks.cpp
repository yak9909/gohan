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
// マイデザインの看板・顔出し看板の表（Vapecord DesignStand）。garden + 0x4BF70 から 8 枚。
// 1 枚 = パターン 0x870 B + x u32 + y u32 = 0x878 B。次の欄 UnlockedPWPs が 0x50330 なので
// (0x50330 - 0x4BF70) / 8 = 0x878 と合う。空きは x = y = 0xFFFFFFFF。
const u32 kStandsOffset = 0x4BF70 - 0x4BE80;
const u32 kStandBytes = 0x878;
const u32 kStandPattern = 0x870;
const u32 kStandX = 0x870;
const u32 kStandY = 0x874;
const u32 kStandEmpty = 0xFFFFFFFFu;

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

// ---- 村の建物の実体（IDA-opus-5.5-F004）--------------------------------------------------
// 村の建物は BsStrcMgr が読み込み段 2（sub_6DE198）で 1 件ずつ作る。その 1 件分を写す。
const u32 kStrcMgr = 0x0094A6C0;            // u32: BsStrcMgr の実体。部屋に居なければ 0

typedef u32 (*ProfileFn)(u32 id);                                   // 0x005C8CB0
typedef u32 (*ByteOfFn)(u32 id);                                    // 0x005C97A0
typedef bool (*IsFn)(u32 id);                                       // 0x006CBDB4
typedef float (*HeightFn)(const float *pos, u32 zero);              // 0x006C69C0（S0 で返る）
typedef u32 (*SpawnFn)(u32 profile, u32 parent, u32 param, const float *pos,
                       const u16 *rotation);                        // 0x0057ADFC
typedef void (*DestroyFn)(u32 actor);                               // 0x005200B8

const ProfileFn BuildingProfile = reinterpret_cast<ProfileFn>(0x005C8CB0);
// 村の読み込みはこれが 0 か 165 のものだけ作る（商店街の dobj_* は 1 で除外）
const ByteOfFn VillageFilter = reinterpret_cast<ByteOfFn>(0x005C97A0);
// 分類表 0x885FEC が 15（橋 0x90〜0xA7）。高さに +32 する
const IsFn IsBridge = reinterpret_cast<IsFn>(0x006CBDB4);
const HeightFn GroundHeight = reinterpret_cast<HeightFn>(0x006C69C0);
// 生成関数。位置の指す先を大域変数 0x947FE8 に置いたまま返るので、静的な領域を渡す
const SpawnFn SpawnActor = reinterpret_cast<SpawnFn>(0x0057ADFC);
// 基本マネージャに削除を要求する（Vapecord DESPAWN_INSECT と同じ関数）
const DestroyFn RequestDestroy = reinterpret_cast<DestroyFn>(0x005200B8);

const u32 kActorPosition = 0x14;            // float x, y, z（Actor 基底 ctor 0x57AE88）
const u32 kActorDestroying = 0x0F;          // u8: 削除要求済み
const u32 kActorBuildingId = 0x66;          // u8（CRO の 0xB7E5E8）
const u32 kActorHouseFlag = 0x5C0;          // u8: プレイヤーの家だけ（CRO の 0xB7E19C）
const float kBridgeLift = 32.0f;            // flt_6DE560
const u32 kNpcHouseTable = 0x008887A0;      // u8[10]: 住民の家

// 管理役の一覧（{数, 容量, 配列}）。番地は sub_6DE198 の即値（逆アセンブルと逆コンパイルで一致）。
const u32 kListPlayerHouse = 0x6758;        // 家（actor+0x5C0 が 0）
const u32 kListNpcHouse = 0x6774;
const u32 kListBridge = 0x67CC;             // 家（+0x5C0 が非 0）・92・104・橋・199。管理役が毎フレーム読む
const u32 kListOther = 0x68C0;              // それ以外（公共事業の大半）
// 消すときに探す一覧。上の 4 本に、特別 id 用の 4 本を足したもの。
const u32 kAllLists[] = { 0x6758, 0x6774, 0x67A8, 0x67CC, 0x68A0, 0x68B0, 0x68C0, 0x6A5C };

float s_spawnPosition[3];

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
volatile bool s_reloaded;                   // 直前の操作で部屋を読み直したか

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

bool IsBuilding(u16 id) {
    return id < kEmptyId;
}

// Vapecord の IsFaceCutOutBuilding と同じ 2 つ。この 2 つだけ看板表にも位置を持つ。
bool IsDesignStand(u16 id) {
    return id == 0xDC || id == 0xDD;
}

u8 *StandAt(u8 *data, u32 index) {
    return data + kStandsOffset + kStandBytes * index;
}

u32 &StandX(u8 *stand) { return *reinterpret_cast<u32 *>(stand + kStandX); }
u32 &StandY(u8 *stand) { return *reinterpret_cast<u32 *>(stand + kStandY); }

s32 FreeStand(u8 *data) {
    for (u32 i = 0; i < kStands; ++i) {
        u8 *stand = StandAt(data, i);
        if (StandX(stand) == kStandEmpty && StandY(stand) == kStandEmpty)
            return (s32)i;
    }
    return -1;
}

// Vapecord SetFaceCutOutData をそのまま: 空きに位置を入れ、残りの空きへそのパターンを写す。
void StandPlace(u8 *data, u32 x, u32 y) {
    const s32 free = FreeStand(data);
    if (free < 0)
        return;
    u8 *stand = StandAt(data, (u32)free);
    StandX(stand) = x & 0xFF;
    StandY(stand) = y & 0xFF;
    for (u32 j = 0; j < kStands; ++j) {
        u8 *other = StandAt(data, j);
        if (StandX(other) == kStandEmpty && StandY(other) == kStandEmpty)
            std::memcpy(other, stand, kStandPattern);
    }
}

// Vapecord EditFaceCutOutData: 古い位置の看板を新しい位置へ。
void StandMove(u8 *data, u32 oldX, u32 oldY, u32 newX, u32 newY) {
    for (u32 i = 0; i < kStands; ++i) {
        u8 *stand = StandAt(data, i);
        if (StandX(stand) == oldX && StandY(stand) == oldY) {
            StandX(stand) = newX & 0xFF;
            StandY(stand) = newY & 0xFF;
            return;
        }
    }
}

// Vapecord TryRemoveBuilding: その位置の看板を空きに戻す。
void StandRemove(u8 *data, u32 x, u32 y) {
    for (u32 i = 0; i < kStands; ++i) {
        u8 *stand = StandAt(data, i);
        if (StandX(stand) == x && StandY(stand) == y) {
            StandX(stand) = kStandEmpty;
            StandY(stand) = kStandEmpty;
        }
    }
}

const float *PlayerPosition(void) {
    const u32 player = GetPlayer(GetOnlineIndex(), 1);
    if (player == 0)
        return nullptr;
    return reinterpret_cast<const float *>(player + kPlayerPosition);
}

// 描画スレッド。実体をその場で作れなかったときの退避先。
void Reload(const float *coords) {
    if (coords == nullptr)
        return;
    ReloadRoom(RoomData(), RoomId(), coords, reinterpret_cast<const u32 *>(kU0Data),
               6, 0, 0, 1, 1);
}

u32 StrcMgr(void) {
    return *reinterpret_cast<volatile u32 *>(kStrcMgr);
}

u32 *ListCount(u32 mgr, u32 list) { return reinterpret_cast<u32 *>(mgr + list); }
u32 ListCapacity(u32 mgr, u32 list) { return *reinterpret_cast<u32 *>(mgr + list + 4); }
u32 *ListArray(u32 mgr, u32 list) { return *reinterpret_cast<u32 **>(mgr + list + 8); }

bool IsNpcHouse(u32 id) {
    const u8 *table = reinterpret_cast<const u8 *>(kNpcHouseTable);
    for (u32 i = 0; i < 10; ++i)
        if (table[i] == id)
            return true;
    return false;
}

// 読み込み段 2 が特別扱いする id（別プロファイル・追加の実体・管理役への登録がある）。
// これらは部屋の読み直しに任せる。
bool IsSpecial(u32 id) {
    return (id >= 80 && id <= 83) || id == 89 || id == 90 || id == 93;
}

bool CanSpawnInPlace(u32 id) {
    if (id >= kEmptyId || IsSpecial(id))
        return false;
    const u32 filter = VillageFilter(id);
    return filter == 0 || filter == 165;
}

// sub_6DE198 の 1 件分: 位置 → 高さ → 生成 → 一覧へ。作れたら true。
bool SpawnVisual(u32 id, u32 x, u32 y) {
    const u32 mgr = StrcMgr();
    if (mgr == 0 || !CanSpawnInPlace(id))
        return false;
    s_spawnPosition[0] = (float)(32 * x + 16);
    s_spawnPosition[1] = 0.0f;
    s_spawnPosition[2] = (float)(32 * y + 16);
    s_spawnPosition[1] = GroundHeight(s_spawnPosition, 0);
    if (IsBridge(id))
        s_spawnPosition[1] += kBridgeLift;
    const u32 actor = SpawnActor(BuildingProfile(id), mgr, id, s_spawnPosition, nullptr);
    if (actor == 0)
        return false;

    u32 list = kListOther;
    if (id <= 3)
        list = *reinterpret_cast<volatile u8 *>(actor + kActorHouseFlag) == 0 ? kListPlayerHouse
                                                                             : kListBridge;
    else if (IsNpcHouse(id))
        list = kListNpcHouse;
    else if (id == 92 || id == 104 || id == 199 || IsBridge(id))
        list = kListBridge;
    u32 *count = ListCount(mgr, list);
    if (*count < ListCapacity(mgr, list)) {
        ListArray(mgr, list)[*count] = actor;
        ++*count;
    }
    return true;
}

bool IsActorOf(u32 actor, u32 id, u32 x, u32 y) {
    if (actor == 0 || *reinterpret_cast<volatile u8 *>(actor + kActorDestroying) != 0)
        return false;
    if (*reinterpret_cast<volatile u8 *>(actor + kActorBuildingId) != id)
        return false;
    const float *p = reinterpret_cast<const float *>(actor + kActorPosition);
    return (s32)p[0] == (s32)(32 * x + 16) && (s32)p[2] == (s32)(32 * y + 16);
}

// 一覧から抜いてから削除を要求する。管理役は毎フレーム一覧を読み、AcStrc の
// デストラクタは自分を一覧から外さないので、順序を逆にすると解放済みを読まれる。
bool KillVisual(u32 id, u32 x, u32 y) {
    const u32 mgr = StrcMgr();
    if (mgr == 0 || IsSpecial(id))
        return false;
    for (u32 l = 0; l < sizeof(kAllLists) / sizeof(kAllLists[0]); ++l) {
        u32 *count = ListCount(mgr, kAllLists[l]);
        u32 *array = ListArray(mgr, kAllLists[l]);
        if (array == nullptr || *count > ListCapacity(mgr, kAllLists[l]))
            continue;
        for (u32 i = 0; i < *count; ++i) {
            const u32 actor = array[i];
            if (!IsActorOf(actor, id, x, y))
                continue;
            for (u32 j = i + 1; j < *count; ++j)
                array[j - 1] = array[j];
            --*count;
            array[*count] = 0;
            RequestDestroy(actor);
            return true;
        }
    }
    return false;
}

// 建ったものの中へ閉じ込めないよう、Vapecord と同じく 2 マス手前へ出す。
void StepOut(u32 x, u32 y) {
    const u32 player = GetPlayer(GetOnlineIndex(), 1);
    if (player == 0)
        return;
    float out[3];
    TileToWorld(out, x, y + 2);
    float *p = reinterpret_cast<float *>(player + kPlayerPosition);
    p[0] = out[0];
    p[1] = out[1];
    p[2] = out[2];
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

    // 見た目はその場で作り直す（IDA-opus-5.5-F004）。作れなかったときだけ部屋を読み直す。
    s_reloaded = false;
    switch (op) {
    case Op::Place: {
        // ゲームの設置関数。足元の属性・セーブの表・占有まで自分でやる。
        BuildingPlace(s_argX, s_argY, s_argId);
        if (IsDesignStand((u16)s_argId))
            StandPlace(data, s_argX, s_argY);
        if (SpawnVisual(s_argId, s_argX, s_argY)) {
            StepOut(s_argX, s_argY);
        } else {
            float out[3];
            TileToWorld(out, s_argX, s_argY + 2);
            Reload(out);
            s_reloaded = true;
        }
        return Result::Ok;
    }
    case Op::Remove: {
        Slot *slot = SlotAt(s_argSlot);
        if (slot == nullptr)
            return Result::NoSaveData;
        if (!IsBuilding(slot->id))
            return Result::EmptySlot;
        const Slot old = *slot;
        if (IsDesignStand(slot->id))
            StandRemove(data, slot->x, slot->y);
        slot->id = kEmptyId;
        slot->x = 0;
        slot->y = 0;
        if (data[kCountOffset] != 0)
            --data[kCountOffset];
        // ★ここが Vapecord に無い部分。表を書き換えただけでは古い場所の属性と占有が残る。
        RebuildAttributes();
        RebuildOccupancy();
        if (!KillVisual(old.id, old.x, old.y)) {
            Reload(here);
            s_reloaded = true;
        }
        return Result::Ok;
    }
    case Op::Move: {
        Slot *slot = SlotAt(s_argSlot);
        if (slot == nullptr)
            return Result::NoSaveData;
        if (!IsBuilding(slot->id))
            return Result::EmptySlot;
        const Slot old = *slot;
        if (IsDesignStand(slot->id))
            StandMove(data, slot->x, slot->y, s_argX, s_argY);
        slot->x = (u8)s_argX;
        slot->y = (u8)s_argY;
        RebuildAttributes();
        RebuildOccupancy();
        if (KillVisual(old.id, old.x, old.y) && SpawnVisual(old.id, s_argX, s_argY)) {
            StepOut(s_argX, s_argY);
        } else {
            float out[3];
            TileToWorld(out, s_argX, s_argY + 2);
            Reload(out);
            s_reloaded = true;
        }
        return Result::Ok;
    }
    case Op::Rebuild:
        // 当たり判定は作り直した直後から効く（T004）。見た目は変わらないので読み直さない。
        RebuildAttributes();
        RebuildOccupancy();
        return Result::Ok;
    default:
        return Result::Ok;
    }
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

bool LastReloaded(void) {
    return s_reloaded;
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
        if (slot == nullptr || !IsBuilding(slot->id))
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
    if (!IsBuilding(id))
        return Result::InvalidId;
    u8 *data = BuildingData();
    if (data == nullptr)
        return Result::NoSaveData;
    if (IsDesignStand(id) && FreeStand(data) < 0)
        return Result::NoFreeStand;
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
    case Result::InvalidId: return u8"その番号の建物はありません";
    case Result::NoFreeSlot: return u8"建物の空きがありません";
    case Result::NoSelection: return u8"建物を選んでいません";
    case Result::EmptySlot: return u8"選んだスロットに建物がありません";
    case Result::NoFreeStand: return u8"マイデザインの看板の空きがありません";
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
            // 名前のある建物は 175 件（tools/strc/catalog_buildings.py）。項目は 255 まで持てる。
            const u32   kMaxNames = 255;

            const char *g_names[kMaxNames];
            u8          g_ids[kMaxNames];
            u32         g_count;
            int         g_pickIndex = -1;
            s32         g_selected = -1;

            void    Report(const char *title, PublicWorks::Result result)
            {
                if (result == PublicWorks::Result::Ok)
                    GuiNotification::Notify(title, PublicWorks::LastReloaded()
                                                       ? u8"完了（部屋を読み直しました）"
                                                       : u8"完了（その場で反映）");
                else
                    GuiNotification::NotifyRed(title, PublicWorks::ResultName(result));
            }

            void    PlaceExecute(int index)
            {
                (void)index;
                const int pick = g_pickIndex >= 0 ? GuiMenu::ItemApplied(g_pickIndex) : -1;

                if (pick < 0 || (u32)pick >= g_count)
                    return;
                Report(kPwPlace, PublicWorks::Place(g_ids[pick]));
            }

            void    NearestExecute(int index)
            {
                (void)index;
                static char message[96];

                g_selected = PublicWorks::Nearest();
                PublicWorks::Slot slot;
                if (g_selected < 0 || !PublicWorks::ReadSlot((u32)g_selected, slot))
                {
                    GuiNotification::NotifyRed(kPwNearest, u8"近くに建物がありません");
                    return;
                }
                // 家には名前表の名前が無いので、id も並べて出す。
                const char *name = PublicWorks::NameOf(slot.id);
                std::snprintf(message, sizeof(message), u8"%ld番 0x%02X %s (%u,%u)",
                              (long)g_selected, (unsigned)slot.id, name[0] != '\0' ? name : u8"（名前なし）",
                              (unsigned)slot.x, (unsigned)slot.y);
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
            // 名前はゲーム自身の表（0x00955700）から引く。役場・店・家も含めて名前のある全部。
            g_count = 0;
            for (u32 id = 0; id < PublicWorks::kEmptyId && g_count < kMaxNames; ++id)
            {
                const char *name = PublicWorks::NameOf((u16)id);
                if (name[0] == '\0')
                    continue;
                g_names[g_count] = name;
                g_ids[g_count] = (u8)id;
                ++g_count;
            }
            g_pickIndex = GuiMenu::FindItem(kPwPick);
            if (g_pickIndex >= 0 && g_count > 0)
                GuiMenu::SetItemOptions(g_pickIndex, g_names, (int)g_count);

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

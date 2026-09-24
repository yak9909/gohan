#include "PublicWorks.hpp"

#include "BuildingEditor.hpp"
#include "BuildingPreview.hpp"
#include "BuildingHighlight.hpp"

#include "Cheats.hpp"
#include "GridCursor.hpp"
#include "GuiMenu.hpp"
#include "GuiNotification.hpp"
#include "MapIconPools.h"

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
typedef u32 (*HeapFreeFn)(void *heap);                              // 0x0074D744

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
// sead::ExpHeap::getFreeSize（グリッドカーソルと同じ）
const HeapFreeFn HeapFreeSize = reinterpret_cast<HeapFreeFn>(0x0074D744);

const u32 kActorPosition = 0x14;            // float x, y, z（Actor 基底 ctor 0x57AE88）
const u32 kActorDestroying = 0x0F;          // u8: 削除要求済み
const u32 kActorBuildingId = 0x66;          // u8（CRO の 0xB7E5E8）
const u32 kActorHouseFlag = 0x5C0;          // u8: プレイヤーの家だけ（CRO の 0xB7E19C）
// ★橋を建てるときの持ち上げ（BsStrcMgr_SpawnStructures 0x6DE6CC が FLDS で読む flt_6DE560 = 32.0）。数値は写さず、ゲームのものを読む
const u32 kBridgeLiftAddr = 0x006DE560;
// 川底の判定（IDA-opus-5.5-F026/F027）: マスの属性コード（sub_6C4F08(pos, 0)、0 = 無し）を FieldAttr_WaterKind 0x5CD544
// （byte_957936）で引いて 1 = 川。岸の角の斜めのマスは 0。
typedef u32 (*AttrAtFn)(const float *pos, u32 room);                // 0x006C4F08
typedef u32 (*WaterKindFn)(u32 code);                               // 0x005CD544
const AttrAtFn AttrAt = reinterpret_cast<AttrAtFn>(0x006C4F08);
const WaterKindFn WaterKind = reinterpret_cast<WaterKindFn>(0x005CD544);
const u32 kWaterRiver = 1;
const u32 kNpcHouseTable = 0x008887A0;      // u8[10]: 住民の家

// 管理役の一覧（{数, 容量, 配列}）。番地は sub_6DE198 の即値（逆アセンブルと逆コンパイルで一致）。
const u32 kListPlayerHouse = 0x6758;        // 家（actor+0x5C0 が 0）
const u32 kListNpcHouse = 0x6774;
const u32 kListBridge = 0x67CC;             // 家（+0x5C0 が非 0）・92・104・橋・199。管理役が毎フレーム読む
const u32 kListOther = 0x68C0;              // それ以外（公共事業の大半）
// 消すときに探す一覧。上の 4 本に、特別 id 用の 4 本を足したもの。
const u32 kAllLists[] = { 0x6758, 0x6774, 0x67A8, 0x67CC, 0x68A0, 0x68B0, 0x68C0, 0x6A5C };

float s_spawnPosition[3];

// ---- 置いたマスのアイテムと下画面の地図（IDA-opus-5.5-F005）---------------------------------
// アイテムの見た目は fgobj がマスごとに作る。ゲーム自身がアイテムを消したあとに呼ぶ
// Field_MarkDirty（Vapecord TRAMPLE3）でそのマスの物体を壊し「未構築」に戻すと、今の地面で作り直す。
typedef void (*MarkDirtyFn)(u32 x, u32 y, u32 destroy);           // 0x0059DA7C
const MarkDirtyFn FieldMarkDirty = reinterpret_cast<MarkDirtyFn>(0x0059DA7C);
const u32 kFieldTilesX = 0x70;              // fgobj のビットマップは 112 x 96 マス（0x5A1B28 の境界検査）
const u32 kFieldTilesY = 0x60;
const u32 kOccupancyLayer1 = 0x00AB1DE8;    // dword_AB1DE4[1]
const u32 kOccupancyWidth = 112;
const u32 kOccupancyHeight = 96;
const s32 kFootprintBefore = 7;             // 足元 16 x 16 の (7, 7) が建物の (x, y)
const s32 kFootprintAfter = 8;

// 下画面の村の地図（BsMenuMapVillage）。建物のアイコンは作成段 9 で 1 回だけ
// sub_221190(map, 0, 1) が置く。プールを隠して数を 0 に戻し、同じ関数を呼び直す。
typedef void (*MapPlaceFn)(u32 map, u32 book, u32 keep);           // 0x00221190
typedef void (*IconVisibleFn)(u32 icon, u32 visible);              // 0x006A81C4
typedef void (*IconUpdateFn)(u32 icon);                            // 0x0060D760
const MapPlaceFn MapPlaceBuildings = reinterpret_cast<MapPlaceFn>(0x00221190);
const IconVisibleFn IconSetVisible = reinterpret_cast<IconVisibleFn>(0x006A81C4);
const IconUpdateFn IconUpdate = reinterpret_cast<IconUpdateFn>(0x0060D760);

const u32 kBaseMgr = 0x0096F2A4;            // u32: 基本マネージャ（vc_GETDATA1 が返す）
const u32 kBaseSlots = 0x400;               // mgr + 20 * slot = プロセス（sub_51EF38）
const u32 kBaseSlotStride = 20;
const u32 kMapVillageVtable = 0x008EBA24;   // vtbl_BsMenuMapVillage
const u32 kMapInProcess = 0xA7C;            // proc + 2684 = 地図本体（作成段 9 の引数）
const u32 kMapCreateStep = 0x578;           // proc + 1400: 作成が済むと 0 に戻る
const u32 kMapIconStride = 0x314;           // MapIcon 1 個 = 788 B
const u32 kMapCount49E4 = 0xB3AC;              // map + 45996: プール +0x49E4 の使用数
const u32 kMapCount2B1C = 0xB3B0;              // map + 46000: プール +0x2B1C の使用数
struct IconPool { u32 offset; u32 count; };
// ゲームの割り当て関数 0x222434 は枠の数を見ない。枠を超えると隣の枠や配列の外を「アイコン」として
// 書く（星アイコン、線路の柵アイコンの消滅。IDA-opus-5.5-F006）。これはゲーム自身が部屋を読み直した
// ときの挙動でもあるので**直さない**（利用者指示 2026-09-23）。ゲームのコードにも手を入れない。
// A（+0x2B1C、10 個）を超えた分は直後の B（+0x49E4、8 個）を上書きする。これが星アイコンで、
// ゲーム自身の読み直しでも起きて落ちない。さらに先の +0x6284 は初期化されていないアイコンで、
// そこへ届くと 0x4B4338 で落ちる（1 本目のダンプ。IDA-opus-5.5-F009）。
// A の添字 18 = 0x2B1C + 788 * 18 = 0x6284、B の添字 8 = 0x49E4 + 788 * 8 = 0x6284。
const u32 kMapPoolA = 10;                   // 'A' = プール +0x2B1C
const u32 kMapPoolB = 8;                    // 'B' = プール +0x49E4
const u32 kMapCrashA = 18;                  // A がこの数を超えると +0x6284 に届く
// 工事中の公共事業は B を 1 つ使う（sub_221190 の別枝）。工事中かは数えないので 1 つ空けておく。
const u32 kMapPendingReserve = 1;

// ---- 資源の枠（IDA-opus-5.5-F007）---------------------------------------------------------
// 建物の実体はモデルとテクスチャを BsStrcMgr の ResourceMgr（管理役 +0xF24、村では 1,423,360 B の
// ヒープと 0x40 個の枠）から借りる。枠は部屋を片付けるまで返らず、空きの一覧が尽きても CRO の
// 0xB49834 は null を確かめずに先頭を取る → 読み込みスレッドの 0x56A0EC で落ちた（2 本目のダンプ）。
// ゲームの読み直しは毎回作り直すので起きない。なので、その場で作るのは余裕があるときだけにする。
const u32 kResourceMgr = 0xF24;             // mgr + 3876
const u32 kResourceFreeHead = 0x563C;       // mgr + 22076 = 空き一覧の先頭（ノード +8 が次）
const u32 kResourceNodeNext = 8;
const u32 kSpawnFreeEntries = 3;            // fobj の 1 種類のファイル数の最大（romfs）
const u32 kSpawnFreeHeap = 0x30000;         // 77,712 B（fobj の最大）の 2 倍を切り上げ

// この共有の 64 枠を使うのは、実体の vtable slot 55 が 0xB49620 を呼ぶ 7 クラスだけ（IDA-opus-5.5-F008）。
// 1 種類 = 2 枠（モデル＋季節テクスチャ 1 本。実機で 4 種 = 8 枠、ベンチ 1 種で +2 を確認）。
const u8 kSharedPoolProfiles[] = {
    0x80,                                   // AcStrcFieldObj
    0x8D,                                   // AcStrcBridge
    0x8E,                                   // AcStrcLightHouse
    0x8F,                                   // AcStrcTrashBox
    0x90,                                   // AcStrcMyDesignSign
    0x92,                                   // AcStrcGeyser
    0x93,                                   // AcStrcScreen
};
const u32 kResourceEntries = 0x40;          // BsStrcMgr_InitHeaps が作る枠の数
const u32 kEntriesPerKind = 2;
// 部屋の読み直しでゲームが読むのは「表にある種類 × 2」。64 枠 = 32 種が上限で、2 種ぶん余裕を残す。
const u32 kMaxSharedKinds = kResourceEntries / kEntriesPerKind - 2;

// 役場・店などは実体ごとに専用の ExpHeap（実測 69,632〜82,420 B）を親ヒープから借りる
// （実体 +0xFC の専用置き場、読み込みは 0xB47474）。親は BsStrcMgr の親 *(0x94CC68)（村で 5 MB）。
const u32 kStrcParentHeap = 0x0094CC68;     // u32: sead::ExpHeap*
const u32 kSpawnParentFree = 0x40000;       // 実測の最大 82,420 B の 3 倍強
const IconPool kMapBuildingPools[] = {
    { 0x0004, 14 },                         // 家（0〜3）と住民の家（8〜17）の固定枠
    { 0x2B1C, 10 },                         // 役場・店などの割り当て枠（カウンタ +46000）
    { 0x49E4, 8 },                          // 橋・坂などの割り当て枠（カウンタ +45996）
};

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
volatile ReloadWhy s_reloadWhy = ReloadWhy::None;   // 読み直した理由（通知に出す）
volatile MapState s_mapState = MapState::Untouched;  // 直前の操作で下画面の地図をどうしたか

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

bool IsHeap(u32 h) {
    return h >= 0x30000000u && h < 0x40000000u && (h & 3u) == 0u;
}

// ResourceMgr に 1 種類分（最大 3 ファイル・78 KB）を読む余裕があるか。
bool ResourceRoom(u32 mgr) {
    u32 free = 0;
    for (u32 node = *reinterpret_cast<volatile u32 *>(mgr + kResourceFreeHead);
         node != 0 && free < kSpawnFreeEntries;
         node = *reinterpret_cast<volatile u32 *>(node + kResourceNodeNext)) {
        if (node < 0x08000000u)
            return false;                   // 一覧が壊れている疑い
        ++free;
    }
    if (free < kSpawnFreeEntries)
        return false;
    void *heap = *reinterpret_cast<void **>(mgr + kResourceMgr + 4);
    if (!IsHeap(reinterpret_cast<u32>(heap)))
        return false;
    return HeapFreeSize(heap) >= kSpawnFreeHeap;
}

bool UsesSharedPool(u32 profile) {
    for (u32 i = 0; i < sizeof(kSharedPoolProfiles); ++i)
        if (kSharedPoolProfiles[i] == profile)
            return true;
    return false;
}

// 役場・店などの専用ヒープを借りる親に余裕があるか。
bool ParentRoom(void) {
    const u32 heap = *reinterpret_cast<volatile u32 *>(kStrcParentHeap);
    return IsHeap(heap) && HeapFreeSize(reinterpret_cast<void *>(heap)) >= kSpawnParentFree;
}

// 建物表にある、共有の枠を使う建物の種類の数（extra があればそれも 1 種として足す）。
u32 SharedKinds(u32 extra) {
    bool seen[0x100] = {};
    u32 kinds = 0;
    for (u32 i = 0; i <= kSlots; ++i) {
        u32 id = extra;
        if (i < kSlots) {
            const Slot *slot = SlotAt(i);
            if (slot == nullptr)
                continue;
            id = slot->id;
        }
        if (id >= kEmptyId || seen[id] || !UsesSharedPool(BuildingProfile(id)))
            continue;
        seen[id] = true;
        ++kinds;
    }
    return kinds;
}

// sub_6DE198 の 1 件分: 位置 → 高さ → 生成 → 一覧へ。作れたら true。
bool SpawnVisual(u32 id, u32 x, u32 y) {
    const u32 mgr = StrcMgr();
    if (mgr == 0) {
        s_reloadWhy = ReloadWhy::NoManager;
        return false;
    }
    if (!CanSpawnInPlace(id)) {
        s_reloadWhy = ReloadWhy::Special;
        return false;
    }
    // 読む先で余裕の見方が違う（IDA-opus-5.5-F008）。足りなければゲームの読み直しに任せる。
    const u32 profile = BuildingProfile(id);
    if (UsesSharedPool(profile) ? !ResourceRoom(mgr) : !ParentRoom()) {
        s_reloadWhy = UsesSharedPool(profile) ? ReloadWhy::SharedPoolFull : ReloadWhy::ParentHeapLow;
        return false;
    }
    s_spawnPosition[0] = (float)(32 * x + 16);
    s_spawnPosition[1] = SpawnHeight(id, x, y);
    s_spawnPosition[2] = (float)(32 * y + 16);
    const u32 actor = SpawnActor(profile, mgr, id, s_spawnPosition, nullptr);
    if (actor == 0) {
        s_reloadWhy = ReloadWhy::SpawnFailed;
        return false;
    }

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
    if (mgr == 0 || IsSpecial(id)) {
        s_reloadWhy = mgr == 0 ? ReloadWhy::NoManager : ReloadWhy::Special;
        return false;
    }
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
    s_reloadWhy = ReloadWhy::ActorNotFound;
    return false;
}

// 足元 16 x 16 のマスのアイテムを、今の地面の高さで作り直させる。
void RefreshItems(u32 x, u32 y) {
    for (s32 dy = -kFootprintBefore; dy <= kFootprintAfter; ++dy) {
        for (s32 dx = -kFootprintBefore; dx <= kFootprintAfter; ++dx) {
            const s32 tx = (s32)x + dx;
            const s32 ty = (s32)y + dy;
            // sub_753450 は境界を見ないので、ここで絞る
            if (tx < 0 || ty < 0 || tx >= (s32)kFieldTilesX || ty >= (s32)kFieldTilesY)
                continue;
            FieldMarkDirty((u32)tx, (u32)ty, 1);
        }
    }
}

u32 FindMapVillage(void) {
    const u32 mgr = *reinterpret_cast<volatile u32 *>(kBaseMgr);
    if (mgr == 0)
        return 0;
    for (u32 slot = 0; slot < kBaseSlots; ++slot) {
        const u32 proc = *reinterpret_cast<volatile u32 *>(mgr + kBaseSlotStride * slot);
        if (proc < 0x08000000u || *reinterpret_cast<volatile u32 *>(proc) != kMapVillageVtable)
            continue;
        if (*reinterpret_cast<volatile u8 *>(proc + kActorDestroying) != 0)
            continue;
        if (*reinterpret_cast<volatile u32 *>(proc + kMapCreateStep) != 0)
            continue;
        return proc + kMapInProcess;
    }
    return 0;
}

// 建物表から、地図が使う枠の数を数える（sub_221190 と同じく x >= 16 のものだけ）。
void CountMapPools(u32 &a, u32 &b) {
    a = 0;
    b = 0;
    for (u32 i = 0; i < kSlots; ++i) {
        const Slot *slot = SlotAt(i);
        if (slot == nullptr || slot->id >= kEmptyId || slot->x < 16)
            continue;
        if (kMapIconPool[slot->id] == 'A')
            ++a;
        else if (kMapIconPool[slot->id] == 'B')
            ++b;
    }
}

// 地図の建物アイコンを、画面が切り替わったときと同じ関数（作成段 9）で置き直す。
// 作りたてと同じ状態（枠のアイコンを隠し、数を 0）にしてから呼ぶ。A が 10 を超えた分は
// ゲームと同じく B を上書きして星アイコンになる。ゲームでも落ちる数のときだけ何もしない
// （その数にならないよう Place が断るので、通常は起きない）。
bool MapWouldCrash(u32 a, u32 b) {
    return a > kMapCrashA || b + kMapPendingReserve > kMapPoolB;
}

MapState RefreshMap(void) {
    u32 a = 0, b = 0;
    CountMapPools(a, b);
    if (MapWouldCrash(a, b))
        return MapState::Full;
    const u32 map = FindMapVillage();
    if (map == 0)
        return MapState::NotFound;
    // 数が壊れていたら触らない（地図でない物を掴んだ疑い）。A はゲームでも 10 を超えうる。
    if (*reinterpret_cast<u32 *>(map + kMapCount49E4) > kMapPoolB || *reinterpret_cast<u32 *>(map + kMapCount2B1C) > kMapCrashA)
        return MapState::NotFound;
    for (u32 p = 0; p < sizeof(kMapBuildingPools) / sizeof(kMapBuildingPools[0]); ++p) {
        for (u32 i = 0; i < kMapBuildingPools[p].count; ++i) {
            const u32 icon = map + kMapBuildingPools[p].offset + kMapIconStride * i;
            IconSetVisible(icon, 0);
            IconUpdate(icon);
        }
    }
    *reinterpret_cast<u32 *>(map + kMapCount49E4) = 0;
    *reinterpret_cast<u32 *>(map + kMapCount2B1C) = 0;
    MapPlaceBuildings(map, 0, 1);
    return MapState::Refreshed;
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
    // プレイヤーの位置は変えない（利用者指示 2026-09-23）。読み直すときも今の位置のまま。
    float stay[3] = { here[0], here[1], here[2] };
    s_reloaded = false;
    s_reloadWhy = ReloadWhy::None;
    s_mapState = MapState::Untouched;
    switch (op) {
    case Op::Place: {
        // ゲームが部屋を読み込むときに枠へ入りきらない種類数にはしない（入りきらないと
        // ゲーム自身の読み込みが 0x56A0EC で落ちる。3 本目のダンプ。IDA-opus-5.5-F008）。
        const u32 profile = BuildingProfile(s_argId);
        if (UsesSharedPool(profile)) {
            if (SharedKinds(s_argId) > kMaxSharedKinds)
                return Result::TooManyKinds;
        } else if (!ParentRoom()) {
            // 役場・店などは 1 棟ごとに親ヒープから専用ヒープを借りる。読み直しても借りる量は
            // 同じなので、読み直しへ逃がすとゲーム自身の読み込みが落ちる（4 本目。IDA-opus-5.5-F009）。
            return Result::NoHeapRoom;
        }
        {
            // 置いた後の地図がゲームでも落ちる数になるなら断る。
            u32 a = 0, b = 0;
            CountMapPools(a, b);
            if (kMapIconPool[s_argId] == 'A')
                ++a;
            else if (kMapIconPool[s_argId] == 'B')
                ++b;
            if (MapWouldCrash(a, b))
                return Result::MapLimit;
        }
        // ゲームの設置関数。足元の属性・セーブの表・占有まで自分でやる。
        BuildingPlace(s_argX, s_argY, s_argId);
        if (IsDesignStand((u16)s_argId))
            StandPlace(data, s_argX, s_argY);
        if (!SpawnVisual(s_argId, s_argX, s_argY)) {
            Reload(stay);
            s_reloaded = true;
            return Result::Ok;
        }
        RefreshItems(s_argX, s_argY);
        s_mapState = RefreshMap();
        return Result::Ok;
    }
    case Op::Remove: {
        Slot *slot = SlotAt(s_argSlot);
        if (slot == nullptr)
            return Result::NoSaveData;
        if (!IsBuilding(slot->id))
            return Result::EmptySlot;
        // 光らせている材質は実体と一緒に消えるので、消す前に元へ戻す。
        BuildingHighlight::ClearNow();
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
            Reload(stay);
            s_reloaded = true;
            return Result::Ok;
        }
        RefreshItems(old.x, old.y);
        s_mapState = RefreshMap();
        return Result::Ok;
    }
    case Op::Move: {
        Slot *slot = SlotAt(s_argSlot);
        if (slot == nullptr)
            return Result::NoSaveData;
        if (!IsBuilding(slot->id))
            return Result::EmptySlot;
        const Slot old = *slot;
        const bool lit = BuildingHighlight::GetState() == BuildingHighlight::State::Active;
        BuildingHighlight::ClearNow();
        if (IsDesignStand(slot->id))
            StandMove(data, slot->x, slot->y, s_argX, s_argY);
        slot->x = (u8)s_argX;
        slot->y = (u8)s_argY;
        RebuildAttributes();
        RebuildOccupancy();
        if (!KillVisual(old.id, old.x, old.y) || !SpawnVisual(old.id, s_argX, s_argY)) {
            Reload(stay);
            s_reloaded = true;
            return Result::Ok;
        }
        RefreshItems(old.x, old.y);
        RefreshItems(s_argX, s_argY);
        s_mapState = RefreshMap();
        if (lit)                                    // 新しい実体を次のフレームで光らせ直す
            BuildingHighlight::Select(old.id, (u8)s_argX, (u8)s_argY);
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
    // ★前の操作の「読み直した」を残さない。以前はここで消さず、要求がタイムアウトすると前の操作
    //   （特殊建物で読み直した）の記録を建物エディターが読み、関係ない操作のたびに再開を繰り返しえた。
    s_reloaded = false;
    s_reloadWhy = ReloadWhy::None;
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
    if (op != Op::None) {
        s_result = Execute(op);
        s_doneSeq = s_seq;
        s_op = Op::None;
    }
    BuildingHighlight::FrameStep();
    BuildingEditor::FrameStep();
    BuildingPreview::FrameStep();
}

float SpawnHeight(u16 id, u32 x, u32 y) {
    float pos[3] = { (float)(32 * x + 16), 0.0f, (float)(32 * y + 16) };
    float h = GroundHeight(pos, 0);
    if (IsBridge(id))
        h += *reinterpret_cast<const volatile float *>(kBridgeLiftAddr);
    return h;
}

namespace {

bool IsRiverBed(s32 x, s32 y) {
    if (x < 0 || y < 0 || x >= (s32)kFieldTilesX || y >= (s32)kFieldTilesY)
        return false;
    const float pos[3] = { (float)(32 * x + 16), 0.0f, (float)(32 * y + 16) };
    return WaterKind(AttrAt(pos, 0)) == kWaterRiver;
}

u32 s_bridgeKey = 0xFFFFFFFFu;
float s_bridgeHeight;

}  // namespace

float BridgeHeight(u32 x, u32 y) {
    const u32 key = x | (y << 8);
    if (key == s_bridgeKey)
        return s_bridgeHeight;
    // 近い順（チェビシェフ距離の輪、輪の中はマンハッタン距離の小さいもの）に川底を探す
    s32 bx = -1, by = -1;
    for (s32 r = 0; r < (s32)kFieldTilesX && bx < 0; ++r) {
        s32 best = 0x7FFFFFFF;
        for (s32 dy = -r; dy <= r; ++dy) {
            for (s32 dx = -r; dx <= r; ++dx) {
                if (dx != -r && dx != r && dy != -r && dy != r)
                    continue;                                   // 輪の上だけ
                const s32 d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                if (d < best && IsRiverBed((s32)x + dx, (s32)y + dy)) {
                    best = d;
                    bx = (s32)x + dx;
                    by = (s32)y + dy;
                }
            }
        }
    }
    float h;
    if (bx >= 0) {
        float pos[3] = { (float)(32 * bx + 16), 0.0f, (float)(32 * by + 16) };
        h = GroundHeight(pos, 0) + *reinterpret_cast<const volatile float *>(kBridgeLiftAddr);
    } else {                                                    // 村に川が無い: 基点の地面
        float pos[3] = { (float)(32 * x + 16), 0.0f, (float)(32 * y + 16) };
        h = GroundHeight(pos, 0);
    }
    s_bridgeKey = key;
    s_bridgeHeight = h;
    return h;
}

float CursorHeight(u16 id, u32 x, u32 y) {
    if (IsBridgeId(id))
        return BridgeHeight(x, y);
    float pos[3] = { (float)(32 * x + 16), 0.0f, (float)(32 * y + 16) };
    return GroundHeight(pos, 0);
}

bool IsBridgeId(u16 id) {
    return id < kEmptyId && IsBridge(id);
}

bool StartFrameHook(void) {
    return EnsureHook();
}

bool Highlight(u32 index) {
    const Slot *slot = SlotAt(index);
    if (slot == nullptr || !IsBuilding(slot->id) || !EnsureHook())
        return false;
    BuildingHighlight::Select(slot->id, slot->x, slot->y);
    return true;
}

void Unhighlight(void) {
    BuildingHighlight::Clear();
}

bool LastReloaded(void) {
    return s_reloaded;
}

ReloadWhy LastReloadWhy(void) {
    return s_reloadWhy;
}

const char *ReloadWhyName(ReloadWhy why) {
    switch (why) {
    case ReloadWhy::None: return u8"-";
    case ReloadWhy::NoManager: return u8"建物の管理役が取れない";
    case ReloadWhy::Special: return u8"その場で作れない特殊な建物";
    case ReloadWhy::SharedPoolFull: return u8"共有の資源枠に空きがない";
    case ReloadWhy::ParentHeapLow: return u8"建物用の親ヒープの空きが少ない";
    case ReloadWhy::SpawnFailed: return u8"生成に失敗";
    case ReloadWhy::ActorNotFound: return u8"消す実体が見つからない";
    }
    return u8"?";
}

MapState LastMapState(void) {
    return s_mapState;
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
    return NearestTo(px, py);
}

s32 NearestTo(u32 px, u32 py) {
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
    u32 x = 0, y = 0;
    if (!PlayerTile(x, y))
        return Result::NoPlayer;
    return PlaceAt(id, x, y);
}

Result PlaceAt(u8 id, u32 x, u32 y) {
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

Result MoveTo(u32 slot, u32 x, u32 y) {
    if (slot >= kSlots)
        return Result::NoSelection;
    s_argSlot = slot;
    s_argX = x;
    s_argY = y;
    return Request(Op::Move);
}

s32 SlotAtTile(u32 x, u32 y) {
    // 占有レイヤ 1（村の建物）。112 x 96 バイト、値はスロット番号、空きは 0xFF
    // （Building_WriteOccupancy 0x526A0C → sub_2E5EB8、消去 sub_2E5ED4）。
    if (x >= kOccupancyWidth || y >= kOccupancyHeight)
        return -1;
    const u8 *layer = *reinterpret_cast<u8 *const *>(kOccupancyLayer1);
    if (layer == nullptr)
        return -1;
    const u8 v = layer[kOccupancyWidth * y + x];
    if (v == 0xFF || v >= kSlots)
        return -1;
    const Slot *slot = SlotAt(v);
    return (slot != nullptr && IsBuilding(slot->id)) ? (s32)v : -1;
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
    case Result::TooManyKinds: return u8"公共事業の種類が多すぎます（ゲームが読み込める上限）";
    case Result::NoHeapRoom: return u8"建物を読み込むメモリの空きがありません（ゲームの上限）";
    case Result::MapLimit: return u8"地図のアイコンがゲームで作れる上限を超えます";
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
                                                       : PublicWorks::LastMapState() == PublicWorks::MapState::Refreshed
                                                             ? u8"完了（その場で反映・地図も更新）"
                                                             : PublicWorks::LastMapState() == PublicWorks::MapState::Full
                                                                   ? u8"完了（その場で反映・地図は更新せず）"
                                                                   : u8"完了（その場で反映・地図は開いたときに更新）");
                else
                    GuiNotification::NotifyRed(title, PublicWorks::ResultName(result));
            }

            int         g_hlOn = -1, g_hlTint = -1, g_hlAlpha = -1, g_hlWave = -1, g_hlSpeed = -1;

            int     Value(int index, int fallback)
            {
                return index >= 0 ? GuiMenu::ItemApplied(index) : fallback;
            }

            void    HighlightParamsApplied(int index, s32 value)
            {
                (void)index;
                (void)value;
                BuildingHighlight::Params p = BuildingHighlight::GetParams();
                p.tint = (u8)Value(g_hlTint, p.tint);
                p.alpha = (u8)Value(g_hlAlpha, p.alpha);
                p.wave = (u8)Value(g_hlWave, p.wave);
                p.speed = (u8)Value(g_hlSpeed, p.speed);
                BuildingHighlight::SetParams(p);
            }

            // チェックボックスの効果（gohan-menu.md §4.3）。ON のあいだ「選ぶ」たびに光らせる。
            bool        g_hlActive;

            bool    HighlightWanted(void)
            {
                return g_hlActive;
            }

            bool    HighlightIsActive(int index)
            {
                (void)index;
                return g_hlActive;
            }

            void    HighlightSetActive(int index, bool active)
            {
                (void)index;
                g_hlActive = active;
                if (!active)
                    PublicWorks::Unhighlight();
                else if (g_selected >= 0)
                    PublicWorks::Highlight((u32)g_selected);
            }

            const GuiMenu::ToggleEffectFuncs kHighlightFuncs = { HighlightIsActive, HighlightSetActive };

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
                if (HighlightWanted())
                    PublicWorks::Highlight((u32)g_selected);
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

        void    SetSelectedPublicWork(s32 slot)
        {
            g_selected = slot;
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

            g_hlOn = GuiMenu::FindItem(kHlOn);
            g_hlTint = GuiMenu::FindItem(kHlTint);
            g_hlAlpha = GuiMenu::FindItem(kHlAlpha);
            g_hlWave = GuiMenu::FindItem(kHlWave);
            g_hlSpeed = GuiMenu::FindItem(kHlSpeed);
            if (g_hlOn >= 0)
                GuiMenu::RegisterToggleEffect(g_hlOn, &kHighlightFuncs);
            const int params[] = { g_hlTint, g_hlAlpha, g_hlWave, g_hlSpeed };
            for (u32 k = 0; k < 4; ++k)
                if (params[k] >= 0)
                    GuiMenu::RegisterApply(params[k], HighlightParamsApplied);
            HighlightParamsApplied(-1, 0);
        }
    }
}

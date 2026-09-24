#pragma once

#include <3ds.h>

// 公共事業を置く・消す・動かす。そして**消した／動かしたあとの当たり判定をその場で直す**。
//
// Vapecord の公共事業チートはセーブの建物表を書き換えて部屋を読み直す。置く方はゲームの
// 設置関数 0x002424F4 がその場で足元の属性を押すので判定が付くが、消す・動かす方は
// **属性を戻す処理が走らない**ので、再起動するまで古い場所に当たり判定が残る（利用者確認）。
//
// ゲームには「区画の地形属性を素に戻して、建物表から全部押し直す」関数がある
// （0x00242878。ModuleIndoor.cro / ModulePrologue.cro が自分で呼んでいる）。
// 占有マップを消して全部書き直す関数もある（0x0024220C。本体の 15 か所が使う常用処理）。
// 表を書き換えたあとにこの 2 つを呼べば、再起動と同じ状態になる（IDA-opus-5.5-F002、実機確認済み）。
//
// 見た目（村の建物の実体）は部屋を読み直さずに作る・消す（IDA-opus-5.5-F004）。
// 読み込み段 2 が 1 件ずつやっている「位置 → 高さ → 生成 → 管理役の一覧へ」を写し、
// 消すときは一覧から抜いてから削除を要求する。特別扱いの id などは部屋の読み直しへ退避する。
//
// ゲームの関数はすべて描画スレッド（グリッドカーソルのフック）で呼ぶ。
// メニュー側は要求を置いて、終わるのを待つだけ。
namespace PublicWorks
{
    static const u8  kFirstId = 0x90;       // fobj_*（公共事業）の範囲。検査の照合用
    static const u8  kLastId = 0xFB;
    static const u8  kEmptyId = 0xFC;       // これ未満は全部「建物」。役場・店・家も扱う
    static const u32 kSlots = 56;
    static const u32 kStands = 8;           // マイデザインの看板・顔出し看板

    enum class Result : u32
    {
        Ok,
        NotInVillage,       // 村の屋外でしか触らない
        NoSaveData,
        NoPlayer,
        InvalidId,          // 0xFC 以上（空き）
        NoFreeSlot,
        NoSelection,
        EmptySlot,          // 選んだスロットが空いている
        NoFreeStand,        // マイデザインの看板表に空きが無い
        TooManyKinds,       // 共有の資源枠に入りきらない種類数になる（IDA-opus-5.5-F008）
        NoHeapRoom,         // 役場・店などの専用ヒープを借りる親に余裕が無い（IDA-opus-5.5-F009）
        MapLimit,           // 地図のアイコンがゲームでも落ちる数になる（IDA-opus-5.5-F009）
        HookFailed,
        Busy,
        TimedOut,
    };

    struct Slot
    {
        u16 id;
        u8  x;
        u8  y;
    };

    // ---- 設置の余裕（ゲームのスレッドが約 0.5 秒ごとに数え直した写し）----
    //   used / slots: 建物表の使用数 / 枠数（56。埋まると設置は NoFreeSlot）。公共事業以外（役場・店など）も含む。
    //   kindsLeft: 共有の資源枠を使う建物（普通の公共事業・橋など 7 クラス）の、新しい種類をあと何種類置けるか
    //     （上限 kMaxSharedKinds = 30。同じ種類を増やしてもメモリは増えない。IDA-opus-5.5-F008）。
    struct Capacity { u16 used; u16 slots; u16 kindsLeft; bool valid; };
    Capacity        GetCapacity(void);

    // ---- 読むだけ（メニュースレッド）----
    bool            ReadSlot(u32 index, Slot &out);
    const char *    NameOf(u16 id);                 // ゲームの表の名前。無ければ ""
    bool            PlayerTile(u32 &x, u32 &y);
    // プレイヤーに一番近い建物のスロット（役場・店・家も含む）。無ければ -1。
    s32             Nearest(void);
    s32             NearestTo(u32 x, u32 y);        // マス (x, y) に一番近い建物（建物エディター）

    // ---- 変える（要求を出して描画スレッドの完了を待つ）----
    Result          Place(u8 id);                   // プレイヤーの足元へ
    Result          PlaceAt(u8 id, u32 x, u32 y);   // 村のマス (x, y) へ（建物エディター）
    Result          MoveTo(u32 slot, u32 x, u32 y);
    // マス (x, y) を占有している建物のスロット（ゲームの占有マップから）。無ければ -1。
    s32             SlotAtTile(u32 x, u32 y);
    Result          Remove(u32 slot);
    Result          MoveToPlayer(u32 slot);
    // 表は変えずに、当たり判定と占有だけ作り直す（見た目は変わらないので読み直さない）。
    Result          Rebuild(void);

    const char *    ResultName(Result result);
    // 直前の操作で見た目をその場で作れず、部屋を読み直したか（IDA-opus-5.5-F004）。
    bool            LastReloaded(void);
    // 読み直した理由（その場で作れなかった／消せなかったわけ）。
    enum class ReloadWhy : u32 { None, NoManager, Special, SharedPoolFull, ParentHeapLow, SpawnFailed, ActorNotFound };
    ReloadWhy       LastReloadWhy(void);
    const char *    ReloadWhyName(ReloadWhy why);
    // 直前の操作で下画面の地図の建物アイコンをどうしたか（IDA-opus-5.5-F005 / F006）。
    enum class MapState : u32 { Untouched, Refreshed, NotFound, Full };
    MapState        LastMapState(void);

    // スロットの建物を光らせる／やめる（BuildingHighlight。次のフレームで反映）。
    bool            Highlight(u32 slot);
    void            Unhighlight(void);

    bool            IsBridgeId(u16 id);             // ゲームの Building_IsBridge 0x6CBDB4
    // 建物を (x, y) に建てたときの実体の高さ（地面 0x6C69C0、橋は +32）。描画スレッドから呼ぶ。
    float           SpawnHeight(u16 id, u32 x, u32 y);
    // 橋の高さ（ゲームのスレッドから）。村の中で一定: ゲームが橋を建てる計算（川底 + flt_6DE560）を、村の川底で
    // 一番多い高さ（橋を架ける川の段）で行った値。村ごとに 1 回数えて覚える。川が無ければ (x, y) の地面。
    float           BridgeHeight(u32 x, u32 y);
    // UnitCursor の高さ: 橋なら BridgeHeight、ほかは基点の地面
    float           CursorHeight(u16 id, u32 x, u32 y);
    // 描画スレッドの毎フレームの口（グリッドカーソルのフック）を入れる。
    bool            StartFrameHook(void);

    // グリッドカーソルのフックから毎フレーム。
    void            FrameStep(void);
}

namespace CTRPluginFramework
{
    namespace Cheats
    {
        void    WirePublicWorks(void);
        // メニューの「選んだ公共事業」を外から決める（建物エディター）。-1 で外す。
        void    SetSelectedPublicWork(s32 slot);
    }
}

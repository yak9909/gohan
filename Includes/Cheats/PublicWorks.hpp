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
// 表を書き換えたあとにこの 2 つを呼べば、再起動と同じ状態になる（IDA-opus-5.5-F002）。
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

    // ---- 読むだけ（メニュースレッド）----
    bool            ReadSlot(u32 index, Slot &out);
    const char *    NameOf(u16 id);                 // ゲームの表の名前。無ければ ""
    bool            PlayerTile(u32 &x, u32 &y);
    // プレイヤーに一番近い建物のスロット（役場・店・家も含む）。無ければ -1。
    s32             Nearest(void);

    // ---- 変える（要求を出して描画スレッドの完了を待つ）----
    Result          Place(u8 id);                   // プレイヤーの足元へ
    Result          Remove(u32 slot);
    Result          MoveToPlayer(u32 slot);
    // 表は変えずに、当たり判定と占有だけ作り直して部屋を読み直す。
    Result          Rebuild(void);

    const char *    ResultName(Result result);

    // グリッドカーソルのフックから毎フレーム。
    void            FrameStep(void);
}

namespace CTRPluginFramework
{
    namespace Cheats
    {
        void    WirePublicWorks(void);
    }
}

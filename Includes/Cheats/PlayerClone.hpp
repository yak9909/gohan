#pragma once

#include <3ds.h>

// 自分のプレイヤーの複製（試験）。ゲーム自身の PlayerModel を、人形 AcNpcDemoDollPlayer と同じ呼び方で
// もう 1 体作り、プレイヤーの右隣へ置く。服・帽子・アクセは本物のプロフィールから作るのでそのまま。
// 髪型・髪色は複製の PlayerModel +548 / +552 だけを書く（本物のプロフィールには書かない）。
//
// ゲームの関数（IDA-opus-5.5-F028 / F029。docs/topics/player_clone_preview.md）:
//   PlayerModel_ctor 0x1D3854 / PlayerModel_CreateFromProfile 0x1CF090（1 が返るまで毎フレーム）
//   毎フレーム: PlayerModel_SetMatrix 0x1CEC10 → vtbl[2] 0x1D2BE8 → PlayerModel_CalcAnim 0x1CE8D8(pm,1,1)
//              → vtbl[4] 0x1D33FC → vtbl[5] 0x1D2CCC（部品の更新と部品の表への登録）→ Scene_SubmitNode(体, 0)
//   片付け: PlayerModel_DestroyStep 0x1D30D0（1 が返るまで）→ PlayerModel_dtor 0x1D3980
// 部品はプレイヤー管理役 BsPlayerMgr（*(0x94A374)）のバンクの空き枠を使う。場面が変わると管理役ごと作り直されるので、
// 管理役かプレイヤーが変わったらゲームの関数は呼ばずに手放す。
namespace PlayerClone
{
    // ---- メニュースレッド ----
    bool            Show(void);                 // フックを入れて作成を頼む。フックが入らなければ false
    void            Hide(void);                 // 片付けを頼む
    bool            IsShown(void);
    // 髪型（0..33、-1 = 本物のまま）と髪色（0..15、-1 = 本物のまま）。作ったあとも効く
    void            SetHair(s32 style, s32 color);
    // 画面に固定（late pass で専用カメラ）。yaw / pitch は度、x/y は上画面のピクセル（カメラの中心）、zoom は百分率
    void            SetScreen(bool on, s32 yaw, s32 pitch, s32 x, s32 y, s32 zoom);
    // プロセス終了時に 1 回。late pass のフックを戻して置き場を消す
    void            Shutdown(void);
    struct Status
    {
        u32     stage;          // 0 = 無し、1 = 作成中、2 = 表示中、3 = 片付け中、4 = 失敗
        u32     failReason;     // 1 = プレイヤー無し、2 = プロフィール無し、3 = 作成が終わらない、4 = 場面が変わった、5 = 空き枠が無い
        u32     frames;         // 今の段に入ってからのフレーム数
        u32     submits;
        u8      hair;           // 複製の pm+548
        u32     hairColor;      // 複製の pm+552
        u32     lateDraws;      // late pass で描いたメッシュの累計
        u32     parts;          // 部品の表から抜き取った数（画面に固定のとき）
    };
    Status          Read(void);
    const char *    StageName(u32 stage);
    const char *    FailName(u32 reason);

    // ---- 描画スレッド（GridCursor のフレームフックから）----
    void            FrameStep(void);
}

namespace CTRPluginFramework
{
    namespace Cheats
    {
        void    WirePlayerClone(void);
    }
}

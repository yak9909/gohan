#pragma once

#include <3ds.h>

// マップエディター（gohan Issue #16）。模様替え UI の見た目で、村の 8x8 マスを下画面に出して編集する。
//
// ゲームの模様替え UI（BsMenuInteriorEditor）は ModuleIndoor.cro にあり村では使えない（IDA-opus-5.5-F050）。
// そこで同じ資源 `Layout/chip/chip.arc` をゲームの Layout 部品で組み、動きはゲームの実装を写す。
//   盤面   … chip_room_00（kind アニメのフレーム 4 = ゲームが使う最大の 8x8。1 マス 20）。扉・窓・投函ボタンは隠す（本体以外は出さない）
//   コマ   … cip_01{C,N,P}_WWxHH。通常アイテム = C（上に置ける家具に置ける家具）、fgobj（ID <= 0xFD）= N（置けない家具）、
//             建物 = P（上に物を置ける家具。衝突判定が 1x1/2x1/1x2/2x2 の四角ならコマ 1 個、ほかは 1x1 を並べる）
//   位置   … N_All = 盤面の中心（N_Room_00 の大域位置）+ (マス − 中心) × 20、y は下へ減る（ModuleFtr 0xB0F758 と同じ式）
//   名前   … 持ち物欄と同じ吹き出し ItemSelectNameWindow（itm_slct_win.arc の N_itm_nm_00）
// カメラは公共事業エディターと同じ（FieldCamera）。元の下画面 UI（地図・タブ）は GameList の手順で退場させておく。
//
// 操作（メニューを閉じている間。プレイヤーは止める）
//   十字キー / スライドパッド … 表示する 8x8 を 1 マスずつ動かす（押し続けは公共事業エディターと同じ。CursorRepeat.hpp）
//   タッチ   … アイテムのマスなら名前を出す（建物は無視）
// 段階 1（2026-09-27）: 表示・カメラ・名前だけ。配置・削除・範囲選択・移動は段階 2 以降。
namespace MapEditor
{
    const s32       kView = 8;                  // 盤面のマス数（縦横）

    // ---- メニュースレッド ----
    void            Tick(u32 keys);             // 項目が有効な間、毎ティック。keys は Controller::GetKeysDown(true)（メニュー表示中は 0）
    void            Stop(void);
    bool            Running(void);
    void            Reset(void);                // 失敗の記憶を消す（項目を外したとき）

    // ---- 描画スレッド（PublicWorks::FrameStep から）----
    void            FrameStep(void);
}

namespace CTRPluginFramework
{
    namespace Cheats
    {
        void    WireMapEditor(void);
        bool    MapEditorTick(int index, u16 held);
        bool    MapEditorDisable(int index);
    }
}

#pragma once

#include <3ds.h>

// 建物エディター（仮名）。カメラだけを動かして、離れた場所の建物を置く・動かす・消す。
//
// カメラ: `CameraGame`（`*(0x0094A880)`）の基準位置 +4/+8/+C を毎フレーム書く。ゲームは
//   `sub_1A5124` で目標値（+276..）を基準位置へ写すので、エディターの間だけその関数の 2 語目を
//   `POP {R4-R8,PC}` にして止める（Vapecord の UNLOCKCAMERA と同じ語。JPN 0x001A5128）。
// プレイヤー: 入力遮断ケーブ A のモード 3（ボタン＋スライドパッドのビットとアナログ値）で止める。
// カーソルのマス: ゲームの UnitCursor をマス指定で並べる（GridCursor::ShowTiles）。形は
//   その建物の足元データ `Strc/data/<名>.bin` の「ゲームが属性を書くマス」（Building_WriteOccupancy と同じ規則）の、
//   外接の四角を 1 マスずつ縮めた内側（ベンチ 4x3 -> 2x1）。
// どの建物の上か: ゲームの占有マップ（PublicWorks::SlotAtTile）。
//
// 操作（メニューを閉じている間）
//   スライドパッド … カーソルを 1 マスずつ（押し続けで連続）
//   L / R         … モード（配置 → 移動 → 削除）
//   十字 左右      … 配置する建物を替える（配置モード）
//   Y + 十字       … 建物を順に選ぶ（カーソルもそこへ飛ぶ）
//   A             … 実行（配置: 置く / 移動: 選ぶ→カーソルへ動かし、選択を外す / 削除: 選んだ建物を消す）
//   X             … 配置モードで、カーソルに一番近い建物を「選んだ建物」にする
// 重なりは確かめない（利用者指示）。カーソルのマスは衝突判定の一回り小さい形。
namespace BuildingEditor
{
    enum class Mode : u8 { Place, Move, Remove, Count };

    // ---- メニュースレッド ----
    // チェック項目が有効な間、毎ティック。keys は Controller::GetKeysDown(true)（メニュー表示中は 0）。
    void            Tick(u32 keys);
    void            Stop(void);                 // チェック項目が外れたとき
    bool            Running(void);
    void            Reset(void);                // 失敗の記憶を消す（チェックを外したとき）

    // ---- 描画スレッド（PublicWorks::FrameStep から）----
    void            FrameStep(void);
}

namespace CTRPluginFramework
{
    namespace Cheats
    {
        void    WireBuildingEditor(void);
        bool    BuildingEditorTick(int index, u16 held);
        bool    BuildingEditorDisable(int index);
    }
}

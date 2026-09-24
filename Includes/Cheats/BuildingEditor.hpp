#pragma once

#include <3ds.h>

// 建物エディター（仮名）。カメラだけを動かして、離れた場所の建物を置く・動かす・消す。
//
// カメラ: `CameraGame`（`*(0x0094A880)`）の基準位置 +4/+8/+C を毎フレーム書く。ゲームは
//   `sub_1A5124` で目標値（+276..）を基準位置へ写すので、エディターの間だけその関数の 2 語目を
//   `POP {R4-R8,PC}` にして止める（Vapecord の UNLOCKCAMERA と同じ語。JPN 0x001A5128）。
// プレイヤー: 入力遮断ケーブ A のモード 3（ボタン＋スライドパッドのビットとアナログ値）で止める。
// カーソルのマス: ゲームの UnitCursor をマス指定で並べる（GridCursor::ShowTiles）。形は**衝突判定**（利用者の定義）:
//   足元データ `Strc/data/<名>.bin` の属性を書くマスのうち「物・花を置けない」マス = 花を植えられない属性
//   （ゲームの表 byte_957A35 が 0。IDA-opus-5.5-F020）。1 マスも無ければ足元の範囲を一回り削った四角。
//   高さは全部そろえて設置プレビューと同じ。
//   色: 配置 = そのまま、移動 = 青、削除 = 赤。
// どの建物の上か: 衝突判定のマスがカーソルに重なる建物のうち、基点が一番近いもの（無ければ占有マップ）。
// 画面遷移で村の屋外を離れたら止まり、戻ったら自動で再開する。項目はホットキーで入れ切りできる。
//
// 操作（メニューを閉じている間）
//   スライドパッド … カーソルを 1 マスずつ（押し続けで連続）
//   L / R         … モード（配置 → 移動 → 削除）
//   十字          … 下画面のリスト（ゲームの公共事業リストの UI）を操作。選んだ建物が配置する種類になる（GameList）
//   タッチ        … リストの行を選ぶ
//   A             … 実行（配置: 置く / 移動: 選ぶ→カーソルへ動かし、選択を外す / 削除: 選んだ建物を消す）
//   X             … 配置モードで、カーソルの下（無ければ一番近く）の建物の種類をコピーする
// 重なりは確かめない（利用者指示）。
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

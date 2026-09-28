#pragma once

#include <3ds.h>

// マップエディター（gohan Issue #16）。模様替え UI の見た目で、村の 7x7 マスを下画面に出して編集する。
//
// ゲームの模様替え UI（BsMenuInteriorEditor）は ModuleIndoor.cro にあり村では使えない（IDA-opus-5.5-F050）。
// そこで同じ資源 `Layout/chip/chip.arc` をゲームの Layout 部品で組み、動きはゲームの実装を写す。
//   盤面   … chip_room_00（kind アニメのフレーム 4 = ゲームが使う最大の 8x8 を焼いてから、大きさを 7x7 へ書き換える。1 マス 20。
//             利用者指示 2026-09-28: 9x9 に拡張 → 大きすぎたので 7x7。真ん中のマスの背景を赤く）。扉・窓・投函ボタンは隠す（本体以外は出さない）
//   コマ   … 通常アイテム = cip_01C_02x02（黄緑）、fgobj（ID <= 0xFD）= 同じ C を濃い緑に、建物 = 衝突判定のマスごとに
//             cip_01P_02x02 をオレンジに（利用者指示 2026-09-27。色はマテリアルの黒色・白色を書き換える）
//   位置   … N_All = 盤面の中心（N_Room_00 の大域位置）+ (マス − 中心) × 20、y は下へ減る（ModuleFtr 0xB0F758 と同じ式）
//   名前   … 持ち物欄と同じ吹き出し ItemSelectNameWindow（itm_slct_win.arc の N_itm_nm_00）
// カメラは公共事業エディターと同じ（FieldCamera）。元の下画面 UI（地図・タブ）は GameList の手順で退場させておく。
//
// 操作（メニューを閉じている間。プレイヤーは止める）
//   十字キー / スライドパッド … 表示する 7x7 を 1 マスずつ動かす（押し続けは公共事業エディターと同じ。CursorRepeat.hpp）
//   タッチ   … アイテムのマスなら名前を出す（建物は無視）
//   L / R    … 配置 → 削除 → 範囲選択
//   範囲選択 … スライド = 範囲を引く／タップ = そのマスを選ぶ（範囲の中なら一覧: 複製・削除・埋める・やめる）／
//              範囲の中の長押し = 持ち上げて移動（行き先は上書き）／B = 一覧・持ち上げ・選択を 1 段ずつ解く
// 段階 1〜2 は実機確認済み（2026-09-27）、段階 3（範囲選択）は実機未確認。
namespace MapEditor
{
    const s32       kView = 7;                  // 盤面のマス数（縦横。利用者指示 2026-09-28 で 8 → 9 → 7）
    enum class Mode : u8 { Place, Remove, Select };
    const u32       kModesNow = 3;              // L / R で巡回するモードの数（配置 → 削除 → 範囲選択）

    // ---- メニュースレッド ----
    void            Tick(u32 keys);             // 項目が有効な間、毎ティック。keys は Controller::GetKeysDown(true)（メニュー表示中は 0）
    void            Stop(void);
    bool            Running(void);
    void            Reset(void);                // 失敗の記憶を消す（項目を外したとき）
    // スポイトの長押しの進み（下画面のメニュー描画が進捗バーを描く）。出さないときは偽。x, y = 長押しを始めた画素
    bool            PickProgress(float &progress, int &x, int &y);
    u32             PlaceItem(void);            // 配置するアイテム（0xFFFFFFFF = 未設定）
    void            SetPlaceItem(u32 id);

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

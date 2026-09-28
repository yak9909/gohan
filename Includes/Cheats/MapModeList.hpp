#pragma once

#include <3ds.h>

// マップエディターの下画面左端のモード一覧（利用者の依頼 2026-09-28）。
//
// 村の下画面で地図の右に出る建物一覧 = ゲームの BsMenuMapList（map_village.arc の list_00）をそのまま組み、
// 行の選択・音・押した見た目・選んだ見た目（文字の色・N_slct_00 の帯）はゲームの関数に任せる。
// ゲームとの違いは利用者の指定した 4 つだけ: 開閉できない・ずっと開いた・選択を解除できない・アイコンを描かない。
// 見た目の調整（利用者の指定）: 矢印とページ送りを隠す、真ん中を切って縦を詰める、左の画面外へはみ出させる、文字を右へ寄せる。
// 解析: work/evidence/map_editor/mode_list/analysis.md（IDA-opus-5.5-F062）、見本: tools/layout/mode_list_preview.mjs。
//
// ★呼ぶのはゲームのスレッド（MapEditor::FrameStep）だけ。
namespace MapModeList
{
    const u32       kRows = 3;                  // 配置・削除・範囲選択（MapEditor::Mode の順）

    bool            LoadStep(void);             // arc を読む。済めば真（毎フレーム呼ぶ）
    // 組み立て（1 フレームに 1 段）。済めば真。heapOk = nw::lyt のヒープに余裕がある。mode = 最初に選ぶ行
    bool            BuildStep(bool heapOk, u8 mode);
    const char *    Error(void);                // 失敗の理由（無ければ空）
    bool            Built(void);
    void            Enter(void);                // 入場（list_00_in）
    void            Leave(void);                // 退場（list_00_out）
    bool            Animating(void);            // 入場・退場の途中
    // 毎フレーム。mode = いまのモード（見た目をこれに合わせる）。held / x / y = 一覧が受け持つ指（下画面の画素）。
    // 戻り値: 一覧で決まったモード（無ければ −1）
    s32             Frame(u8 mode, bool held, u16 x, u16 y);
    void            Draw(void *layoutMgr);      // 描画登録（盤面と盤面のコマの後に呼ぶ = 盤面より手前）
    bool            Contains(u32 x, u32 y);     // この画素で始まった指は一覧が受け持つ
    void            Destroy(void);              // 描画登録をやめて数フレーム後に
}

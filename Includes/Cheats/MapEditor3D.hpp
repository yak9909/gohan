#pragma once

#include <3ds.h>

// マップエディターの上画面の 3D（設計: acnl_disassemble docs/topics/map_editor_3d.md、根拠 IDA-opus-5.5-F055）。
//   - 赤いハイライト: 村のアイテムの実体（fgobj::ObjectBase）の材質を写して、TEV の最終段で赤と混ぜる（BuildingHighlight と同じ手）。
//   - 移動の複製: アイテムのモデル（村の季節資源を借りる）で自前の体を作り、白を混ぜて半透明で少し浮かせて描く（BuildingPreview と同じ手）。
// 全部ゲームの描画スレッド（MapEditor::FrameStep）から呼ぶ。
namespace MapEditor3D
{
    struct Clone
    {
        u32 item;       // 村のアイテム（上位 16 ビットの旗ごと）
        u8  x, y;       // 行き先のマス
    };
    static const u32 kMaxClones = 48;

    // 毎フレーム。highlight(x, y) が真のマスの実体を赤くし、clones を描く。
    void    Frame(bool (*highlight)(s32 x, s32 y), const Clone *clones, u32 count);
    // エディターを止める: 赤を戻し、複製を壊し、ヒープを返す（数フレームかけて。終わったら真）。
    bool    Release(void);
    // すぐに片付ける（描くのはもうやめてある前提）。場面が同じなら赤を戻して複製を壊し、変わっていればゲームの物には触らずにヒープだけ返す。
    void    Abandon(void);
}

#pragma once

#include <3ds.h>

// マップエディターの上画面の 3D（設計: acnl_disassemble docs/topics/map_editor_3d.md、根拠 IDA-opus-5.5-F055 / F056）。
//   - 赤いハイライト: 村のアイテムの実体（fgobj）の体の資源から自前の体を作り、赤を混ぜ、実体の行列を写して少し大きく重ねる。
//     実体の材質は書き換えない（同じモデルで共有されていて、書き換えると他の同じ物も赤くなり、ゲームの書き出しで落ちた）。
//   - 移動の複製: アイテムのモデル（元の実体の資源、無ければゲームと同じ引き方）で自前の体を作り、白を混ぜて半透明で少し浮かせて描く。
// 全部ゲームの描画スレッド（MapEditor::FrameStep）から呼ぶ。
namespace MapEditor3D
{
    struct Clone
    {
        u32 item;       // 村のアイテム（上位 16 ビットの旗ごと）
        u8  x, y;       // 行き先のマス
        u8  srcX, srcY; // 元のマス（そこに実体があれば、その体の資源を使う）
    };
    static const u32 kMaxClones = 48;

    // 毎フレーム。highlight(x, y) が真のマスの実体を赤くし、clones を描く。
    void    Frame(bool (*highlight)(s32 x, s32 y), const Clone *clones, u32 count);
    // エディターを止める: 赤を戻し、複製を壊し、ヒープを返す（数フレームかけて。終わったら真）。
    bool    Release(void);
    // すぐに片付ける（描くのはもうやめてある前提）。場面が同じなら赤を戻して複製を壊し、変わっていればゲームの物には触らずにヒープだけ返す。
    void    Abandon(void);
}

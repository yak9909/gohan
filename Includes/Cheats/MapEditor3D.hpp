#pragma once

#include <3ds.h>

// マップエディターの上画面の 3D（設計: acnl_disassemble docs/topics/map_editor_3d.md、根拠 IDA-opus-5.5-F055〜F059）。
//   - 赤いハイライト: 村の物体の描画関数の表を包み、選んだ物体だけ TEV 段 5 に赤を積む（F058。体もヒープも使わない）。
//   - 移動の複製: アイテムのモデル（元の実体の資源、無ければゲームと同じ引き方）で自前の体をモデルの種類ごとに 1 つ作り、
//     白を混ぜて半透明にし、描画ノードが行き先ごとに行列を変えて少し浮かせて描く（F059）。
// 全部ゲームの描画スレッド（MapEditor::FrameStep）から呼ぶ。
namespace MapEditor3D
{
    struct Clone
    {
        u32 item;       // 村のアイテム（上位 16 ビットの旗ごと）
        u8  x, y;       // 行き先のマス
        u8  srcX, srcY; // 元のマス（そこに実体があれば、その体の資源を使う）
    };
    static const u32 kMaxClones = 128;      // 盤面 8x8 の周り 1 マスで最大 100 か所

    // ハイライトの種類（利用者指示 2026-09-29: 赤 = 選択・削除、青 = スポイト、白 = 配置モードの中心マス）
    static const u8 kHighlightNone = 0, kHighlightRed = 1, kHighlightBlue = 2, kHighlightWhite = 3;
    static const u8 kHighlightKinds = 3;
    // 毎フレーム。highlight(x, y) が返す種類（0 = 無し）の色でマスの実体を塗り、clones を描く。
    void    Frame(u8 (*highlight)(s32 x, s32 y), const Clone *clones, u32 count);
    // 種類ごとの濃さ（TEV 段 5 の定数アルファ = 色を混ぜる割合。0〜255）
    void    SetHighlightStrength(u8 kind, u8 strength);
    u8      HighlightStrength(u8 kind);
    // エディターを止める: 赤を戻し、複製を壊し、ヒープを返す（数フレームかけて。終わったら真）。
    bool    Release(void);
    // すぐに片付ける（描くのはもうやめてある前提）。場面が同じなら赤を戻して複製を壊し、変わっていればゲームの物には触らずにヒープだけ返す。
    void    Abandon(void);
}

#pragma once

#include <3ds.h>

// 選んだ建物に色を合成し、透明度を sin 波で揺らす（IDA-opus-5.5-F010〜F013）。
//
// 建物の部品はゲームのデータだけで集める: 建物表のスロット → BsStrcMgr の実体 → 子プロセス
// （基本マネージャのノード +8 / +16）→ 各オブジェクトの前 16 B の見出し +0xC の大きさ →
// その範囲の nw::gfx::Model / SkeletalModel 参照（範囲から参照される hobj:: の中も）。
// 材質ごとに TEV の最終段を「前段と定数色を混ぜる」に替え（空き段が無ければ詰める・近似する）、
// フラグメント設定を定数アルファのブレンドにする。簡易版の書き出し関数（事前コマンドを流すだけ）は
// そのモデルだけ汎用 0x49C08C へ差し替える。写しと元の値は gohan 自身の配列に持つ。
//
// ゲームのメモリはすべてゲームのスレッド（FrameStep）で触る。メニューは要求を置くだけ。
namespace BuildingHighlight
{
    struct Params
    {
        u32 color;          // 0x00BBGGRR（アルファは使わない）
        u8  tint;           // 色の合成度合い 0〜255
        u8  alpha;          // 不透明度の中心 0〜255（255 = 不透明）
        u8  wave;           // 不透明度の揺れ幅 0〜255
        u8  speed;          // 揺れの速さ 1〜100（100 で 10 フレーム周期）
    };

    enum class State : u32
    {
        Off,
        Pending,            // 要求を受けた。次のフレームで当てる
        Active,
        Failed,
    };

    // 建物表の (id, x, y) の建物を光らせる。すでに別の建物が光っていれば先に戻す。
    void            Select(u16 id, u8 x, u8 y);
    // 戻す（次のフレームで）。
    void            Clear(void);
    // 今すぐ戻す。ゲームのスレッド（FrameStep の中）からだけ呼ぶ。建物を消す・動かす前に使う。
    void            ClearNow(void);
    void            SetParams(const Params &params);
    // 色だけ替える（建物エディター: 移動は青、削除は赤）。次のフレームから反映。
    static const u32 kBlue = 0x00FFB060u;   // 0x00BBGGRR（F011〜F013 の実機の色）
    static const u32 kRed = 0x004040FFu;
    void            SetColor(u32 color);                // 合成度合いはメニューの値
    static const u32 kWhite = 0x00FFFFFFu;
    void            SetStyle(u32 color, s16 tint);      // 合成度合いも決める（負ならメニューの値）
    // いま光らせている建物の (id, x, y)。光っていなければ false。
    bool            Current(u16 &id, u8 &x, u8 &y);
    const Params &  GetParams(void);
    State           GetState(void);
    // 直前の適用で触った材質の数（通知用）。
    u32             MaterialCount(void);
    u32             ModelCount(void);

    // ゲームのスレッドから毎フレーム（グリッドカーソルのフックの相乗り枠）。
    void            FrameStep(void);

    // ---- ほかの描画部品（設置プレビュー・グリッドカーソル）と共有する道具。描画スレッドから ----
    // [addr, addr+len) が読めるか（svcQueryMemory。結果は FrameStep の先頭で捨てる）
    bool            SafeReadable(u32 addr, u32 len);
    // RTTI が nw::gfx::Material か（読めるかを確かめてから辿る）
    bool            LooksLikeMaterial(u32 obj);
    // TEV ブロック（244 B、書き換えてよい写し）の最終段を「前段と定数色 5 を混ぜる」に組み替える。
    // 0 = できない / 1 = 空き段 / 2 = 詰めた / 3 = 近似（Constant5 は前乗算）。F011〜F013。
    int             PlanTev(u8 *block, u32 colour);
    u32             TintConstant(u32 color, u8 strength, bool premultiplied);
}

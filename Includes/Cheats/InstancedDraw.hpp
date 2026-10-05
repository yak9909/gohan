#ifndef INSTANCED_DRAW_HPP
#define INSTANCED_DRAW_HPP

// 体 1 つを行列だけ変えて何か所にも描く（IDA-opus-5.5-F059。村の物体 fgobj と同じ作法）。
//
// ゲームの描画ノード g3d::FuncNode を 1 つ作り、毎フレーム描画リスト（シーン 0）へ出す。そのコールバックが、
// 束（体のホルダ + 行列の並び）ごとに、行列を書く → 世界行列の更新 → 視点 × 行列（+444）→ メッシュを描く、を繰り返す。
// ヒープは体の数（モデルの種類の数）だけで、置く数には依らない。
//
// 使うのは描画スレッド（GridCursor の FrameCallback とその相乗り）だけ。

#include <3ds/types.h>

namespace InstancedDraw
{
    // 行列 3x4（SetMatrix3x4 に渡す形。平行移動は [3] [7] [11]）
    struct Batch
    {
        void *          holder;         // ModelInstance のホルダ（+4 = 体）
        const float (*  matrices)[12];
        u32             count;
    };

    struct Drawer
    {
        alignas(8) u8   holder[16];     // FuncNode のホルダ（+4 = ノード）
        const Batch *   batches;        // このフレームに描く束（呼ぶ側の static な配列）
        u32             batchCount;
        u32             drawn;          // 描いた数（直近のフレーム。確認用）
        u32             skipped;        // コマンドバッファの残りが足りずに描かなかった数
        bool            underArmed;     // 物体の下に描く分がこのフレームに登録された（描いたら落とす）
        bool            overArmed;      // 村の物体の層 0 の後に描く分がこのフレームに登録された（描いたら落とす）
        bool            layer1Armed;    // 村の物体の層 1 の前に描く分がこのフレームに登録された（描いたら落とす）
        bool            underFirst;     // SubmitUnder の中でほかより先に描く（マスの色。カーソルをその上に）。Create のあとに立てる
        bool            groundFirst;    // SubmitUnder のとき、マイデザインを先に描かせる（エディターだけ。立っている登録が無いフレームは並べ替えない）
    };

    // FuncNode を作る。allocator は ssys::ma::HeapAllocator（vtable +8 で確保）。
    // ★ゲームの生成関数は確保の失敗を確かめないので、ヒープの残りを kCreateBytes 以上にしてから呼ぶこと。
    static const u32 kCreateBytes = 0x800;
    bool    Create(Drawer &d, void *allocator);
    bool    Created(const Drawer &d);
    // 描画リストへ出すのをやめて数フレーム経ってから（ヒープを返す前に）。
    void    Destroy(Drawer &d);
    // このフレームに描く束を渡して描画リストへ出す。束は描き終わるまで（このフレームの間）生きていること。
    void    Submit(Drawer &d, const Batch *batches, u32 count);
    // ★村の物体（fgobj）の下に描く（IDA-opus-5.5-F061。利用者指示 2026-09-28: UnitCursor は常に対象マスのモデルより下／背面）。
    //   村の物体の描画ノード（*0x948E70 + 0x45B0 の +4）の層 0 のコールバック（+0x148 = 0x59A900）を包み、その先頭で描く
    //   = 地面の後・村の物体の前。束のメッシュは層を問わずこのとき描く。描けない場面（村の物体の描画ノードが無い）では false
    //   （呼ぶ側が Submit に切り替える）。深度を書かない体にしておくこと（DisableDepthWrite）。
    //   ★groundFirst の登録があるフレームは、その前に村の物体の層 1 の一覧からマイデザインだけを抜いてゲームの fgobj_DrawList で
    //   先に描く（IDA-opus-5.5-F066）= 地面 → マイデザイン → ここで描く物 → 残りの村の物体。無いフレームは並べ替えない。
    bool    SubmitUnder(Drawer &d, const Batch *batches, u32 count);
    // ★村の物体の層 0（マイデザインなど地面の物を含む）の後に描く（利用者指示 2026-09-28: マップエディターのマスの色は
    //   マイデザインより上・それ以外より下）。SubmitUnder と同じ包みで、0x59A900 を呼んだあとに描く。描画ノードは作らなくてよい
    //   （Create 不要）。深度は書かない体にしておくこと。層 0 の物体の手前の画素は深度テストで隠れる。
    bool    SubmitOver(Drawer &d, const Batch *batches, u32 count);
    // 村の物体の層 1 のコールバック（node+0x14C = 0x59BFEC）を包み、その先頭で描く = すべての描画ノードの層 0 の後、村の物体の層 1 の前。
    bool    SubmitBeforeLayer1(Drawer &d, const Batch *batches, u32 count);
    // 体の材質の深度書き込みを切る（フラグメント部分 M+0x50 の +280 bit1 を落とし、鍵 +720 を 0 に。汎用の書き出し 0x49CA94〜0x49CAF8）。
    //   体ごとの写し（bufferOption 0x834）か自前の資源の体にだけ使うこと
    void    DisableDepthWrite(void *holder);
}

#endif

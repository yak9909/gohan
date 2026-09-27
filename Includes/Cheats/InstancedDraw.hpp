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
}

#endif

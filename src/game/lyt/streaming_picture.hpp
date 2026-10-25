#pragma once
#include "game/generated/api.hpp"

namespace gohan::game::lyt::detail {
using PictureDraw = decltype(generated::NwLytPaneVtable::draw_self);
// 元の Picture 内の呼び出しと流し出し本体の ARM 命令で確認した約束。
// R0=描画の状態、R1=書き込み先カーソル、戻り値 R0=進めたカーソル。
// 有効なステージング配列と十分な記録領域が必要。空のバッチには呼ばない。
// 各配列の先頭語をヘッダー用に書き換え、個数をゼロに戻すので、
// 流した配列の内容をそのまま再送せず、以後は元の Picture に積み直させる。
using FlushBatch = unsigned *(*)(generated::NwLytDrawContext *, unsigned *);

// 描画の状態（NwLytDrawContext。生成の型には欄が無い）の +0x24 の 1 バイトは、
// まだ流していない板（四角形）の数。0 以外なら溜まったバッチがある（最大 26）。
// ゲーム自身の描画の終わりの処理も、この値が 0 以外の時だけバッチを流す。
inline constexpr unsigned PendingQuadCountOffset = 0x24;

// GPU レジスタ 0x080（テクスチャ単位の設定）の第 3 バイトだけを書く命令（マスク 0x4）。
// データの bit16 がテクスチャキャッシュの無効化で、ゲームのテクスチャ切り替えも同じ書き込みで
// bit16 を立てる。記録列に入る順は、データの語、ヘッダーの語。
inline constexpr unsigned TextureCacheInvalidateData = 0x00010000;
inline constexpr unsigned TextureCacheInvalidateHeader = 0x00040080;

// 遅延中の板を先に流し、今回の板より前に必ずテクスチャキャッシュを無効化する。
// レジスタの第3バイトだけを書くので、テクスチャ有効単位・座標等は変えない。
// 差分キャッシュにも依存しない。記録列を再生する度にこの命令も再生される。
#ifdef __3DS__
inline unsigned *StreamingPicture(generated::NwLytPane *pane, unsigned *cursor,
                                 generated::NwLytDrawInfo *info, generated::NwLytDrawContext *context) {
    // 整数番地から作った関数ポインタを別の関数の引数に渡さず、ここで直接呼ぶ。
    // 使用中の ARM コンパイラは、その間接呼び出しを関数間最適化で消し、
    // 復帰もしないコードを生成する。実際の最適化済み ARM を回帰試験する。
    const bool pending = reinterpret_cast<const unsigned char *>(context)[PendingQuadCountOffset] != 0;
    if (pending)
        cursor = reinterpret_cast<FlushBatch>(generated::DrawCtx_FlushBatch_addr)(context, cursor);
    *cursor++ = TextureCacheInvalidateData;
    *cursor++ = TextureCacheInvalidateHeader;
    return reinterpret_cast<PictureDraw>(generated::nwlyt_Picture_DrawToCommandList_addr)(pane, cursor, info, context);
}
#endif
} // namespace gohan::game::lyt::detail

#pragma once
#include "game/generated/api.hpp"

namespace gohan::game::lyt::detail {
using PictureDraw = decltype(generated::NwLytPaneVtable::draw_self);
// 板のバッチの流し出し。型は symbols に無いので、ここで宣言する。
// 引数は描画の状態とコマンドのカーソルで、進めたカーソルを返す。
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
inline unsigned *DrawStreamingPicture(generated::NwLytPane *pane, unsigned *cursor,
                                     generated::NwLytDrawInfo *info, generated::NwLytDrawContext *context,
                                     bool pending, FlushBatch flush, PictureDraw draw) {
    if (pending) cursor = flush(context, cursor);
    *cursor++ = TextureCacheInvalidateData;
    *cursor++ = TextureCacheInvalidateHeader;
    return draw(pane, cursor, info, context);
}

#ifdef __3DS__
inline unsigned *StreamingPicture(generated::NwLytPane *pane, unsigned *cursor,
                                 generated::NwLytDrawInfo *info, generated::NwLytDrawContext *context) {
    const bool pending = reinterpret_cast<const unsigned char *>(context)[PendingQuadCountOffset] != 0;
    return DrawStreamingPicture(pane, cursor, info, context, pending,
        reinterpret_cast<FlushBatch>(generated::DrawCtx_FlushBatch_addr),
        reinterpret_cast<PictureDraw>(generated::nwlyt_Picture_DrawToCommandList_addr));
}
#endif
} // namespace gohan::game::lyt::detail

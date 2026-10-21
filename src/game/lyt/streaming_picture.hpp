#pragma once
#include "game/generated/api.hpp"

namespace gohan::game::lyt::detail {
using PictureDraw = decltype(generated::NwLytPaneVtable::draw_self);
using FlushBatch = unsigned *(*)(generated::NwLytDrawContext *, unsigned *);
inline constexpr std::uintptr_t PictureDrawAddress = 0x0073CB58;
inline constexpr std::uintptr_t FlushBatchAddress = 0x004B8360;

// 遅延中の板を先に流し、今回の板より前に必ずテクスチャキャッシュを無効化する。
// レジスタの第3バイトだけを書くので、テクスチャ有効単位・座標等は変えない。
// 差分キャッシュにも依存しない。記録列を再生する度にこの命令も再生される。
inline unsigned *DrawStreamingPicture(generated::NwLytPane *pane, unsigned *cursor,
                                     generated::NwLytDrawInfo *info, generated::NwLytDrawContext *context,
                                     bool pending, FlushBatch flush, PictureDraw draw) {
    if (pending) cursor = flush(context, cursor);
    *cursor++ = 0x00010000;
    *cursor++ = 0x00040080;
    return draw(pane, cursor, info, context);
}

#ifdef __3DS__
inline unsigned *StreamingPicture(generated::NwLytPane *pane, unsigned *cursor,
                                 generated::NwLytDrawInfo *info, generated::NwLytDrawContext *context) {
    const bool pending = reinterpret_cast<const unsigned char *>(context)[0x24] != 0;
    return DrawStreamingPicture(pane, cursor, info, context, pending,
        reinterpret_cast<FlushBatch>(FlushBatchAddress), reinterpret_cast<PictureDraw>(PictureDrawAddress));
}
#endif
} // namespace gohan::game::lyt::detail

// 製品と同じコールバックを、最適化後の機械語で取り出す。
#include "game/lyt/streaming_picture.hpp"

using namespace gohan::game;
extern "C" lyt::detail::PictureDraw StreamingEntry() {
    return lyt::detail::StreamingPicture;
}
extern "C" const unsigned GameEntries[] = {
    generated::nwlyt_Picture_DrawToCommandList_addr,
    generated::DrawCtx_FlushBatch_addr,
    generated::PictureDrawEntry_len, generated::PictureDrawEntry_crc32,
    generated::FlushBatchEntry_len, generated::FlushBatchEntry_crc32,
};

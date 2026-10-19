#pragma once
#include "game/generated/api.hpp"
#include <cstddef>
#include <cstdint>

namespace gohan::game::lyt::detail {
// ゲームのヒープの空き（導入前の確認用）。
struct HeapSpace {
    unsigned freeBytes = 0;
    unsigned largestBlock = 0;
};

// ゲーム API の境界。PC 試験でも生成されたものと同じ関数型を使い、代役で差し替える。
struct ResourceApi {
    generated::SsysMaAllocator *(*allocator)();
    generated::ssys_ma_Allocator_Alloc_t allocate;
    generated::ssys_ma_Allocator_Free_t free;
    generated::GpuCmd_GetProperty_t property;
    generated::GpuCmd_CreateLists_t create;
    generated::GpuCmd_SelectList_t select;
    generated::GpuCmd_AllocateListStorage_t storage;
    generated::GpuCmd_DestroyLists_t destroy;
    bool (*flush)(void *, std::size_t);
    // GPU が読むメモリの物理番地。
    std::uint32_t (*physical)(const void *);
    // 確保元のヒープの形を確かめ、空きの量を返す。ヒープでなければ false。
    bool (*heapSpace)(generated::SsysMaAllocator *, int alignment, HeapSpace *);
    generated::SsysMaLayoutMgr *(*manager)();
    generated::ssys_ma_lyt_LayoutMgr_AddLayout_t addLayout;
    generated::nwfont_TextBatch_Init_t batchInit;
    generated::nwlyt_TextBox_CalcDrawBufferBytes_t bufferBytes;
    // ゲームが用意する既定の文字の処理器が使えるか。
    bool (*textReady)();
};
// VerifyRenderer が成功した後にだけ使用する。
const ResourceApi &GameResourceApi();
} // namespace gohan::game::lyt::detail

#include "game/lyt/resource_api.hpp"
#include <3ds.h>

namespace gohan::game::lyt::detail {
namespace {
generated::SsysMaAllocator *Allocator() {
    return *reinterpret_cast<generated::g_LayoutAllocator_t *>(generated::g_LayoutAllocator_addr);
}
generated::SsysMaLayoutMgr *Manager() {
    return *reinterpret_cast<generated::g_LayoutMgr_t *>(generated::g_LayoutMgr_addr);
}
bool Flush(void *data, std::size_t bytes) {
    return R_SUCCEEDED(svcFlushProcessDataCache(CUR_PROCESS_HANDLE,
                                              reinterpret_cast<std::uintptr_t>(data), bytes));
}
std::uint32_t Physical(const void *data) { return osConvertVirtToPhys(data); }

bool Space(generated::SsysMaAllocator *allocator, int alignment, HeapSpace *out) {
    if (!allocator || !allocator->heap)
        return false;
    auto *heap = allocator->heap;
    const auto *table = reinterpret_cast<const generated::vtbl_sead_ExpHeap_t *>(
        generated::vtbl_sead_ExpHeap_addr);
    // 想定した種類のヒープだけに、型付きの仮想表の呼び出しを使う。
    if (heap->vptr != table)
        return false;
    out->freeBytes = table->get_free_size(heap);
    out->largestBlock = table->get_max_allocatable_size(heap, alignment);
    return true;
}

bool TextReady() {
    const auto *processor = reinterpret_cast<const generated::g_DefaultWideTagProcessor_t *>(
        generated::g_DefaultWideTagProcessor_addr);
    return processor->vptr == reinterpret_cast<const generated::vtbl_nw_font_TagProcessorBase_wchar_t_t *>(
                                  generated::vtbl_nw_font_TagProcessorBase_wchar_t_addr);
}
} // namespace

const ResourceApi &GameResourceApi() {
    static const ResourceApi api{
        Allocator,
        reinterpret_cast<generated::ssys_ma_Allocator_Alloc_t>(generated::ssys_ma_Allocator_Alloc_addr),
        reinterpret_cast<generated::ssys_ma_Allocator_Free_t>(generated::ssys_ma_Allocator_Free_addr),
        reinterpret_cast<generated::GpuCmd_GetProperty_t>(generated::GpuCmd_GetProperty_addr),
        reinterpret_cast<generated::GpuCmd_CreateLists_t>(generated::GpuCmd_CreateLists_addr),
        reinterpret_cast<generated::GpuCmd_SelectList_t>(generated::GpuCmd_SelectList_addr),
        reinterpret_cast<generated::GpuCmd_AllocateListStorage_t>(generated::GpuCmd_AllocateListStorage_addr),
        reinterpret_cast<generated::GpuCmd_DestroyLists_t>(generated::GpuCmd_DestroyLists_addr),
        Flush,
        Physical,
        Space,
        Manager,
        reinterpret_cast<generated::ssys_ma_lyt_LayoutMgr_AddLayout_t>(generated::ssys_ma_lyt_LayoutMgr_AddLayout_addr),
        reinterpret_cast<generated::nwfont_TextBatch_Init_t>(generated::nwfont_TextBatch_Init_addr),
        reinterpret_cast<generated::nwlyt_TextBox_CalcDrawBufferBytes_t>(generated::nwlyt_TextBox_CalcDrawBufferBytes_addr),
        TextReady,
    };
    return api;
}
} // namespace gohan::game::lyt::detail

#include "game/lyt/recording_resources.hpp"
#include <3ds.h>

namespace gohan::game::lyt::detail {
namespace {
generated::SsysMaAllocator *Allocator() {
    return *reinterpret_cast<generated::g_LayoutAllocator_t *>(generated::g_LayoutAllocator_addr);
}
bool Flush(void *data, std::size_t bytes) {
    return R_SUCCEEDED(svcFlushProcessDataCache(CUR_PROCESS_HANDLE,
                                              reinterpret_cast<std::uintptr_t>(data), bytes));
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
    };
    return api;
}
} // namespace gohan::game::lyt::detail

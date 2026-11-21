#include "game/heap.hpp"
#include "game/generated/api.hpp"
namespace gohan::game::heap {
Room Measure(Kind kind, int alignment) {
    using namespace generated;
    SeadExpHeapPrefix *heap = nullptr;
    if (kind == Kind::Command) {
        auto *mgr = *reinterpret_cast<g_GraphicMemoryMgr_t *>(g_GraphicMemoryMgr_addr);
        if (mgr) heap = static_cast<SeadExpHeapPrefix *>(mgr->commandHeap);
    } else {
        auto *alloc = *reinterpret_cast<SsysMaAllocator **>(
            kind == Kind::Layout ? g_UiLayoutAllocator_addr : g_UiResourceAllocator_addr);
        if (alloc) heap = alloc->heap;
    }
    Room room;
    if (!heap || heap->vptr != reinterpret_cast<const vtbl_sead_ExpHeap_t *>(vtbl_sead_ExpHeap_addr)) return room;
    room.free_bytes = reinterpret_cast<sead_ExpHeap_getFreeSize_t>(sead_ExpHeap_getFreeSize_addr)(heap);
    room.contiguous_bytes = reinterpret_cast<sead_ExpHeap_getMaxAllocatableSize_t>(sead_ExpHeap_getMaxAllocatableSize_addr)(heap, alignment);
    room.measured = true;
    return room;
}
} // namespace gohan::game::heap

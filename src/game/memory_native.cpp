#include "game/memory.hpp"
#include "core/frame.hpp"
#include "game/hook_batch.hpp"
#include <3ds.h>
#include <cstring>

namespace gohan::game::memory {
namespace {
bool Query(std::uintptr_t address, Region &region) {
    MemInfo memory; PageInfo page;
    if (R_FAILED(svcQueryMemory(&memory, &page, address))) return false;
    region = {memory.base_addr, memory.size, memory.state != MEMSTATE_FREE && (memory.perm & MEMPERM_READ) != 0};
    return true;
}
bool Copy(std::uintptr_t address, void *out, std::size_t size) { std::memcpy(out, reinterpret_cast<const void *>(address), size); return true; }
Reader reader({Query, Copy});
bool frame_cache = false;
bool Wire() {
    const auto result = core::frame::Global().Add(BeginFrame, -100).result;
    frame_cache = result == core::frame::Result::Queued || result == core::frame::Result::Unchanged;
    return frame_cache;
}
void Shutdown() { PausedGame pause; frame_cache = false; core::frame::Global().Remove(BeginFrame); }
const core::Feature feature{"game.memory", Wire, Shutdown, core::ShutdownStage::GameHooks};
}
void BeginFrame() { reader.BeginFrame(); }
bool Readable(std::uintptr_t address, std::size_t length) {
    if (!frame_cache) reader.BeginFrame();
    return reader.Readable(address, length);
}
bool IsClass(std::uintptr_t object, const char *name) {
    if (!frame_cache) reader.BeginFrame();
    return reader.IsClass(object, name);
}
const core::Feature &Feature() { return feature; }
} // namespace gohan::game::memory

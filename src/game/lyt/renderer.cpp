#include "game/lyt/renderer.hpp"
#include "game/lyt/engine.hpp"
#include "game/lyt/verification.hpp"
#include <CTRPluginFramework.hpp>
#include "core/sync.hpp"
#include <new>
#include <cstring>
#include <cstdlib>

namespace gohan::game::lyt {
namespace {
core::SpinMutex rendererMutex;
detail::Engine *engine = nullptr;
void *engineMemory = nullptr;
bool verified = false;
RendererMemory memoryStatus;

void ReleaseEngine() {
    if (!engine) return;
    engine->~Engine();
    std::free(engineMemory);
    engine = nullptr;
    engineMemory = nullptr;
}

bool AcquireEngine(const FontResource &font) {
    if (engine) return true;
    const auto &api = detail::GameResourceApi();
    // CPU側の木・文字枠はプラグイン自身のヒープへ置く。
    if (!api.manager() || !api.textReady() || !detail::Scene::Prepare(api, font)) return false;
    constexpr auto alignment = alignof(detail::Engine);
    engineMemory = std::malloc(sizeof(detail::Engine) + alignment - 1);
    if (!engineMemory) return false;
    void *memory = reinterpret_cast<void *>((reinterpret_cast<std::uintptr_t>(engineMemory) + alignment - 1) & ~(alignment - 1));
    // 元の静的領域と同じ初期値にする。ゲームの部品の未使用欄も0を保つ。
    std::memset(memory, 0, sizeof(detail::Engine));
    engine = new (memory) detail::Engine(api);
    return true;
}
} // namespace

InstallResult InstallRenderer(const FontResource &font, BackgroundAllocation *background) {
    core::LockGuard guard(rendererMutex);
    if (background) *background = {};
    const auto result = detail::VerifyRenderer();
    if (result != InstallResult::Ready)
        return result;
    verified = true;
    if (!AcquireEngine(font)) return InstallResult::ResourceFailed;
    const auto installed = engine->Install(font);
    memoryStatus = engine->Resources().MemoryStatus();
    if (background) *background = engine->Resources().BackgroundStatus();
    if (installed != InstallResult::Ready) ReleaseEngine();
    return installed;
}

RendererMemory ReadRendererMemory() {
    core::LockGuard guard(rendererMutex);
    if (engine) memoryStatus = engine->Resources().MemoryStatus();
    if (verified) {
        const auto &api = detail::GameResourceApi();
        detail::HeapSpace space;
        auto *allocator = api.allocator();
        memoryStatus.current.measured = allocator && api.heapSpace(allocator, 128, &space);
        memoryStatus.current.freeBytes = space.freeBytes;
        memoryStatus.current.largestBlock = space.largestBlock;
    }
    memoryStatus.pluginBytes = engine ? sizeof(detail::Engine) + alignof(detail::Engine) - 1 : 0;
    if (!engine) {
        memoryStatus.gameBytes = memoryStatus.gameUpperBytes = memoryStatus.commandBytes = 0;
        memoryStatus.fontBytes = memoryStatus.uiBytes = memoryStatus.windowBytes = memoryStatus.selectionBytes = 0;
    }
    return memoryStatus;
}

bool Present(const Drawing &drawing) {
    if (!rendererMutex.TryLock()) return true;
    const bool result = engine && engine->Present(drawing);
    rendererMutex.Unlock();
    return result;
}
bool UpdateSelection(const std::uint8_t *packed, unsigned &slot) {
    if (!rendererMutex.TryLock()) return false;
    const bool result = engine && engine->UpdateSelection(packed, slot);
    rendererMutex.Unlock();
    return result;
}

bool DetachRenderer() {
    if (!rendererMutex.TryLock()) return false;
    const bool result = !engine || engine->Detach();
    // エンジン内の木・文字バッチも、ノードとGPUの参照の排出が済んでから返す。
    if (result) {
        if (engine) memoryStatus = engine->Resources().MemoryStatus();
        ReleaseEngine();
    }
    rendererMutex.Unlock();
    return result;
}

void WaitMilliseconds(unsigned milliseconds) {
    CTRPluginFramework::Sleep(CTRPluginFramework::Milliseconds(milliseconds));
}
} // namespace gohan::game::lyt

#include "game/lyt/renderer.hpp"
#include "game/lyt/engine.hpp"
#include "game/lyt/verification.hpp"
#include <CTRPluginFramework.hpp>
#include "core/sync.hpp"
#include <new>
#include <cstring>

namespace gohan::game::lyt {
namespace {
core::SpinMutex rendererMutex;
detail::Engine *engine = nullptr;
generated::SsysMaAllocator *engineAllocator = nullptr;
constexpr int EngineAlignment = 128;

void ReleaseEngine() {
    if (!engine) return;
    engine->~Engine();
    detail::GameResourceApi().free(engineAllocator, engine);
    engine = nullptr;
    engineAllocator = nullptr;
}

bool AcquireEngine(const FontResource &font) {
    if (engine) return true;
    const auto &api = detail::GameResourceApi();
    // 照合後、前提と空きを確かめてからゲームのヒープへCPU側の木・文字枠を置く。
    if (!api.manager() || !api.textReady() || !detail::Scene::Prepare(api, font)) return false;
    auto *allocator = api.allocator();
    detail::HeapSpace space;
    if (!allocator || !api.heapSpace(allocator, EngineAlignment, &space) ||
        space.largestBlock < sizeof(detail::Engine) + EngineAlignment ||
        space.freeBytes < sizeof(detail::Engine) + EngineAlignment + core::limits::RendererHeapReserve) return false;
    void *memory = api.allocate(allocator, sizeof(detail::Engine), EngineAlignment);
    if (!memory) return false;
    // 元の静的領域と同じ初期値にする。ゲームの部品の未使用欄も0を保つ。
    std::memset(memory, 0, sizeof(detail::Engine));
    engineAllocator = allocator;
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
    if (!AcquireEngine(font)) return InstallResult::ResourceFailed;
    const auto installed = engine->Install(font);
    if (background) *background = engine->Resources().BackgroundStatus();
    if (installed != InstallResult::Ready) ReleaseEngine();
    return installed;
}

bool Present(const Drawing &drawing) {
    if (!rendererMutex.TryLock()) return true;
    const bool result = engine && engine->Present(drawing);
    rendererMutex.Unlock();
    return result;
}
bool UpdateSelection(const std::uint8_t *data, std::size_t bytes, unsigned &slot) {
    if (!rendererMutex.TryLock()) return false;
    const bool result = engine && engine->UpdateSelection(data, bytes, slot);
    rendererMutex.Unlock();
    return result;
}

bool DetachRenderer() {
    if (!rendererMutex.TryLock()) return false;
    const bool result = !engine || engine->Detach();
    // エンジン内の木・文字バッチも、ノードとGPUの参照の排出が済んでから返す。
    if (result) ReleaseEngine();
    rendererMutex.Unlock();
    return result;
}

void WaitMilliseconds(unsigned milliseconds) {
    CTRPluginFramework::Sleep(CTRPluginFramework::Milliseconds(milliseconds));
}
} // namespace gohan::game::lyt

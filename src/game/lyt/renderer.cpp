#include "game/lyt/renderer.hpp"
#include "game/lyt/engine.hpp"
#include "game/lyt/verification.hpp"
#include <CTRPluginFramework.hpp>
#include "core/sync.hpp"

namespace gohan::game::lyt {
namespace {
core::SpinMutex rendererMutex;
detail::Engine &Instance() {
    static detail::Engine engine(detail::GameResourceApi());
    return engine;
}
} // namespace

InstallResult InstallRenderer(const FontResource &font, BackgroundAllocation *background) {
    core::LockGuard guard(rendererMutex);
    if (background) *background = {};
    const auto result = detail::VerifyRenderer();
    if (result != InstallResult::Ready)
        return result;
    const auto installed = Instance().Install(font);
    if (background) *background = Instance().Resources().BackgroundStatus();
    return installed;
}

bool Present(const Drawing &drawing) {
    if (!rendererMutex.TryLock()) return true;
    const bool result = Instance().Present(drawing);
    rendererMutex.Unlock();
    return result;
}
bool UpdateSelection(const std::uint8_t *data, std::size_t bytes, unsigned &slot) {
    if (!rendererMutex.TryLock()) return false;
    const bool result = Instance().UpdateSelection(data, bytes, slot);
    rendererMutex.Unlock();
    return result;
}

bool DetachRenderer() {
    if (!rendererMutex.TryLock()) return false;
    const bool result = Instance().Detach();
    rendererMutex.Unlock();
    return result;
}

void WaitMilliseconds(unsigned milliseconds) {
    CTRPluginFramework::Sleep(CTRPluginFramework::Milliseconds(milliseconds));
}
} // namespace gohan::game::lyt

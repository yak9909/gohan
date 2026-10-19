#include "game/lyt/renderer.hpp"
#include "game/lyt/engine.hpp"
#include "game/lyt/verification.hpp"
#include <CTRPluginFramework.hpp>

namespace gohan::game::lyt {
namespace {
detail::Engine &Instance() {
    static detail::Engine engine(detail::GameResourceApi());
    return engine;
}
} // namespace

InstallResult InstallRenderer(const FontResource &font) {
    const auto result = detail::VerifyRenderer();
    if (result != InstallResult::Ready)
        return result;
    return Instance().Install(font);
}

bool Present(const Drawing &drawing) { return Instance().Present(drawing); }

bool DetachRenderer() { return Instance().Detach(); }

void WaitMilliseconds(unsigned milliseconds) {
    CTRPluginFramework::Sleep(CTRPluginFramework::Milliseconds(milliseconds));
}
} // namespace gohan::game::lyt

#include "game/lyt/renderer.hpp"
#include <CTRPluginFramework.hpp>
#include <cassert>

int main() {
    using namespace gohan::game::lyt;
    using CTRPluginFramework::FakeLayout;
    FontResource font{};
    FakeLayout::title = 0;
    assert(InstallRenderer(font) == InstallResult::WrongTitle && FakeLayout::reads == 0);
    FakeLayout::title = 0x0004000000086200ULL;
    FakeLayout::readable = false;
    assert(InstallRenderer(font) == InstallResult::Unreadable);
    FakeLayout::readable = true;
    FakeLayout::top[0] = 'x';
    assert(InstallRenderer(font) == InstallResult::Mismatch);
    FakeLayout::top[0] = '1';
    FakeLayout::bottom[0] = 'x';
    assert(InstallRenderer(font) == InstallResult::Mismatch);
    FakeLayout::bottom[0] = '1';
    FakeLayout::reads = 0;
    assert(InstallRenderer(font) == InstallResult::MissingBindings && FakeLayout::reads == 18);
    assert(!Present(Drawing{}));
    DetachRenderer();
}

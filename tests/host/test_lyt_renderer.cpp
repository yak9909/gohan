#include "game/lyt/renderer.hpp"
#include <CTRPluginFramework.hpp>
#include <cassert>

int main() {
    using namespace gohan::game::lyt;
    using CTRPluginFramework::FakeLayout;
    FontResource font{};
    FakeLayout::Reset();
    FakeLayout::title = 0;
    assert(InstallRenderer(font) == InstallResult::WrongTitle && FakeLayout::reads == 0);
    FakeLayout::title = 0x0004000000086200ULL;
    FakeLayout::readable = false;
    assert(InstallRenderer(font) == InstallResult::Unreadable);
    FakeLayout::readable = true;
    unsigned total = 0;
    for (std::size_t i = 0; i < FakeLayout::bytes.size(); ++i) {
        const auto &check = detail::RendererChecks[i];
        FakeLayout::Reset();
        FakeLayout::bytes[i][0] ^= 1;
        assert(InstallRenderer(font) == InstallResult::Mismatch);
        assert(FakeLayout::reads == static_cast<int>(total + check.bytes));
        FakeLayout::Reset();
        FakeLayout::unreadableCheck = i;
        assert(InstallRenderer(font) == InstallResult::Unreadable);
        assert(FakeLayout::reads == static_cast<int>(total + 1));
        total += check.bytes;
    }
    FakeLayout::Reset();
    assert(InstallRenderer(font) == InstallResult::MissingBindings);
    assert(FakeLayout::reads == static_cast<int>(total));
    assert(!Present(Drawing{}));
    DetachRenderer();
    DetachRenderer();
}

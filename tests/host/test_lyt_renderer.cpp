// 照合 → 導入 → 描画 → 取り外しの入口（game::lyt の公開関数）を、ゲームの代役で通して試験する。
#include "game/lyt/renderer.hpp"
#include "game/lyt/engine.hpp"
#include "lyt_font.hpp"
#include "lyt_game.hpp"
#include <CTRPluginFramework.hpp>
#include <cassert>
#include <cstdio>

namespace gohan::game::lyt::detail {
const ResourceApi &GameResourceApi() { return fakegame::Api(); }
} // namespace gohan::game::lyt::detail

int main() {
    using namespace gohan::game::lyt;
    using CTRPluginFramework::FakeLayout;
    using Fake = fakegame::Game;
    static fakegame::TestFont font;
    Fake::Reset();
    FakeLayout::Reset();
    FakeLayout::title = 0;
    assert(InstallRenderer(font.resource) == InstallResult::WrongTitle && FakeLayout::reads == 0);
    FakeLayout::title = 0x0004000000086200ULL;
    FakeLayout::readable = false;
    assert(InstallRenderer(font.resource) == InstallResult::Unreadable);
    FakeLayout::readable = true;
    unsigned total = 0;
    for (std::size_t i = 0; i < FakeLayout::bytes.size(); ++i) {
        const auto &check = detail::RendererChecks[i];
        FakeLayout::Reset();
        FakeLayout::bytes[i][0] ^= 1;
        assert(InstallRenderer(font.resource) == InstallResult::Mismatch);
        assert(FakeLayout::reads == static_cast<int>(total + check.bytes));
        FakeLayout::Reset();
        FakeLayout::unreadableCheck = i;
        assert(InstallRenderer(font.resource) == InstallResult::Unreadable);
        assert(FakeLayout::reads == static_cast<int>(total + 1));
        total += check.bytes;
    }
    // 照合の拒否では、ゲームの資源に一切触れない。
    assert(Fake::allocations == 0 && Fake::attempts == 0 && Fake::addCalls == 0 && Fake::batchInits == 0);
    assert(!Present(Drawing{}));
    assert(DetachRenderer() && DetachRenderer());

    // 照合に通っても、前提（管理者・空き）が揃わなければ何も確保せず拒否する。
    FakeLayout::Reset();
    Fake::haveManager = false;
    assert(InstallRenderer(font.resource) == InstallResult::ResourceFailed && Fake::Balanced());
    assert(Fake::allocations == 0 && !Present(Drawing{}));
    Fake::Reset();

    // ゲームのヒープへは字形・画像だけを置き、導入途中の失敗では必ず返す。
    Fake::largestBlock = font.resource.atlasBytes + 127;
    assert(InstallRenderer(font.resource) == InstallResult::ResourceFailed && Fake::Balanced());
    assert(Fake::allocations == 0);
    Fake::Reset();
    Fake::freeBytes = font.resource.atlasBytes + gohan::core::limits::RendererGameAllocationSlack + gohan::core::limits::RendererHeapReserve - 1;
    assert(InstallRenderer(font.resource) == InstallResult::ResourceFailed && Fake::Balanced());
    assert(Fake::allocations == 0);
    Fake::Reset();
    Fake::failAllocate = 1;
    assert(InstallRenderer(font.resource) == InstallResult::ResourceFailed && Fake::Balanced());
    assert(!Present(Drawing{}) && DetachRenderer());
    Fake::Reset();
    for (int fail : {1, 2}) {
        Fake::failCreate = fail;
        assert(InstallRenderer(font.resource) == InstallResult::ResourceFailed && Fake::Balanced());
        Fake::Reset();
    }
    Fake::flushFails = true;
    assert(InstallRenderer(font.resource) == InstallResult::ResourceFailed && Fake::Balanced());
    Fake::Reset();

    // 導入 → 描画 → 取り外し。
    FakeLayout::Reset();
    assert(InstallRenderer(font.resource) == InstallResult::Ready);
    auto memory = ReadRendererMemory();
    assert(memory.pluginBytes >= sizeof(detail::Engine));
    assert(memory.gameBytes == font.resource.atlasBytes && Fake::blocks.size() == 1);
    assert(memory.before.measured && memory.after.measured && memory.current.measured);
    assert(memory.before.freeBytes - memory.after.freeBytes == memory.gameBytes);
    Fake::freeBytes = 607800 + memory.gameBytes;
    Fake::largestBlock = 603712 + memory.gameBytes;
    memory = ReadRendererMemory();
    assert(memory.current.freeBytes == 607800 && memory.current.largestBlock == 603712);
    Drawing drawing;
    drawing.revisions[0] = 1;
    auto &command = drawing.screens[0].commands[0];
    command.kind = Kind::Rectangle;
    command.bounds = {20, 20, 120, 40};
    command.clip = {0, 0, 400, 240};
    command.color = 0xFFFFFFFFu;
    drawing.screens[0].count = drawing.screens[0].rectangles = 1;
    assert(Present(drawing) && Fake::addCalls == 1 && Fake::manager.top.head != nullptr);
    assert(!DetachRenderer());
    Fake::EndPasses();
    int calls = 1;
    while (!DetachRenderer())
        assert(++calls < 20);
    assert(Fake::Balanced() && !Present(drawing));
    memory = ReadRendererMemory();
    assert(memory.gameBytes == 0 && memory.pluginBytes == 0 && memory.commandBytes == 0);
    for (int repeat = 0; repeat < 3; ++repeat) {
        assert(InstallRenderer(font.resource) == InstallResult::Ready);
        assert(Present(drawing));
        assert(!DetachRenderer());
        Fake::EndPasses();
        int drain = 0;
        while (!DetachRenderer()) assert(++drain < 20);
        assert(Fake::Balanced() && !Present(drawing));
    }
    WaitMilliseconds(5);
    assert(FakeLayout::sleeps == 1 && FakeLayout::slept == 5);
    std::puts("lyt renderer: ok");
}

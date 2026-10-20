#include "game/lyt/recording_resources.hpp"
#include "lyt_game.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

namespace {
using namespace gohan::game;
using namespace gohan::game::lyt;
using detail::RecordingResources;
using Fake = fakegame::Game;

constexpr unsigned AtlasBytes = 256;

void Failures(const FontResource &font) {
    RecordingResources resources(fakegame::Api());
    assert(resources.ReleaseIdle());
    assert(!resources.Acquire(FontResource{}));
    Fake::haveAllocator = false;
    assert(!resources.Acquire(font) && resources.Empty());
    Fake::haveAllocator = true;
    // ヒープの空きが足りない時は、何も確保せず拒否する（ゲームは確保の失敗を検査しない）。
    Fake::largestBlock = AtlasBytes + 127;
    assert(!resources.Acquire(font) && resources.Empty() && Fake::allocations == 0);
    Fake::largestBlock = 3000000;
    Fake::freeBytes = AtlasBytes + gohan::core::limits::RendererGameAllocationSlack + gohan::core::limits::RendererHeapReserve - 1;
    assert(!resources.Acquire(font) && resources.Empty() && Fake::allocations == 0);
    Fake::freeBytes = AtlasBytes + gohan::core::limits::RendererGameAllocationSlack + gohan::core::limits::RendererHeapReserve;
    assert(resources.Acquire(font) && Fake::lists.size() == 2);
    assert(resources.ReleaseIdle() && Fake::Balanced());
    Fake::Reset();
    Fake::allocateFails = true;
    assert(!resources.Acquire(font) && resources.Empty() && Fake::allocations == 0);
    Fake::allocateFails = false;
    Fake::flushFails = true;
    assert(!resources.Acquire(font) && resources.Empty());
    assert(Fake::allocations == 1 && Fake::frees == 1 && Fake::attempts == 0);
    Fake::Reset();
    for (int fail : {1, 2}) {
        Fake::failCreate = fail;
        assert(!resources.Acquire(font) && resources.Empty());
        assert(Fake::lists.empty() && Fake::allocations == Fake::frees && Fake::bound == 7);
        assert(Fake::storages == fail - 1);
        assert(Fake::releases == (fail == 1 ? "A" : "LA"));
        assert(resources.ReleaseIdle());
        Fake::Reset();
    }
}

void RepeatedAcquireAndRelease(const FontResource &font) {
    RecordingResources resources(fakegame::Api());
    for (int i = 0; i < 50; ++i) {
        assert(resources.Acquire(font));
        assert(!resources.Empty() && Fake::bound == 7 && Fake::lists.size() == 2);
        assert(std::memcmp(resources.Atlas(), font.atlas, font.atlasBytes) == 0);
        const auto attempts = Fake::attempts;
        assert(!resources.Acquire(font) && Fake::attempts == attempts);
        for (const auto screen : {Screen::Top, Screen::Bottom}) {
            Fake::bound = resources.List(screen);
            assert(!resources.ReleaseIdle());
            assert(!resources.Empty() && Fake::frees == i && Fake::lists.size() == 2);
        }
        Fake::bound = 7;
        assert(resources.ReleaseIdle() && resources.Empty());
        assert(Fake::releases.substr(Fake::releases.size() - 3) == "LLA");
        assert(Fake::lists.empty() && Fake::allocations == Fake::frees);
        assert(resources.ReleaseIdle() && Fake::allocations == Fake::frees);
    }
}

void HeapBudget() {
    namespace limits = gohan::core::limits;
    std::vector<std::uint8_t> atlas(65536), pixels(131072);
    const unsigned sizes[] = {32768, 256, 256, 1024, 1024, 32768, 131072, 32768};
    TextureResource textures[8];
    for (unsigned i = 0; i < 8; ++i) { textures[i].data = pixels.data(); textures[i].bytes = sizes[i]; }
    FontResource font;
    font.atlas = atlas.data(); font.atlasBytes = atlas.size();
    font.textures = textures; font.textureCount = 8; font.requiredTextureCount = 3;
    Fake::Reset(); Fake::freeBytes = 738876; Fake::largestBlock = 603712;
    RecordingResources resources(fakegame::Api());
    assert(resources.Acquire(font) && resources.BackgroundStatus().result == BackgroundResult::Ready);
    const auto &m = resources.MemoryStatus();
    assert(m.gameBytes == 297472 && m.gameUpperBytes == 297984);
    assert(m.commandBytes == 295944);
    assert(m.gameUpperBytes <= limits::RendererGameHeapLimit && Fake::allocateCalls == 2);
    assert(m.before.freeBytes == 738876 && m.after.freeBytes == 441404);
    assert(m.before.freeBytes - m.gameUpperBytes >= limits::RendererHeapReserve);
    assert(resources.ReleaseIdle() && Fake::Balanced());
    // 上限を超える必須画像はゲームの確保関数に触れず拒否する。
    Fake::Reset(); font.atlasBytes = limits::RendererGameHeapLimit;
    assert(!resources.Acquire(font) && resources.Empty() && Fake::allocateCalls == 0);
    // 任意画像だけが上限を超える時は必須画像を保ち、代わりの色にする。
    font.atlasBytes = atlas.size(); textures[7].bytes += 4096;
    assert(resources.Acquire(font) && resources.BackgroundStatus().result == BackgroundResult::InsufficientSpace);
    assert(Fake::allocateCalls == 1 && resources.MemoryStatus().gameUpperBytes <= limits::RendererGameHeapLimit);
    assert(resources.ReleaseIdle() && Fake::Balanced());
}
} // namespace

int main() {
    std::array<std::uint8_t, AtlasBytes> atlas{};
    for (std::size_t i = 0; i < atlas.size(); ++i)
        atlas[i] = static_cast<std::uint8_t>(i);
    FontResource font{};
    font.atlas = atlas.data();
    font.atlasBytes = atlas.size();
    Failures(font);
    RepeatedAcquireAndRelease(font);
    HeapBudget();
    assert(Fake::Balanced());
}

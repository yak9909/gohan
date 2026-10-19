#include "game/lyt/recording_resources.hpp"
#include "lyt_game.hpp"
#include <array>
#include <cassert>
#include <cstring>

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
    Fake::freeBytes = AtlasBytes + 128 + gohan::core::limits::RendererHeapReserve - 1;
    assert(!resources.Acquire(font) && resources.Empty() && Fake::allocations == 0);
    Fake::freeBytes = AtlasBytes + 128 + gohan::core::limits::RendererHeapReserve;
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
    assert(Fake::Balanced());
}

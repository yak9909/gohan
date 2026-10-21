#include "game/lyt/recording_resources.hpp"
#include "lyt_game.hpp"
#include "lyt_font.hpp"
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
    Fake::freeBytes = AtlasBytes + gohan::core::limits::RendererGameAllocationSlack - 1;
    assert(!resources.Acquire(font) && resources.Empty() && Fake::allocations == 0);
    Fake::freeBytes = AtlasBytes + gohan::core::limits::RendererGameAllocationSlack;
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
    Fake::failSpace = 2;
    assert(!resources.Acquire(font) && resources.Empty() && Fake::Balanced());
    Fake::Reset();
    // 記録列の確保元が想定と違って画像用ヒープなら、実差分で常駐上限を守る。
    Fake::commandsInLayoutHeap = true;
    assert(!resources.Acquire(font) && resources.Empty() && Fake::Balanced());
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
    fakegame::UiFont font;
    Fake::Reset(); Fake::freeBytes = 228580; Fake::largestBlock = 200000;
    RecordingResources resources(fakegame::Api());
    assert(resources.Acquire(font.resource));
    auto m = resources.MemoryStatus();
    assert(m.gameBytes == 119296 && m.gameUpperBytes == 120320);
    assert(m.fontBytes == 32768 && m.uiBytes == 33280 && m.windowBytes == 32768 && m.selectionBytes == 20480);
    assert(m.before.freeBytes == 228580 && m.after.freeBytes == 109284);
    assert(m.before.freeBytes - m.gameUpperBytes == 108260);
    assert(m.commandBytes == 295944 && Fake::allocateCalls == 1);
    const auto *data = static_cast<const std::uint8_t *>(resources.Atlas());
    for (unsigned i = 0; i < font.resource.textureCount; ++i)
        if (font.textures[i].gpuResident) {
            const auto offset = TextureOffset(font.resource, i);
            assert(offset % 128 == 0 && offset + font.textures[i].bytes <= m.gameBytes);
            assert(std::memcmp(data + offset, font.textures[i].data, font.textures[i].bytes) == 0);
        }
    assert(resources.ReleaseIdle() && Fake::Balanced());
    // 全量を導入時に検査する。大き過ぎる帯だけを省いて成功にしない。
    Fake::Reset(); font.textures.back().bytes += 2048;
    assert(!resources.Acquire(font.resource) && resources.Empty() && Fake::allocateCalls == 0);
    font.textures.back().bytes -= 2048;
    // 管理費・整列込み上限の境界と、見積もりを超えた実差分を検査する。
    for (unsigned overhead : {1024u, 1025u}) {
        Fake::Reset(); Fake::allocationOverhead = overhead;
        const bool ready = resources.Acquire(font.resource);
        assert(ready == (overhead == 1024));
        if (ready) assert(resources.MemoryStatus().gameUpperBytes == limits::RendererResidentHeapLimit);
        assert(resources.ReleaseIdle() && Fake::Balanced());
    }
    Fake::Reset(); Fake::freeBytes = 120319;
    assert(!resources.Acquire(font.resource) && Fake::allocateCalls == 0);
    Fake::Reset(); Fake::largestBlock = 119296 + 127;
    assert(!resources.Acquire(font.resource) && Fake::allocateCalls == 0);

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

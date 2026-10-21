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
    std::vector<std::uint8_t> atlas(65536), pixels(131072);
    const unsigned sizes[] = {32768, 256, 256, 1024, 1024, 32768, 131072, 32768};
    TextureResource textures[8];
    for (unsigned i = 0; i < 8; ++i) { textures[i].data = pixels.data(); textures[i].bytes = sizes[i]; }
    textures[3].gpuResident = textures[4].gpuResident = false;
    FontResource font;
    font.atlas = atlas.data(); font.atlasBytes = atlas.size();
    font.textures = textures; font.textureCount = 8; font.requiredTextureCount = 3;
    Fake::Reset(); Fake::freeBytes = 738876; Fake::largestBlock = 603712;
    RecordingResources resources(fakegame::Api());
    assert(resources.Acquire(font) && resources.BackgroundStatus().result == BackgroundResult::NotAttempted);
    auto m = resources.MemoryStatus();
    assert(m.residentBytes == 98816 && m.residentUpperBytes == 99072);
    assert(m.gameUpperBytes <= limits::RendererResidentHeapLimit && Fake::allocateCalls == 1);
    assert(m.openBytes == 0 && m.before.freeBytes == 738876 && m.after.freeBytes == 640060);
    assert(resources.AcquireBackground(font) && resources.BackgroundStatus().result == BackgroundResult::Ready);
    m = resources.MemoryStatus();
    assert(m.gameBytes == 295424 && m.gameUpperBytes == 295936);
    assert(m.commandBytes == 2 * (68 + limits::RendererRecordBytes + 28 * limits::RendererQueueEntries));
    assert(m.gameUpperBytes <= limits::RendererGameHeapLimit && Fake::allocateCalls == 2);
    assert(m.openBytes == 196608 && m.openUpperBytes == 196864 && m.current.freeBytes == 443452);
    assert(m.before.freeBytes - m.gameUpperBytes >= limits::RendererHeapReserve);
    assert(resources.ReleaseIdle() && Fake::Balanced());
    // 上限を超える必須画像はゲームの確保関数に触れず拒否する。
    Fake::Reset(); font.atlasBytes = limits::RendererGameHeapLimit;
    assert(!resources.Acquire(font) && resources.Empty() && Fake::allocateCalls == 0);
    // 任意画像だけが上限を超える時は必須画像を保ち、代わりの色にする。
    font.atlasBytes = atlas.size(); textures[7].bytes += 4096;
    assert(resources.Acquire(font) && !resources.AcquireBackground(font) && resources.BackgroundStatus().result == BackgroundResult::BudgetExceeded);
    assert(Fake::allocateCalls == 1 && resources.MemoryStatus().gameUpperBytes <= limits::RendererGameHeapLimit);
    assert(resources.ReleaseIdle() && Fake::Balanced());
    // 予算の境界は常駐120,000バイト。総予算300,000以下でも常駐を増やせない。
    Fake::Reset(); font.atlas = pixels.data();
    font.atlasBytes = limits::RendererResidentHeapLimit - limits::RendererGameAllocationSlack;
    font.textureCount = 0;
    assert(resources.Acquire(font));
    assert(resources.MemoryStatus().residentUpperBytes == limits::RendererResidentHeapLimit);
    assert(resources.ReleaseIdle() && Fake::Balanced());
    ++font.atlasBytes;
    assert(!resources.Acquire(font) && Fake::allocateCalls == 1);
    // 管理費が見積もりを上回り、確保後に予備を割った時も背景だけを戻す。
    Fake::Reset();
    font.atlas = atlas.data(); font.atlasBytes = atlas.size(); font.textureCount = 8;
    textures[7].bytes = 32768;
    Fake::allocationOverhead = 512;
    Fake::freeBytes = 98816 + 512 + 196608 + limits::RendererGameAllocationSlack + limits::RendererHeapReserve;
    assert(resources.Acquire(font));
    assert(resources.MemoryStatus().residentUpperBytes == 98816 + 512);
    assert(!resources.AcquireBackground(font) && !resources.Background());
    assert(resources.BackgroundStatus().result == BackgroundResult::InsufficientSpace);
    assert(resources.MemoryStatus().openUpperBytes == 0 && Fake::blocks.size() == 1);
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

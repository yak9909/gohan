#include "game/lyt/recording_resources.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <set>
#include <string>

namespace {
using namespace gohan::game;
using namespace gohan::game::lyt;
using detail::RecordingResources;

struct Fake {
    static inline generated::SsysMaAllocator allocator{};
    alignas(128) static inline std::array<std::uint8_t, 256> memory{};
    static inline bool haveAllocator = true, allocateFails = false, flushFails = false;
    static inline unsigned bound = 7, next = 20;
    static inline int allocations = 0, frees = 0, attempts = 0, failCreate = 0, storages = 0;
    static inline std::set<unsigned> lists;
    static inline std::string releases;

    static void Reset() {
        assert(lists.empty() && allocations == frees);
        haveAllocator = true;
        allocateFails = flushFails = false;
        bound = 7;
        next = 20;
        allocations = frees = attempts = failCreate = storages = 0;
        releases.clear();
    }
    static generated::SsysMaAllocator *Allocator() { return haveAllocator ? &allocator : nullptr; }
    static void *Allocate(generated::SsysMaAllocator *owner, unsigned bytes, int alignment) {
        assert(owner == &allocator && bytes <= memory.size() && alignment == 128);
        if (allocateFails)
            return nullptr;
        ++allocations;
        return memory.data();
    }
    static void Free(generated::SsysMaAllocator *owner, void *data) {
        assert(owner == &allocator && data == memory.data() && lists.empty());
        assert(allocations == frees + 1);
        ++frees;
        releases += 'A';
    }
    static void Property(unsigned property, unsigned *value) {
        assert(property == 519);
        *value = bound;
    }
    static void Create(int count, unsigned *id) {
        assert(count == 1 && *id == 0);
        if (++attempts == failCreate)
            return;
        *id = next++;
        assert(lists.insert(*id).second);
    }
    static void Select(unsigned id) {
        assert(id == 7 || lists.contains(id));
        bound = id;
    }
    static void Storage(unsigned bytes, int queue) {
        assert(lists.contains(bound));
        assert(bytes == gohan::core::limits::RendererRecordBytes && queue == 16);
        ++storages;
    }
    static void Destroy(int count, const unsigned *id) {
        assert(count == 1 && bound != *id && lists.erase(*id) == 1);
        releases += 'L';
    }
    static bool Flush(void *data, std::size_t size) {
        assert(data == memory.data() && size == memory.size());
        return !flushFails;
    }
};

const detail::ResourceApi api{Fake::Allocator, Fake::Allocate, Fake::Free, Fake::Property,
                            Fake::Create, Fake::Select, Fake::Storage, Fake::Destroy, Fake::Flush};

void Failures(const FontResource &font) {
    RecordingResources resources(api);
    assert(resources.ReleaseIdle());
    assert(!resources.Acquire(FontResource{}));
    Fake::haveAllocator = false;
    assert(!resources.Acquire(font) && resources.Empty());
    Fake::haveAllocator = true;
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
    RecordingResources resources(api);
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
    std::array<std::uint8_t, 256> atlas{};
    for (std::size_t i = 0; i < atlas.size(); ++i)
        atlas[i] = static_cast<std::uint8_t>(i);
    FontResource font{};
    font.atlas = atlas.data();
    font.atlasBytes = atlas.size();
    Failures(font);
    RepeatedAcquireAndRelease(font);
}

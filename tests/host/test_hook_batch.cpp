#include "game/hook_batch.hpp"
#include <cassert>
#include <map>
#include <vector>

using namespace gohan::game;
namespace {
std::map<std::uintptr_t, std::uint32_t> words;
std::vector<unsigned> installed, removed;
bool paused = false, supported = true;
unsigned fail = 99, pauses = 0, resumes = 0;
bool Supported() { return supported; }
bool Pause() { const bool own = !paused; paused = true; if (own) ++pauses; return own; }
void Resume(bool own) { if (own) { paused = false; ++resumes; } }
bool Read(std::uintptr_t address, std::uint8_t &byte) {
    assert(paused);
    auto it = words.find(address & ~std::uintptr_t(3));
    if (it == words.end()) return false;
    byte = (it->second >> (8 * (address & 3))) & 255; return true;
}
bool Install(void *, unsigned i, const HookSpec &) { assert(paused); if (i == fail) return false; installed.push_back(i); return true; }
void Remove(void *, unsigned i) { assert(paused); removed.push_back(i); }
const HookBackend backend{Supported, Pause, Resume, Read, Install, Remove};
bool Word(std::uintptr_t address, std::uint32_t &word) { if (!words.contains(address)) return false; word = words[address]; return true; }
bool Write(std::uintptr_t a, std::uint32_t w) { words[a] = w; return true; }
void Sync(std::uintptr_t, std::size_t) {}
}
int main() {
    const HookSpec specs[] = {{{4, 4, WordCrc32(0xE92D4010u)}, 128}, {{8, 4, WordCrc32(0xE1A00000u)}, 132, HookKind::Mitm}, {{12, 4, WordCrc32(0xE92D4070u)}, 136}};
    words = {{4, 0xE92D4010u}, {8, 0xE1A00000u}, {12, 0xE92D4070u}};
    for (unsigned failure = 0; failure < 3; ++failure) {
        fail = failure; installed.clear(); removed.clear();
        HookBatch batch(backend, specs);
        assert(batch.Wire() == Result::Failed);
        assert(installed.size() == failure && removed.size() == failure);
        for (unsigned i = 0; i < failure; ++i) assert(removed[i] == failure - 1 - i);
        batch.Shutdown(); assert(removed.size() == failure && !paused);
    }
    fail = 99; installed.clear(); removed.clear();
    {
        const HookSpec repeated[] = {specs[0], specs[0]};
        HookBatch batch(backend, repeated);
        assert(batch.Wire() == Result::Unsupported && installed.empty());
    }
    {
        HookBatch batch(backend, specs), duplicate(backend, specs);
        assert(batch.Wire() == Result::Ok && batch.Wire() == Result::Ok);
        assert(installed.size() == 3 && duplicate.Wire() == Result::Unsupported);
        const PatchWord patch_words[] = {{8, 0, WordCrc32(words[8])}};
        const PatchMemory memory{Word, Write, Sync};
        PatchSet patch(memory, patch_words);
        assert(patch.Wire() == Result::Unsupported);
        batch.Shutdown(); assert((removed == std::vector<unsigned>{2, 1, 0}));
        assert(patch.Wire() == Result::Ok && duplicate.Wire() == Result::Unsupported);
        patch.Shutdown(); assert(duplicate.Wire() == Result::Ok);
    }
    supported = false;
    HookBatch wrong(backend, specs);
    const auto before = pauses;
    assert(wrong.Wire() == Result::Unsupported && pauses == before);
    supported = true;
    for (unsigned bad = 0; bad < 2; ++bad) {
        const auto original = words[8];
        if (!bad) words.erase(8); else words[8] ^= 1;
        HookBatch batch(backend, specs); installed.clear();
        assert(batch.Wire() == Result::Unsupported && installed.empty());
        words[8] = original;
    }
    const std::uint32_t pc_words[] = {0xE59F0004u, 0x159F0004u, 0xE24F0008u, 0xEA000000u, 0xE1A0000Fu, 0xED9F0A00u};
    for (auto pc : pc_words) for (auto kind : {HookKind::Hook, HookKind::Mitm}) {
        words[4] = pc;
        const HookSpec spec{{4, 4, WordCrc32(pc)}, 128, kind};
        HookBatch batch(backend, {&spec, 1});
        assert(batch.Wire() == Result::Unsupported);
    }
    assert(pauses == resumes);
}

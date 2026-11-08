#include "game/patch_set.hpp"
#include "game/weather.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>

using namespace gohan::game;
namespace {
std::map<std::uintptr_t, std::uint32_t> memory;
std::vector<std::uintptr_t> writes, syncs;
std::uintptr_t fail_write = 0;
bool Read(std::uintptr_t address, std::uint32_t &word) {
    auto it = memory.find(address);
    if (it == memory.end()) return false;
    word = it->second;
    return true;
}
bool Write(std::uintptr_t address, std::uint32_t word) {
    if (address == fail_write) return false;
    memory[address] = word;
    writes.push_back(address);
    return true;
}
void Sync(std::uintptr_t address, std::size_t bytes) {
    assert(bytes == 4 && writes.back() == address);
    syncs.push_back(address);
}
const PatchMemory access{Read, Write, Sync};
void Reset() {
    memory = {{4, 11}, {8, 22}, {12, 33}, {64, 0}, {68, 0}, {80, 0}};
    writes.clear(); syncs.clear(); fail_write = 0;
}
} // namespace

int main() {
    assert(WordCrc32(0) == 0x2144DF1Cu && WordCrc32(0x12345678u) == 0xAF6D87D2u);
    // 補助語、補助語、入口。OFF は入口から戻す。
    const PatchWord words[] = {{4, 111, WordCrc32(11)}, {8, 222, WordCrc32(22)}, {12, 333, WordCrc32(33)}};
    const std::uint32_t code[] = {77, 88}, other[] = {99};
    const CaveWords caves[] = {{64, code}, {80, other}};
    Reset();
    PatchSet patch(access, words, caves);
    assert(patch.Set(true) == Result::Unsupported);
    assert(patch.Wire() == Result::Ok);
    assert((writes == std::vector<std::uintptr_t>{64, 68, 80}));
    assert(!patch.IsApplied());
    writes.clear(); syncs.clear();
    assert(patch.Set(true) == Result::Ok && patch.IsApplied());
    assert((writes == std::vector<std::uintptr_t>{4, 8, 12}) && writes == syncs);
    memory[8] = 0;
    assert(!patch.IsApplied());
    assert(patch.Set(true) == Result::Ok && patch.IsApplied());
    writes.clear(); syncs.clear();
    assert(patch.Set(false) == Result::Ok && !patch.IsApplied());
    assert((writes == std::vector<std::uintptr_t>{12, 8, 4}) && writes == syncs);
    assert(memory[4] == 11 && memory[8] == 22 && memory[12] == 33);
    assert(memory[64] == 77 && memory[68] == 88 && memory[80] == 99);
    assert(patch.Set(true) == Result::Ok);
    patch.Shutdown();
    assert(memory[4] == 11 && memory[12] == 33 && memory[64] == 77);
    memory[4] = 111; memory[8] = 222; memory[12] = 333;
    assert(patch.IsApplied() && patch.Set(false) == Result::Ok);
    assert(memory[4] == 11 && memory[12] == 33);

    // 不一致・読み取り不能・後半のケーブだけ使用済みでも全て無書き込み。
    for (int failure = 0; failure < 4; ++failure) {
        Reset();
        if (failure == 0) memory[8] ^= 1;
        if (failure == 1) memory.erase(12);
        if (failure == 2) memory[80] = 1;
        if (failure == 3) memory.erase(68);
        PatchSet bad(access, words, caves);
        assert(bad.Wire() == Result::Unsupported);
        assert(bad.Set(true) == Result::Unsupported && !bad.IsApplied());
        assert(writes.empty() && syncs.empty());
        bad.Shutdown();
        assert(writes.empty());
    }
    Reset();
    PatchSet failing(access, words);
    assert(failing.Wire() == Result::Ok);
    fail_write = 8;
    assert(failing.Set(true) == Result::Failed);
    assert(memory[4] == 11 && memory[12] == 33);
    fail_write = 0;
    failing.Shutdown();
    assert(memory[8] == 22);
    Reset();
    PatchSet cave_failure(access, words, caves);
    fail_write = 68;
    assert(cave_failure.Wire() == Result::Failed);
    assert(cave_failure.Set(true) == Result::Unsupported && memory[12] == 33);

    Reset();
    std::array<PatchWord, 9> nine{};
    for (std::size_t i = 0; i < nine.size(); ++i) {
        memory[128 + i * 4] = static_cast<std::uint32_t>(i);
        nine[i] = {128 + i * 4, static_cast<std::uint32_t>(i + 100), WordCrc32(i)};
    }
    PatchSet all_nine(access, nine);
    assert(all_nine.Wire() == Result::Ok && all_nine.Set(true) == Result::Ok && all_nine.IsApplied());
    assert(memory[128 + 8 * 4] == 108);
    all_nine.Shutdown();
    assert(memory[128 + 8 * 4] == 8);

    // 天気はその場の命令から逆算し、未知の命令を上書きしない。
    Reset();
    const PatchWord weather_word[] = {{4, 0xE3A00000u, WordCrc32(11)}};
    PatchSet weather_patch(access, weather_word);
    Weather weather(weather_patch);
    std::int32_t value = -1;
    assert(!weather.Read(&value) && weather.Write(1) == Result::Unsupported);
    assert(weather_patch.Wire() == Result::Ok);
    for (int i = 0; i < 8; ++i) {
        assert(weather.Write(i) == Result::Ok && weather.Read(&value) && value == i);
        assert(memory[4] == Weather::Encode(i, 11));
    }
    assert(weather.Write(-1) == Result::Failed && weather.Write(8) == Result::Failed);
    memory[4] = 0xE3A00007u;
    writes.clear();
    assert(!weather.Read(&value) && weather.Write(2) == Result::Unsupported && writes.empty());
    memory[4] = 0xE3A01000u;
    assert(!weather.Read(&value));
    weather_patch.Shutdown();
    assert(memory[4] == 11);
    std::puts("パッチ・ケーブの照合、書く順序、復元、天気: 成功");
}

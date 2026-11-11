#include "game/hidden_icons.hpp"
#include "game/lyt/icon_cache.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using namespace gohan::game;
using namespace gohan::game::items;
using Bytes = std::vector<std::uint8_t>;
void Put(Bytes &b, std::size_t at, std::uint32_t value, unsigned bytes = 4) {
    for (unsigned i = 0; i < bytes; ++i)
        b.at(at + i) = value >> (8 * i);
}
Bytes Bclim(unsigned color) {
    Bytes b(1064, color);
    const unsigned f = 1024;
    std::memcpy(b.data() + f, "CLIM", 4);
    Put(b, f + 4, 0xFEFF, 2);
    Put(b, f + 6, 20, 2);
    Put(b, f + 8, 0x02020000);
    Put(b, f + 12, 1064);
    Put(b, f + 16, 1);
    std::memcpy(b.data() + f + 20, "imag", 4);
    Put(b, f + 24, 16);
    Put(b, f + 28, 32, 2);
    Put(b, f + 30, 32, 2);
    Put(b, f + 32, 11);
    Put(b, f + 36, 1024);
    return b;
}
Bytes Arc(const std::vector<std::pair<std::string, Bytes>> &files) {
    const auto count = files.size() + 2;
    Bytes names;
    std::vector<unsigned> offsets;
    std::vector<std::string> labels{"", "timg"};
    for (const auto &file : files)
        labels.push_back(file.first);
    for (const auto &name : labels) {
        offsets.push_back(names.size());
        for (char c : name) {
            names.push_back(c);
            names.push_back(0);
        }
        names.push_back(0);
        names.push_back(0);
    }
    const auto length = count * 12 + names.size();
    Bytes b((28 + length + 31) / 32 * 32);
    std::memcpy(b.data(), "darc", 4);
    Put(b, 4, 0xFEFF, 2);
    Put(b, 6, 28, 2);
    Put(b, 8, 0x01000000);
    Put(b, 16, 28);
    Put(b, 20, length);
    Put(b, 24, b.size());
    Put(b, 28, 0x01000000);
    Put(b, 36, count);
    Put(b, 40, 0x01000000 | offsets[1]);
    Put(b, 48, count);
    std::memcpy(b.data() + 28 + 12 * count, names.data(), names.size());
    for (unsigned i = 0; i < files.size(); ++i) {
        const auto at = (b.size() + 127) / 128 * 128;
        b.resize(at);
        Put(b, 52 + i * 12, offsets[i + 2]);
        Put(b, 56 + i * 12, at);
        Put(b, 60 + i * 12, files[i].second.size());
        b.insert(b.end(), files[i].second.begin(), files[i].second.end());
    }
    Put(b, 12, b.size());
    return b;
}
void Archives() {
    HiddenIconArchive archive;
    const auto good =
        Arc({{"5353.bclim", Bclim(99)}, {"12.bclim", Bclim(12)}, {"300.bclim", Bclim(30)}, {"7.bclim", Bclim(7)}});
    assert(archive.Parse(good.data(), good.size()) && archive.Count() == 4);
    for (const auto &[number, color] :
         std::vector<std::pair<unsigned, unsigned>>{{7, 7}, {12, 12}, {300, 30}, {5353, 99}})
        assert(archive.Find(number) && *archive.Find(number) == color);
    assert(!archive.Find(0) && !archive.Find(11) && !archive.Find(65535));
    for (unsigned n = 0; n < 28; ++n)
        assert(!archive.Parse(good.data(), n) && !archive.Count());
    assert(!archive.Parse(nullptr, 100));
    for (const auto &[at, value] : std::vector<std::pair<unsigned, unsigned>>{{0, 0},
                                                                              {4, 0},
                                                                              {6, 0},
                                                                              {8, 0},
                                                                              {12, 100},
                                                                              {16, 0xFFFFFFFFu},
                                                                              {20, 0xFFFFFFFFu},
                                                                              {24, 0},
                                                                              {28, 0},
                                                                              {32, 1},
                                                                              {36, 0},
                                                                              {36, 0xFFFFFFFFu},
                                                                              {44, 1},
                                                                              {48, 1000},
                                                                              {52, 0x00FFFFFFu},
                                                                              {56, 0xFFFFFFF0u},
                                                                              {60, 0xFFFFFFFFu}}) {
        auto bad = good;
        Put(bad, at, value);
        assert(!archive.Parse(bad.data(), bad.size()) && !archive.Count());
    }
    auto duplicate = Arc({{"1.bclim", Bclim(1)}, {"01.bclim", Bclim(2)}});
    assert(!archive.Parse(duplicate.data(), duplicate.size()) && !archive.Count());
    auto wrong = Bclim(0);
    for (const auto &[at, value] :
         std::vector<std::pair<unsigned, unsigned>>{{1024, 0}, {1052, 64}, {1054, 64}, {1056, 10}, {1060, 1023}}) {
        wrong = Bclim(0);
        Put(wrong, at, value, at == 1052 || at == 1054 ? 2 : 4);
        auto b = Arc({{"1.bclim", wrong}, {"2.bclim", Bclim(2)}});
        assert(archive.Parse(b.data(), b.size()) && archive.Count() == 1 && !archive.Find(1) && archive.Find(2));
    }
    auto ignored =
        Arc({{"x.bclim", Bclim(1)}, {"65536.bclim", Bclim(2)}, {"1.png", Bclim(3)}, {"4.bclim", Bytes(1063)}});
    assert(archive.Parse(ignored.data(), ignored.size()) && !archive.Count());
    auto other = good;
    const auto names = 28 + 6 * 12;
    other[names + 2] = 'x';
    assert(archive.Parse(other.data(), other.size()) && !archive.Count());
    auto unterminated = good;
    // 最後の名前の終端を表の終わりまで埋める。
    const auto tableLength = unsigned(good[20]) | unsigned(good[21]) << 8;
    for (unsigned i = 0; i < 2; ++i)
        unterminated[28 + tableLength - 2 + i] = 'x';
    assert(!archive.Parse(unterminated.data(), unterminated.size()));
}
bool flushOk = true;
unsigned flushed = 0;
bool Flush(void *, std::size_t bytes) {
    assert(bytes == 1024);
    ++flushed;
    return flushOk;
}
std::uint32_t Physical(const void *p) { return 4096 + (reinterpret_cast<std::uintptr_t>(p) & 65535); }
void Cache() {
    lyt::IconCache cache;
    alignas(128) std::array<std::uint8_t, 32768 + 256> storage{};
    std::array<std::uint8_t, 1024> pixels{};
    std::array<unsigned, 129> widgets{};
    assert(!cache.Place(&widgets[0], 1, pixels.data()));
    assert(!cache.Bind(storage.data(), 32767, Flush, Physical));
    assert(cache.Bind(storage.data(), storage.size() - 1, Flush, Physical));
    assert(cache.Data() == storage.data() + 128); // 末尾は切り下げ。
    const auto first = cache.Place(&widgets[0], 1, pixels.data());
    assert(first && flushed == 1 && cache.References(0) == 1);
    assert(cache.Place(&widgets[1], 1, pixels.data()) == first && flushed == 1 && cache.References(0) == 2);
    cache.Release(&widgets[0]);
    cache.Release(&widgets[0]);
    cache.Release(nullptr);
    assert(cache.References(0) == 1);
    pixels.fill(9);
    assert(cache.Place(&widgets[0], 2, pixels.data()) != first);
    assert(*static_cast<const std::uint8_t *>(cache.Data()) == 0 && cache.References(1) == 1);
    for (unsigned i = 2; i < 32; ++i)
        assert(cache.Place(&widgets[i], i + 1, pixels.data()));
    assert(!cache.Place(&widgets[32], 33, pixels.data()));
    assert(cache.References(0) == 1 && cache.References(31) == 1);
    cache.Release(&widgets[1]);
    assert(cache.Place(&widgets[32], 33, pixels.data()) == first);
    const auto generation = cache.Generation();
    assert(cache.Bind(storage.data(), storage.size(), Flush, Physical));
    assert(cache.Generation() != generation);
    for (unsigned i = 0; i < 32; ++i)
        assert(!cache.References(i));
    for (unsigned i = 0; i < 128; ++i)
        assert(cache.Place(&widgets[i], 1, pixels.data()));
    assert(cache.References(0) == 128 && !cache.Place(&widgets[128], 1, pixels.data()));
    cache.Release(&widgets[77]);
    assert(cache.References(0) == 127);
    assert(cache.Place(&widgets[128], 1, pixels.data()));
    cache.Unbind();
    assert(!cache.Data() && !cache.Place(&widgets[0], 1, pixels.data()));
    assert(cache.Bind(storage.data(), storage.size(), Flush, Physical));
    flushOk = false;
    assert(!cache.Place(&widgets[0], 1, pixels.data()) && !cache.References(0));
    flushOk = true;
    assert(cache.Place(&widgets[0], 1, pixels.data()) && cache.References(0) == 1);
}
generated::NwLytTexMap *Update(generated::NwLytTexMap *map) {
    assert(map->texture_address == 4096 && map->sampler_bits == 0xABCD0B45u);
    assert(map->image_width == 32 && map->image_height == 32 && map->texture_width == 32 && map->texture_height == 32);
    map->gpu_format = 11;
    return map;
}
void Rules() {
    auto b = Arc({{"9.bclim", Bclim(9)}});
    HiddenIconArchive archive;
    assert(archive.Parse(b.data(), b.size()));
    HiddenEntry entry{1, 9, nullptr, nullptr, nullptr};
    assert(SelectHiddenIcon(true, 0, 1, &entry, archive));
    assert(!SelectHiddenIcon(false, 0, 1, &entry, archive));
    assert(!SelectHiddenIcon(true, 1, 1, &entry, archive));
    assert(!SelectHiddenIcon(true, 0, 1, nullptr, archive));
    assert(!SelectHiddenIcon(true, 0, 0xFFFE, &entry, archive));
    entry.icon = 0;
    assert(!SelectHiddenIcon(true, 0, 1, &entry, archive));
    entry.icon = 8;
    assert(!SelectHiddenIcon(true, 0, 1, &entry, archive));
    generated::NwLytTexMap map{};
    generated::NwLytMaterial material{};
    assert(!WriteHiddenIcon(material, 4096, Update));
    material.variable_data = &map;
    material.flags = 255;
    map.sampler_bits = 0xABCD0545u;
    std::memset(map.unk_0, 255, 4);
    assert(!WriteHiddenIcon(material, 0, Update) && !WriteHiddenIcon(material, 4096, nullptr));
    assert(WriteHiddenIcon(material, 4096, Update));
    assert(material.flags == 251 && map.gpu_format == 11);
    unsigned words[5];
    std::memcpy(words, &map, sizeof(words));
    assert(words[0] == 0 && words[1] == 4096 && words[2] == (32u | 32u << 16) && words[3] == words[2] &&
           words[4] == 0xABCD0B45u);
}
} // namespace
int main(int argc, char **argv) {
    Archives();
    Cache();
    Rules();
    if (argc > 1) {
        std::ifstream file(argv[1], std::ios::binary);
        assert(file);
        const Bytes bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        HiddenIconArchive archive;
        assert(archive.Parse(bytes.data(), bytes.size()));
        std::printf("SD アイコン: %zu 枚、%zu B、読み取り成功\n", archive.Count(), bytes.size());
    }
}

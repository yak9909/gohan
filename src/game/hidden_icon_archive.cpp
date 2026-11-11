#include "game/hidden_icon_archive.hpp"
#include <algorithm>
#include <cstring>
#include <string_view>

namespace gohan::game::items {
namespace {
bool Fits(std::size_t offset, std::size_t bytes, std::size_t size) { return offset <= size && bytes <= size - offset; }
unsigned Le16(const std::uint8_t *p) { return unsigned(p[0]) | unsigned(p[1]) << 8; }
std::uint32_t Le32(const std::uint8_t *p) { return Le16(p) | std::uint32_t(Le16(p + 2)) << 16; }
bool Equal(const std::uint8_t *name, std::size_t units, std::string_view text) {
    if (units != text.size())
        return false;
    for (std::size_t i = 0; i < units; ++i)
        if (Le16(name + 2 * i) != unsigned(text[i]))
            return false;
    return true;
}
int Number(const std::uint8_t *name, std::size_t units) {
    if (units < 7 || units > 11 || !Equal(name + (units - 6) * 2, 6, ".bclim"))
        return -1;
    unsigned value = 0;
    for (std::size_t i = 0; i < units - 6; ++i) {
        const auto c = Le16(name + 2 * i);
        if (c < '0' || c > '9')
            return -1;
        value = value * 10 + c - '0';
    }
    return value <= 0xFFFF ? static_cast<int>(value) : -1;
}
bool Bclim(const std::uint8_t *data, std::size_t size) {
    if (size != 1064)
        return false;
    const auto *f = data + 1024;
    return !std::memcmp(f, "CLIM", 4) && Le16(f + 4) == 0xFEFF && Le16(f + 6) == 0x14 && Le32(f + 8) == 0x02020000 &&
           Le32(f + 12) == size && Le32(f + 16) == 1 && !std::memcmp(f + 20, "imag", 4) && Le32(f + 24) == 0x10 &&
           Le16(f + 28) == 32 && Le16(f + 30) == 32 && Le32(f + 32) == 11 && Le32(f + 36) == 1024;
}
} // namespace
bool HiddenIconArchive::Parse(const std::uint8_t *data, std::size_t size) {
    count_ = 0;
    if (Read(data, size))
        return true;
    count_ = 0;
    return false;
}
bool HiddenIconArchive::Read(const std::uint8_t *data, std::size_t size) {
    if (!data || size < 28 || size > Limit || std::memcmp(data, "darc", 4) || Le16(data + 4) != 0xFEFF ||
        Le16(data + 6) != 28 || Le32(data + 8) != 0x01000000 || Le32(data + 12) != size)
        return false;
    const auto table = Le32(data + 16), length = Le32(data + 20), payload = Le32(data + 24);
    if (table < 28 || (table & 3) || !Fits(table, length, size) || length < 12 || payload < table + length ||
        payload > size)
        return false;
    const auto *entries = data + table;
    const auto count = Le32(entries + 8);
    if (!count || count > length / 12 || Le32(entries) != 0x01000000 || Le32(entries + 4) != 0)
        return false;
    const auto *names = entries + 12 * count;
    const auto namesBytes = length - 12 * count;
    struct Directory {
        unsigned index, end, depth;
        bool timg;
    };
    Directory stack[32]{};
    unsigned used = 0;
    for (unsigned i = 0; i < count; ++i) {
        while (used && i >= stack[used - 1].end)
            --used;
        const auto *entry = entries + 12 * i;
        const auto raw = Le32(entry), off = Le32(entry + 4), len = Le32(entry + 8);
        if (raw & 0xFE000000u)
            return false;
        const auto name = raw & 0x00FFFFFFu;
        if ((name & 1) || !Fits(name, 2, namesBytes))
            return false;
        std::size_t units = 0;
        while (Fits(name + units * 2, 2, namesBytes) && Le16(names + name + units * 2))
            ++units;
        if (!Fits(name + units * 2, 2, namesBytes))
            return false;
        const auto *text = names + name;
        if (i == 0 && units != 0)
            return false;
        if (raw & 0x01000000u) {
            if (used == std::size(stack) || len <= i || len > count ||
                (used && (off != stack[used - 1].index || len > stack[used - 1].end)))
                return false;
            const bool neutral = Equal(text, units, "") || Equal(text, units, ".");
            const auto parentDepth = used ? stack[used - 1].depth : 0;
            const bool timg =
                neutral ? (used ? stack[used - 1].timg : false) : parentDepth == 0 && Equal(text, units, "timg");
            stack[used++] = {i, len, parentDepth + (neutral ? 0u : 1u), timg};
        } else {
            if (!used || off < payload || !Fits(off, len, size))
                return false;
            const auto &parent = stack[used - 1];
            const int number = parent.depth == 1 && parent.timg ? Number(text, units) : -1;
            if (number < 0 || !Bclim(data + off, len))
                continue;
            if (count_ == MaxImages)
                return false;
            images_[count_++] = {static_cast<std::uint16_t>(number), data + off};
        }
    }
    std::sort(images_, images_ + count_, [](const Image &a, const Image &b) { return a.number < b.number; });
    for (std::size_t i = 1; i < count_; ++i)
        if (images_[i - 1].number == images_[i].number)
            return false;
    return true;
}
const std::uint8_t *HiddenIconArchive::Find(std::uint16_t number) const {
    std::size_t lo = 0, hi = count_;
    while (lo < hi) {
        const auto mid = lo + (hi - lo) / 2;
        if (images_[mid].number == number)
            return images_[mid].data;
        if (images_[mid].number < number)
            lo = mid + 1;
        else
            hi = mid;
    }
    return nullptr;
}
} // namespace gohan::game::items

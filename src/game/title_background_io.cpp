#include "game/title_background.hpp"
#include "ui_values.hpp"
#include <cstring>
#include <limits>

namespace gohan::game::titlebg {
namespace {
std::uint32_t Word(const std::uint8_t *p) {
    return p[0] | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
unsigned Half(const std::uint8_t *p) { return p[0] | unsigned(p[1]) << 8; }
using romfs::Fits;
bool Name(const Reader &r, std::uint64_t offset, std::uint64_t capacity, const char *name) {
    const auto length = std::strlen(name);
    if (!Fits(offset, length * 2 + 2, capacity) || length > 95) return false;
    std::uint8_t text[192];
    if (!r.Read(offset, text, length * 2 + 2)) return false;
    for (std::size_t i = 0; i < length; ++i)
        if (text[i * 2] != static_cast<unsigned char>(name[i]) || text[i * 2 + 1]) return false;
    return !text[length * 2] && !text[length * 2 + 1];
}
} // namespace

Failure FindArchive(const Reader &reader, Region &out) {
    romfs::Index index;
    const auto failure = index.Open(reader);
    return failure == Failure::None ? index.Find(core::design::ca2::Titlebg::Source::Archive, out) : failure;
}

bool DecodeClim(const std::uint8_t *data, std::size_t size, unsigned format,
                std::array<std::uint8_t, 256> &out) {
    // この2枚専用。画像全体が 128 + 40 バイトと確定している。
    if (size != 168 || (format != 12 && format != 13)) return false;
    const auto *h = data + 128;
    if (std::memcmp(h, "CLIM", 4) || Half(h + 4) != 0xFEFF || Half(h + 6) != 20 ||
        Word(h + 12) != size || Half(h + 16) != 1 || std::memcmp(h + 20, "imag", 4) ||
        Word(h + 24) != 16 || Half(h + 28) != 16 || Half(h + 30) != 16 ||
        Word(h + 32) != format || Word(h + 36) != 128) return false;
    for (unsigned y = 0; y < 16; ++y) {
        for (unsigned x = 0; x < 16; ++x) {
            unsigned n = (y / 8 * 2 + x / 8) * 64;
            for (unsigned b = 0; b < 3; ++b)
                n += ((x >> b & 1) << (2 * b)) + ((y >> b & 1) << (2 * b + 1));
            out[y * 16 + x] = ((data[n / 2] >> ((n & 1) * 4)) & 15) * 17;
        }
    }
    return true;
}

Failure DecodeArchive(const Reader &r, Sources &out) {
    std::uint8_t h[28], entry[12];
    if (!r.Read(0, h, sizeof(h))) return Failure::Read;
    if (std::memcmp(h, "darc", 4) || Half(h + 4) != 0xFEFF || Half(h + 6) != 28 ||
        Word(h + 12) != r.size) return Failure::Darc;
    const std::uint64_t table = Word(h + 16), tableSize = Word(h + 20), dataAt = Word(h + 24);
    if (table < 28 || !Fits(table, tableSize, r.size) || dataAt < table + tableSize || dataAt > r.size ||
        tableSize < 12 || !r.Read(table, entry, 12)) return Failure::Darc;
    const auto count = Word(entry + 8);
    if (Word(entry) != 0x01000000 || count < 1 || count > tableSize / 12 || count > 4096) return Failure::Darc;
    const auto names = table + count * 12;
    unsigned begin = 0, end = 0;
    for (unsigned i = 1; i < count; ++i) {
        if (!r.Read(table + i * 12, entry, 12)) return Failure::Read;
        const auto type = Word(entry);
        const auto name = names + (type & 0xFFFFFFu);
        if (name >= table + tableSize || (type >> 24) > 1) return Failure::Darc;
        if (type >> 24 && Name(r, name, table + tableSize, "timg")) {
            begin = i + 1;
            end = Word(entry + 8);
            if (end <= i || end > count) return Failure::Darc;
            break;
        }
    }
    if (!end) return Failure::Missing;
    Sources staged;
    unsigned found = 0;
    for (unsigned i = begin; i < end; ++i) {
        if (!r.Read(table + i * 12, entry, 12)) return Failure::Read;
        const auto type = Word(entry);
        if (type >> 24) return Failure::Darc;
        const auto name = names + (type & 0xFFFFFFu);
        const auto offset = Word(entry + 4), size = Word(entry + 8);
        if (!Fits(offset, size, r.size) || offset < dataAt) return Failure::Darc;
        for (unsigned t = 0; t < 2; ++t) {
            const auto wanted = core::design::ca2::Titlebg::Source::Textures[t] + 5;
            if (!Name(r, name, table + tableSize, wanted)) continue;
            if (found & (1 << t) || size != 168) return Failure::Clim;
            std::uint8_t image[168];
            if (!r.Read(offset, image, sizeof(image))) return Failure::Read;
            if (!DecodeClim(image, sizeof(image), t == 0 ? 13 : 12, t == 0 ? staged.alpha : staged.luminance)) return Failure::Clim;
            found |= 1 << t;
        }
    }
    if (found != 3) return Failure::Missing;
    out = staged;
    return Failure::None;
}
} // namespace gohan::game::titlebg

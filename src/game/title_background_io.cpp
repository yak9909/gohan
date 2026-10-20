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
std::uint64_t Wide(const std::uint8_t *p) { return Word(p) | std::uint64_t(Word(p + 4)) << 32; }
bool Fits(std::uint64_t offset, std::uint64_t size, std::uint64_t capacity) {
    return offset <= capacity && size <= capacity - offset;
}
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

bool Reader::Read(std::uint64_t offset, void *out, std::size_t bytes) const {
    return read && Fits(offset, bytes, size) && read(context, offset, out, bytes);
}

Failure FindArchive(const Reader &r, Region &out) {
    out = {};
    std::uint8_t front[96];
    if (!r.Read(0, front, sizeof(front))) return Failure::Read;
    std::uint64_t level = 0;
    if (!std::memcmp(front, "IVFC", 4)) {
        const auto shift = Word(front + 0x4C);
        if (shift < 7 || shift > 24) return Failure::Romfs;
        level = std::uint64_t(1) << shift;
    }
    std::uint8_t h[40];
    if (!r.Read(level, h, sizeof(h))) return Failure::Read;
    if (Word(h) != sizeof(h)) return Failure::Romfs;
    const auto dirAt = Word(h + 12), dirs = Word(h + 16), fileAt = Word(h + 28), files = Word(h + 32);
    const auto dataAt = Word(h + 36);
    if (dirs < 24 || files < 32 || dirs > 0x4000000 || files > 0x4000000 ||
        !Fits(level + dirAt, dirs, r.size) || !Fits(level + fileAt, files, r.size) ||
        !Fits(level + dataAt, 0, r.size)) return Failure::Romfs;
    const char *path = core::design::ca2::Titlebg::Source::Archive;
    std::uint32_t parent = 0;
    for (;;) {
        const auto slash = std::strchr(path, '/');
        const auto length = slash ? std::size_t(slash - path) : std::strlen(path);
        if (!length || length > 95 || !Fits(parent, 24, dirs)) return Failure::Romfs;
        char component[96];
        std::memcpy(component, path, length);
        component[length] = 0;
        std::uint8_t dir[24];
        if (!r.Read(level + dirAt + parent, dir, sizeof(dir))) return Failure::Read;
        std::uint32_t current = Word(dir + (slash ? 8 : 12));
        const auto table = slash ? dirAt : fileAt, size = slash ? dirs : files;
        const unsigned header = slash ? 24 : 32;
        bool found = false;
        for (unsigned steps = 0; current != 0xFFFFFFFFu; ++steps) {
            if (steps >= 4096 || steps > size / header || !Fits(current, header, size)) return Failure::Romfs;
            std::uint8_t entry[32];
            if (!r.Read(level + table + current, entry, header)) return Failure::Read;
            const auto nameBytes = Word(entry + header - 4);
            if ((nameBytes & 1) || !Fits(std::uint64_t(current) + header, nameBytes, size)) return Failure::Romfs;
            bool equal = nameBytes == length * 2 && Word(entry) == parent;
            if (equal) {
                std::uint8_t name[190];
                if (!r.Read(level + table + current + header, name, nameBytes)) return Failure::Read;
                for (std::size_t i = 0; i < length; ++i)
                    if (name[i * 2] != static_cast<unsigned char>(component[i]) || name[i * 2 + 1]) equal = false;
            }
            if (equal) {
                if (!slash) {
                    const auto offset = Wide(entry + 8), bytes = Wide(entry + 16);
                    if (!bytes || !Fits(offset, bytes, r.size - level - dataAt)) return Failure::Romfs;
                    out = {level + dataAt + offset, bytes};
                    return Failure::None;
                }
                parent = current;
                found = true;
                break;
            }
            current = Word(entry + 4);
        }
        if (!found) return Failure::Missing;
        path = slash + 1;
    }
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

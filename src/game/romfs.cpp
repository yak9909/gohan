#include "game/romfs.hpp"
#include <cstring>

namespace gohan::game::romfs {
namespace {
std::uint32_t Word(const std::uint8_t *p) {
    return p[0] | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
std::uint64_t Wide(const std::uint8_t *p) { return Word(p) | std::uint64_t(Word(p + 4)) << 32; }
}
bool Fits(std::uint64_t offset, std::uint64_t size, std::uint64_t capacity) {
    return offset <= capacity && size <= capacity - offset;
}
bool Reader::Read(std::uint64_t offset, void *out, std::size_t bytes) const {
    return read && Fits(offset, bytes, size) && read(context, offset, out, bytes);
}
Failure Index::Open(Reader reader) {
    ready_ = false;
    reader_ = reader;
    std::uint8_t front[96];
    if (!reader.Read(0, front, sizeof(front))) return Failure::Read;
    level_ = 0;
    if (!std::memcmp(front, "IVFC", 4)) {
        const auto shift = Word(front + 0x4C);
        if (shift < 7 || shift > 24) return Failure::Romfs;
        level_ = std::uint64_t(1) << shift;
    }
    std::uint8_t bytes[40];
    if (!reader.Read(level_, bytes, sizeof(bytes))) return Failure::Read;
    for (unsigned i = 0; i < 10; ++i) header_[i] = Word(bytes + i * 4);
    if (header_[0] != 40 || header_[4] < 24 || header_[8] < 32) return Failure::Romfs;
    for (unsigned i = 1; i < 9; i += 2)
        if (header_[i + 1] > 0x4000000 || !Fits(level_ + header_[i], header_[i + 1], reader.size)) return Failure::Romfs;
    if (!Fits(level_ + header_[9], 0, reader.size) || (header_[2] & 3) || (header_[6] & 3) ||
        (!header_[2] != !header_[6])) return Failure::Romfs;
    ready_ = true;
    return Failure::None;
}
Failure Index::Find(const char *path, Region &out) const {
    out = {};
    if (!ready_ || !path || !*path) return Failure::Romfs;
    std::uint32_t parent = 0;
    unsigned depth = 0;
    for (;;) {
        const auto *slash = std::strchr(path, '/');
        const auto length = slash ? std::size_t(slash - path) : std::strlen(path);
        if (++depth > 32 || !length || length > 95 || !Fits(parent, 24, header_[4])) return Failure::Romfs;
        for (std::size_t i = 0; i < length; ++i)
            if (static_cast<unsigned char>(path[i]) < 0x20 || static_cast<unsigned char>(path[i]) > 0x7E) return Failure::Romfs;
        const unsigned hashIndex = slash ? 1 : 5, tableIndex = slash ? 3 : 7, entrySize = slash ? 24 : 32;
        const auto table = header_[tableIndex], size = header_[tableIndex + 1];
        std::uint8_t word[4];
        std::uint32_t current;
        const bool hashed = header_[hashIndex + 1] != 0;
        if (hashed) {
            std::uint32_t hash = parent ^ 123456789u;
            for (std::size_t i = 0; i < length; ++i) hash = ((hash >> 5) | (hash << 27)) ^ static_cast<unsigned char>(path[i]);
            if (!reader_.Read(level_ + header_[hashIndex] + (hash % (header_[hashIndex + 1] / 4)) * 4, word, 4)) return Failure::Read;
        } else {
            // 既存の小さな入力はハッシュ表を省いた木。鎖にも同じ範囲・回数の上限を適用する。
            if (!reader_.Read(level_ + header_[3] + parent + (slash ? 8 : 12), word, 4)) return Failure::Read;
        }
        current = Word(word);
        bool found = false;
        for (unsigned steps = 0; current != 0xFFFFFFFFu; ++steps) {
            if (steps >= 4096 || steps > size / entrySize || !Fits(current, entrySize, size)) return Failure::Romfs;
            std::uint8_t entry[32], name[190];
            if (!reader_.Read(level_ + table + current, entry, entrySize)) return Failure::Read;
            const auto bytes = Word(entry + entrySize - 4);
            if ((bytes & 1) || !Fits(std::uint64_t(current) + entrySize, bytes, size)) return Failure::Romfs;
            bool equal = Word(entry) == parent && bytes == length * 2;
            if (equal) {
                if (!reader_.Read(level_ + table + current + entrySize, name, bytes)) return Failure::Read;
                for (std::size_t i = 0; i < length; ++i)
                    if (name[i * 2] != static_cast<unsigned char>(path[i]) || name[i * 2 + 1]) equal = false;
            }
            if (equal) {
                if (!slash) {
                    const auto offset = Wide(entry + 8), bytesCount = Wide(entry + 16);
                    const auto data = level_ + header_[9];
                    if (!bytesCount || !Fits(offset, bytesCount, reader_.size - data)) return Failure::Romfs;
                    out = {data + offset, bytesCount};
                    return Failure::None;
                }
                parent = current;
                found = true;
                break;
            }
            current = Word(entry + (hashed ? entrySize - 8 : 4));
        }
        if (!found) return Failure::Missing;
        path = slash + 1;
    }
}
} // namespace gohan::game::romfs

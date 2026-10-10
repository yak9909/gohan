#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>

// F016: portable on-disk data, never pointers or an entire house/save record.
namespace InteriorSnapshot {
struct Item { std::uint16_t id, flags; };
struct Record {
    Item item;
    std::uint8_t x, z, rotation, layer;
    std::uint32_t reserved;
};
struct Snapshot {
    char magic[8];
    std::uint32_t version, bytes, checksum;
    std::int32_t minX, minZ, maxX, maxZ;
    Item wallpaper, flooring;
    std::uint8_t wallVariant, floorVariant;
    std::uint16_t count;
    Record records[48];
};
static_assert(sizeof(Record) == 12, "Native furniture placement record");
static_assert(sizeof(Snapshot) == 624, "Interior file version 1");
inline std::uint32_t Checksum(const Snapshot &s) {
    const auto *p = reinterpret_cast<const std::uint8_t *>(&s);
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < sizeof(s); ++i) {
        const bool crc = i >= offsetof(Snapshot, checksum)
            && i < offsetof(Snapshot, checksum) + sizeof(s.checksum);
        h = (h ^ (crc ? 0 : p[i])) * 16777619u;
    }
    return h;
}
inline void Seal(Snapshot &s) {
    std::memcpy(s.magic, "GHROOM1", sizeof(s.magic));
    s.version = 1;
    s.bytes = sizeof(s);
    s.checksum = Checksum(s);
}
inline bool Valid(const Snapshot &s) {
    if (std::memcmp(s.magic, "GHROOM1", sizeof(s.magic)) || s.version != 1
        || s.bytes != sizeof(s) || s.count > 48 || s.checksum != Checksum(s)
        || s.minX < 0 || s.minZ < 0 || s.maxX >= 16 || s.maxZ >= 16
        || s.minX > s.maxX || s.minZ > s.maxZ)
        return false;
    for (std::uint32_t i = 0; i < 48; ++i) {
        const Record &r = s.records[i];
        if (r.reserved || r.rotation > 3 || r.layer > 1 || r.x >= 16 || r.z >= 16)
            return false;
        if (i >= s.count) {
            const Record zero = {};
            if (std::memcmp(&r, &zero, sizeof(r))) return false;
        } else if (r.item.flags & 0xC000u) {
            return false;                       // Rotation has its own field.
        }
    }
    return true;
}
}

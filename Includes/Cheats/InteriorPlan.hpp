#pragma once
#include "InteriorSnapshot.hpp"

// Native footprint geometry is supplied by the caller; no item size guesses.
namespace InteriorPlan {
using InteriorSnapshot::Snapshot;
using InteriorSnapshot::Record;
struct Bounds { int minX, minZ, maxX, maxZ; };
struct Geometry {
    bool wall, supports;
    unsigned count;
    int x[9], z[9];
};
using GeometryFn = bool (*)(const Record &, Geometry &);
using WallFn = bool (*)(int, int, unsigned);
struct Plan { unsigned count, clipped; unsigned char order[48]; };
inline bool Inside(const Bounds &b, int x, int z) {
    return x >= b.minX && x <= b.maxX && z >= b.minZ && z <= b.maxZ;
}
inline bool Wall(const Bounds &b, int x, int z, unsigned rotation) {
    switch (rotation) {
        case 0: return z == b.minZ - 1 && x >= b.minX && x <= b.maxX;
        case 1: return x == b.minX - 1 && z >= b.minZ && z <= b.maxZ;
        case 2: return z == b.maxZ + 1 && x >= b.minX && x <= b.maxX;
        case 3: return x == b.maxX + 1 && z >= b.minZ && z <= b.maxZ;
    }
    return false;
}
inline bool Build(const Snapshot &s, const Bounds &dest, GeometryFn geometry,
                  WallFn nativeWall, Plan &out) {
    if (!InteriorSnapshot::Valid(s) || !geometry) return false;
    const Bounds source = {s.minX, s.minZ, s.maxX, s.maxZ};
    if (dest.minX < 0 || dest.minZ < 0 || dest.maxX >= 16 || dest.maxZ >= 16
        || dest.minX > dest.maxX || dest.minZ > dest.maxZ) return false;
    Geometry shapes[48] = {};
    signed char occupancy[2][16][16];
    std::memset(occupancy, -1, sizeof(occupancy));
    bool keep[48] = {};
    // Validate the original complete layout before clipping any record.
    for (unsigned i = 0; i < s.count; ++i) {
        const Record &r = s.records[i];
        Geometry &g = shapes[i];
        if (!geometry(r, g) || !g.count || g.count > 9 || (g.wall && r.layer)) return false;
        keep[i] = true;
        for (unsigned n = 0; n < g.count; ++n) {
            const int x = g.x[n], z = g.z[n];
            if (x < 0 || x >= 16 || z < 0 || z >= 16
                || !(g.wall ? Wall(source, x, z, r.rotation) : Inside(source, x, z))
                || occupancy[r.layer][z][x] != -1) return false;
            occupancy[r.layer][z][x] = static_cast<signed char>(i);
            if (!(g.wall ? Wall(dest, x, z, r.rotation)
                            && (!nativeWall || nativeWall(x, z, r.rotation))
                        : Inside(dest, x, z))) keep[i] = false;
        }
    }
    for (unsigned i = 0; i < s.count; ++i) {
        if (!s.records[i].layer) continue;
        const Geometry &g = shapes[i];
        int parent = -1;
        for (unsigned n = 0; n < g.count; ++n) {
            const int p = occupancy[0][g.z[n]][g.x[n]];
            if (p < 0 || !shapes[p].supports || shapes[p].wall || (parent >= 0 && p != parent))
                return false;
            parent = p;
        }
        if (!keep[parent]) keep[i] = false;
    }
    out = {};
    // Floors first, then wall furniture, then items on furniture.
    for (unsigned phase = 0; phase < 3; ++phase)
        for (unsigned i = 0; i < s.count; ++i) {
            const unsigned kind = s.records[i].layer ? 2 : (shapes[i].wall ? 1 : 0);
            if (keep[i] && phase == kind) out.order[out.count++] = static_cast<unsigned char>(i);
        }
    out.clipped = s.count - out.count;
    const bool same = source.minX == dest.minX && source.minZ == dest.minZ
        && source.maxX == dest.maxX && source.maxZ == dest.maxZ;
    return !same || !out.clipped;               // Same-size invalid layout is not silently dropped.
}
}

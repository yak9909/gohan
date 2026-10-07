// DecorPlace — HHD の Room_SearchFreeSpot 0x4CEB2C（試す関数なし = a8 == 0 の経路）の探す順を写す。説明は DecorPlace.hpp。

#include "DecorPlace.hpp"

namespace DecorPlace {

namespace {

// HHD の静的初期化（0x4CECD8〜0x4CEE88）の値。(x, z) x 8
const s8 kStart[8][2] = { { 0, 1 }, { 1, 0 }, { 0, 0 }, { 0, 0 }, { 1, 1 }, { 1, 1 }, { 1, 0 }, { 0, 1 } };      // byte_845720
const s8 kCorner[8][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 }, { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };  // byte_845730
const s8 kEdge[8][2] = { { -1, 0 }, { 0, -1 }, { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 }, { 1, 0 }, { 0, 1 } };    // byte_845740
const u8 kCornerFlag[8] = { 0, 0, 1, 1, 0, 0, 1, 1 };                                                             // byte_675BCC
const s32 kMaxRadius = 32;

}  // namespace

bool Search(s32 startX, s32 startZ, const Bounds &b, u32 quadrant, TryFn tryFn, void *ctx, s32 &outX, s32 &outZ) {
    if (b.minX > b.maxX || b.minZ > b.maxZ)
        return false;
    // HHD Room_GetPutStartCell 0x4D0C24〜0x4D0C4C: 入口側の行でも内側へ収めてから探索する。
    startX = startX < b.minX ? b.minX : (startX > b.maxX ? b.maxX : startX);
    startZ = startZ < b.minZ ? b.minZ : (startZ > b.maxZ ? b.maxZ : startZ);
    s32 cx[8], cz[8], ex[8], ez[8], kx[8], kz[8];
    u8 flag[8];
    for (u32 i = 0; i < 8; ++i) {
        u32 d = (quadrant + i) & 3u;
        if (i >= 4)
            d += 4;
        cx[i] = startX + kStart[d][0];
        cz[i] = startZ + kStart[d][1];
        kx[i] = kCorner[d][0];
        kz[i] = kCorner[d][1];
        ex[i] = kEdge[d][0];
        ez[i] = kEdge[d][1];
        flag[i] = kCornerFlag[d];
    }
    // 最初の 8 マス（0x4CEF94〜0x4CF038）
    for (u32 i = 0; i < 8; ++i)
        if (tryFn(ctx, cx[i], cz[i])) {
            outX = cx[i];
            outZ = cz[i];
            return true;
        }
    // 輪（0x4CF03C〜0x4CF408）: 半径 r = 2..32。辺の位置 j が外、方向 i が内
    bool outL = false, outR = false, outT = false, outB = false;
    for (s32 r = 2; r <= kMaxRadius; ++r) {
        for (u32 i = 0; i < 8; ++i) {
            cx[i] += kx[i];
            cz[i] += kz[i];
        }
        for (s32 j = 0; j < r; ++j)
            for (u32 i = 0; i < 8; ++i) {
                const s32 x = cx[i] + j * ex[i], z = cz[i] + j * ez[i];
                if (x < b.minX || x > b.maxX) {
                    if (x < b.minX)
                        outL = true;
                    if (x > b.maxX)
                        outR = true;
                    if (outL && outR && outT && outB)
                        return false;
                }
                if (z < b.minZ || z > b.maxZ) {
                    if (z < b.minZ)
                        outT = true;
                    if (z > b.maxZ)
                        outB = true;
                    if (outL && outR && outT && outB)
                        return false;
                }
                const bool tryHere = flag[i] == 0 || j == r - 1 || r == 2;   // 角の旗が立つ方向は最後の 1 歩だけ（半径 2 は全部）
                if (tryHere && tryFn(ctx, x, z)) {
                    outX = x;
                    outZ = z;
                    return true;
                }
                if (flag[i]) {                                               // 角の歩みを足したマスも試す（負なら試さない）
                    const s32 x2 = x + kx[i], z2 = z + kz[i];
                    if (x2 >= 0 && z2 >= 0 && tryFn(ctx, x2, z2)) {
                        outX = x2;
                        outZ = z2;
                        return true;
                    }
                }
            }
    }
    return false;
}

}  // namespace DecorPlace

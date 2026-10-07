#pragma once

// DecorPlace — リストから出す家具の置き場所を探す（T022 段 2）。HHD の Room_SearchFreeSpot 0x4CEB2C の探す順を ACNL のマスで行う。
// 根拠（解析リポジトリ project_v2）: work/hhd/FINDINGS.md IDA-opus-5.5-HHD-F016、work/FINDINGS.md IDA-gpt-6.1-sol-F002 / F001。
//   - 方向の表は HHD の静的初期化（0x4CEB2C 内）と byte_675BCC の原本の値。方向 = (四分円 + i) & 3（i >= 4 なら +4）。
//   - 四分円は HHD の Room_GetMovingFurnitureQuadrant 0x3FBD8C（押し引き中の家具が無ければ 4 → 2）。リストから出すときは常に 2。
//   - HHD のマスは ACNL の半分（16 単位）で大きさの種類も違うので、座標は写さず「探す順」だけを ACNL のマスで行う。
//     ACNL の Room_TryPutFurnitureAt 0x6920CC は基準のマスを受け取り、足跡（0x94CA08）は基準のマスを含むので、輪のマスを基準のマスとして試す（原点のずれ 0）。
// 純粋な関数（ゲームの関数は試す関数 try_ の中だけ）。tools/hhd/test_decor_place.py が HHD の手順を Python で写した参照と突き合わせる。

#include <3ds/types.h>

namespace DecorPlace {

struct Bounds { s32 minX, minZ, maxX, maxZ; };   // 両端を含む（Room_GetInteriorCellBounds 0x2853A8 と同じ）
typedef bool (*TryFn)(void *ctx, s32 x, s32 z);   // 置けたら真

// startをbへ収め（HHD Room_GetPutStartCell 0x4D0C24〜0x4D0C4C）、そこから探す。
// 置けたマスをoutX/outZに返す。不正な範囲・部屋の4辺を全部越えた・半径32を越えた場合は偽。
bool Search(s32 startX, s32 startZ, const Bounds &b, u32 quadrant, TryFn tryFn, void *ctx, s32 &outX, s32 &outZ);

}  // namespace DecorPlace

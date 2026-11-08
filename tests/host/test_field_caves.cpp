#include "game/field_caves.hpp"
#include "game/generated/api.hpp"
#include "field_cave_expected.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>

using namespace gohan::game;
namespace caves = field_caves;
int main() {
    static_assert(caves::ValidAllocations(caves::kAllocations, sizeof(generated::g_TextPagePadding_t)));
    constexpr caves::Allocation overlap[] = {{"a", 0, 2}, {"b", 4, 2}};
    constexpr caves::Allocation overflow[] = {{"a", 12, 2}};
    constexpr caves::Allocation unaligned[] = {{"a", 1, 1}};
    static_assert(!caves::ValidAllocations(overlap, 16));
    static_assert(!caves::ValidAllocations(overflow, 16));
    static_assert(!caves::ValidAllocations(unaligned, 16));
    // 比較用の配置は期待値と一緒に試験側で固定する。
    constexpr std::uintptr_t base = 0x00838018;
    for (std::size_t i = 0; i < std::size(caves::kAllocations); ++i)
        assert(caves::Address(i) == generated::g_TextPagePadding_addr + caves::kAllocations[i].offset);
    auto tree = caves::Tree(base);
    auto holes = caves::DigHoles(base);
    auto scrape = caves::DigScrape(base);
    auto dirt = caves::DigDirt(base);
    assert(std::equal(tree.begin(), tree.end(), std::begin(expected::kTree), std::end(expected::kTree)));
    assert(std::equal(holes.begin(), holes.end(), std::begin(expected::kDigHoles), std::end(expected::kDigHoles)));
    assert(std::equal(scrape.begin(), scrape.end(), std::begin(expected::kDigScrape), std::end(expected::kDigScrape)));
    assert(std::equal(dirt.begin(), dirt.end(), std::begin(expected::kDigDirt), std::end(expected::kDigDirt)));
    // 配置をずらしても外部 BL とリテラルが正しい行き先を指す。
    auto relocated = caves::Tree(base + 256);
    auto target = [](std::uintptr_t from, std::uint32_t word) {
        auto displacement = static_cast<std::int32_t>((word & 0xFFFFFFu) << 8) >> 6;
        return static_cast<std::uintptr_t>(static_cast<std::int64_t>(from) + 8 + displacement);
    };
    assert(target(base + 256 + caves::kAllocations[0].offset + 21 * 4, relocated[21]) == generated::Player_BeginFieldObjAction_addr);
    assert(relocated[38] == generated::g_TreeHitCounts_addr && relocated[39] == generated::g_DropDataTable_addr);
    // フックも同じ配置で全語一致する。
    assert(caves::Branch(generated::PatchTree0_addr, caves::Address(0), 0xEB000000u) == 0xEB0A7212u);
    assert(caves::Branch(generated::PatchDig30_addr, caves::Address(1), 0xEB000000u) == 0xEB0A5EC3u);
    assert(caves::Branch(generated::PatchDig31_addr, caves::Address(2), 0xEB000000u) == 0xEB077228u);
    assert(caves::Branch(generated::PatchDig32_addr, caves::Address(2), 0xEB000000u) == 0xEB077203u);
    assert(caves::Branch(generated::PatchDig33_addr, caves::Address(3), 0xEB000000u) == 0xEB077253u);
    std::puts("ケーブの全 116 語の一致と割り当て: 成功");
}

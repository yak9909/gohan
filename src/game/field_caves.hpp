#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gohan::game::field_caves {
struct Allocation { const char *name; std::size_t offset; std::size_t words; };
// 詰め物の先頭からの距離。全ての割り当てはこの表で管理する。
inline constexpr Allocation kAllocations[] = {
    {"Tree", 0x3E8, 40}, {"DigHoles", 0x4E8, 42},
    {"DigScrape", 0x5E8, 15}, {"DigDirt", 0x668, 19},
};
constexpr bool ValidAllocations(std::span<const Allocation> entries, std::size_t bytes) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto &a = entries[i];
        if ((a.offset & 3) || a.offset > bytes || a.words > (bytes - a.offset) / 4) return false;
        for (std::size_t j = 0; j < i; ++j) {
            const auto &b = entries[j];
            if (a.offset < b.offset + b.words * 4 && b.offset < a.offset + a.words * 4) return false;
        }
    }
    return true;
}
// ARM の B/BL。外部の行き先もケーブ内の行き先も名前で渡す。
constexpr std::uint32_t Branch(std::uintptr_t from, std::uintptr_t to, std::uint32_t opcode) {
    return opcode | (static_cast<std::uint32_t>((static_cast<std::int64_t>(to) -
                       static_cast<std::int64_t>(from) - 8) / 4) & 0xFFFFFFu);
}
std::array<std::uint32_t, 40> Tree(std::uintptr_t base);
std::array<std::uint32_t, 42> DigHoles(std::uintptr_t base);
std::array<std::uint32_t, 15> DigScrape(std::uintptr_t base);
std::array<std::uint32_t, 19> DigDirt(std::uintptr_t base);
std::uintptr_t Address(std::size_t index);
} // namespace gohan::game::field_caves

#include "game/memory.hpp"
#include <cstring>
#include <limits>

namespace gohan::game::memory {
bool IsHeapPointer(std::uintptr_t address) {
    // 通常ヒープが始まる 0x08000000 以降で、線形ヒープも収まる範囲の
    // 4 バイト整列だけを候補にする。範囲だけで生存や可読性を保証せず、
    // 実際に読む範囲は Readable、アクターの種類は IsClass で別に確かめる。
    return address >= 0x08000000u && address < 0x40000000u && !(address & 3);
}
bool Reader::Readable(std::uintptr_t address, std::size_t length) {
    if (!length || length - 1 > std::numeric_limits<std::uintptr_t>::max() - address) return false;
    const auto last = address + length - 1;
    for (auto page = address & ~std::uintptr_t(0xFFF);;) {
        const auto at = page < address ? address : page;
        const Region *found = nullptr;
        for (unsigned i = 0; i < count_; ++i)
            if (at >= cache_[i].base && at - cache_[i].base < cache_[i].size) { found = &cache_[i]; break; }
        if (!found) {
            Region region{page, 0x1000, false};
            if (!access_.query(at, region) || !region.size || at < region.base || at - region.base >= region.size)
                region = {page, 0x1000, false};
            const auto slot = count_ < core::limits::ReadableRegions ? count_++ : next_++ % core::limits::ReadableRegions;
            cache_[slot] = region; found = &cache_[slot];
        }
        if (!found->readable) return false;
        if (page == (last & ~std::uintptr_t(0xFFF))) return true;
        page += 0x1000;
    }
}
bool Reader::Copy(std::uintptr_t address, void *out, std::size_t size) {
    return Readable(address, size) && access_.copy(address, out, size);
}
bool Reader::IsClass(std::uintptr_t object, const char *name) {
    if (!name || !IsHeapPointer(object)) return false;
    std::uint32_t table, info, text;
    if (!Copy(object, &table, 4) || table < 4 || !Copy(table - 4, &info, 4) ||
        info > 0xFFFFFFFBu || !Copy(info + 4, &text, 4)) return false;
    // 型名を無制限に走査しない。終端を含め、1 バイトずつ読めることを確かめる。
    for (unsigned i = 0; i < 128; ++i) {
        char c;
        if (text > 0xFFFFFFFFu - i || !Copy(text + i, &c, 1) || c != name[i]) return false;
        if (!c) return true;
    }
    return false;
}
} // namespace gohan::game::memory

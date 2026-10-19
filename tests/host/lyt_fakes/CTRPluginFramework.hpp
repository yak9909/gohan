#pragma once
#include <cassert>
#include <cstdint>
#include <string>

namespace CTRPluginFramework {
struct FakeLayout {
    static inline std::uint64_t title = 0x0004000000086200ULL;
    static inline bool readable = true;
    static inline int reads = 0;
    static inline std::string top = "123456789";
    static inline std::string bottom = "123456789";
};
// 読み取りだけを公開する。書き換えとフックの設置は利用できない。
struct Process {
    static std::uint64_t GetTitleID() { return FakeLayout::title; }
    static bool Read8(std::uintptr_t address, std::uint8_t &value) {
        ++FakeLayout::reads;
        if (!FakeLayout::readable)
            return false;
        assert((address >= 1024 && address < 1033) || (address >= 2048 && address < 2057));
        value = address < 2048 ? FakeLayout::top.at(address - 1024) :
                                FakeLayout::bottom.at(address - 2048);
        return true;
    }
};
} // namespace CTRPluginFramework

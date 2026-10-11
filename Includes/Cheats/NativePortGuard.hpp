#pragma once
#include <CTRPluginFramework.hpp>

namespace NativePortGuard {
struct Word { u32 address, expected; };
inline u32 Read(u32 address) {
    if (!(address & 3u)) return *reinterpret_cast<const volatile u32 *>(address);
    const volatile u8 *p = reinterpret_cast<const volatile u8 *>(address);
    u32 word = 0;
    for (unsigned i=0;i<4;++i) word |= static_cast<u32>(p[i]) << (8u*i);
    return word;
}
template <unsigned N> bool Matches(const Word (&words)[N]) {
    if (CTRPluginFramework::Process::GetTitleID() != 0x0004000000086200ULL) return false;
    for (const Word &w : words) if (Read(w.address) != w.expected) return false;
    return true;
}
inline bool Heap(u32 p) { return p >= 0x08000000u && p < 0x40000000u && (p & 3u) == 0; }
}

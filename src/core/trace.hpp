#pragma once
// 開発版だけ実体を持つ記録の輪。印・引数 2 つ・フレーム番号を残す。製品版では何も生成しない。
#include <cstddef>
#include <cstdint>

namespace gohan::core::trace {
struct Entry { const char *tag; std::uint32_t a, b, frame; };
#ifdef GOHAN_DEV
inline constexpr std::size_t Capacity = 64;
struct Ring {
    Entry items[Capacity];
    std::uint32_t head = 0, frame = 0;
};
inline Ring &Instance() { static Ring ring; return ring; }
inline void SetFrame(std::uint32_t f) { Instance().frame = f; }
inline void Mark(const char *tag, std::uint32_t a = 0, std::uint32_t b = 0) {
    Ring &r = Instance();
    r.items[r.head % Capacity] = {tag, a, b, r.frame};
    ++r.head;
}
// 古い順に n 件目（0 が最古）。件数は Count()。
inline std::size_t Count() { const auto h = Instance().head; return h < Capacity ? h : Capacity; }
inline Entry At(std::size_t i) {
    const Ring &r = Instance();
    const std::uint32_t first = r.head < Capacity ? 0 : r.head - Capacity;
    return r.items[(first + i) % Capacity];
}
#else
inline void SetFrame(std::uint32_t) {}
inline void Mark(const char *, std::uint32_t = 0, std::uint32_t = 0) {}
inline std::size_t Count() { return 0; }
#endif
} // namespace gohan::core::trace

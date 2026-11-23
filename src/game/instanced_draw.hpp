#pragma once
#include <array>
#include <cstdint>
namespace gohan::game::instance {
using Matrix = std::array<float, 12>;
// ctx[2] の指す領域はヒープに限らない。読む範囲を確かめてから最後の体を消す。
template<class ReadWord, class Readable, class WriteWord>
void ResetCache(std::uintptr_t context, ReadWord read_word, Readable readable, WriteWord write_word) {
    if (!readable(context, 12)) return;
    const auto inner = static_cast<std::uintptr_t>(read_word(context + 8));
    if (inner && !(inner & 3) && readable(inner, 32))
        write_word(inner + 28, std::uint32_t{0});
}
class Backend {
  public:
    virtual ~Backend() = default;
    virtual bool Prepare(std::uintptr_t context, const Matrix &) = 0;
    virtual void ResetMatrixCache(std::uintptr_t context) = 0;
    virtual void Meshes(std::uintptr_t context) = 0;
};
} // namespace gohan::game::instance

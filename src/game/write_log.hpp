#pragma once
// 書く前に控え、逆順に戻す。容量超過では書かない。
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace gohan::game {
template <std::size_t N> class WriteLog {
  public:
    WriteLog() = default;
    WriteLog(const WriteLog &) = delete;
    WriteLog &operator=(const WriteLog &) = delete;
    template <class T> bool Write(T *address, const T &value) {
        static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= SavedBytes);
        if (!address || count_ >= N) return false;
        auto &s = saved_[count_++];
        s.address = address; s.bytes = sizeof(T);
        std::memcpy(s.old, address, sizeof(T));
        std::memcpy(address, &value, sizeof(T));
        return true;
    }
    bool Write(std::uintptr_t *address, std::uintptr_t value) {
        return Write<std::uintptr_t>(address, value);
    }
    void Restore() {
        while (count_) {
            const auto &s = saved_[--count_];
            std::memcpy(s.address, s.old, s.bytes);
        }
    }
    std::size_t Count() const { return count_; }
    static constexpr std::size_t Capacity() { return N; }
  private:
    static constexpr std::size_t SavedBytes = 64;
    struct Saved { void *address; std::size_t bytes; unsigned char old[SavedBytes]; };
    Saved saved_[N]{};
    std::size_t count_ = 0;
};
} // namespace gohan::game

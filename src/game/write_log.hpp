#pragma once
// 書く前に控え、逆順に戻す。容量を超えたら書かずに false を返す。
#include <cstddef>
#include <cstdint>

namespace gohan::game {
template <std::size_t N> class WriteLog {
  public:
    bool Write(std::uintptr_t *address, std::uintptr_t value) {
        if (!address || count_ >= N) return false;
        saved_[count_++] = {address, *address};
        *address = value;
        return true;
    }
    // 控えた順の逆に戻す。
    void Restore() {
        while (count_) { --count_; *saved_[count_].address = saved_[count_].old; }
    }
    std::size_t Count() const { return count_; }
    static constexpr std::size_t Capacity() { return N; }
  private:
    struct Saved { std::uintptr_t *address; std::uintptr_t old; };
    Saved saved_[N]{};
    std::size_t count_ = 0;
};
} // namespace gohan::game

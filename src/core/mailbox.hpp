#pragma once
#include "core/sync.hpp"
#include <cstdint>
#include <type_traits>

namespace gohan::core {
enum class MailRead { Updated, Unchanged, Busy };

// 固定長の値を最新の一組として渡す。途中の世代は読み飛ばせる。
// 読み手・書き手とも待たない。Busy の場合は後で再試行する。
template <class T> class Mailbox {
    static_assert(std::is_trivially_copyable_v<T>);

  public:
    bool TryPublish(const T &value) {
        if (!mutex_.TryLock())
            return false;
        value_ = value;
        if (++generation_ == 0)
            ++generation_;
        mutex_.Unlock();
        return true;
    }

    MailRead ReadIfNew(T &value, std::uint32_t &generation) {
        if (!mutex_.TryLock())
            return MailRead::Busy;
        if (generation_ == 0 || generation_ == generation) {
            mutex_.Unlock();
            return MailRead::Unchanged;
        }
        value = value_;
        generation = generation_;
        mutex_.Unlock();
        return MailRead::Updated;
    }

  private:
    SpinMutex mutex_;
    // 読み出しのコピーも同じロック内なので、値の置き場は一つで足りる。
    T value_{};
    std::uint32_t generation_ = 0;
};
} // namespace gohan::core

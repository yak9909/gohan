#pragma once
#include <atomic>
#include <type_traits>

namespace gohan::core {
// 一枠の要求。受け取りが終わるまで次の要求を受け付けない。
// 同時の書き込みも CAS で拒否し、受理した値は一度だけ取り出せる。
template <class T> class Request {
    static_assert(std::is_trivially_copyable_v<T>);

  public:
    bool TrySubmit(const T &value) {
        State expected = State::Empty;
        if (!state_.compare_exchange_strong(expected, State::Writing, std::memory_order_acquire))
            return false;
        value_ = value;
        state_.store(State::Pending, std::memory_order_release);
        return true;
    }

    bool TryTake(T &value) {
        State expected = State::Pending;
        if (!state_.compare_exchange_strong(expected, State::Reading, std::memory_order_acquire))
            return false;
        value = value_;
        state_.store(State::Empty, std::memory_order_release);
        return true;
    }

  private:
    enum class State : unsigned { Empty, Writing, Pending, Reading };
    std::atomic<State> state_{State::Empty};
    T value_{};
};
} // namespace gohan::core

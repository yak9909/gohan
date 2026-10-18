#pragma once
#include <atomic>

namespace gohan::core {
// 短いコピーだけを保護する。ゲーム側では TryLock で待機を避ける。
class SpinMutex {
  public:
    bool TryLock() { return !flag_.test_and_set(std::memory_order_acquire); }
    void Lock() {
        while (!TryLock()) {
        }
    }
    void Unlock() { flag_.clear(std::memory_order_release); }

  private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

class LockGuard {
  public:
    explicit LockGuard(SpinMutex &mutex) : mutex_(mutex) { mutex_.Lock(); }
    ~LockGuard() { mutex_.Unlock(); }
    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;

  private:
    SpinMutex &mutex_;
};
} // namespace gohan::core

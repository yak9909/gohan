#pragma once
#include <atomic>

namespace gohan::core {
// 水準と一度だけの知らせ。公開中でも取りこぼさず、読み書きは待たない。
class Signal {
  public:
    void Set(bool value = true) { value_.store(value, std::memory_order_release); }
    bool Get() const { return value_.load(std::memory_order_acquire); }
    bool Take() { return value_.exchange(false, std::memory_order_acq_rel); }

  private:
    std::atomic<bool> value_{false};
};
} // namespace gohan::core

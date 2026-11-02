#pragma once
#include "core/limits.hpp"
#include "core/sync.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace gohan::core::frame {
using Step = void (*)();
enum class Result { Queued, Unchanged, Full, Busy, Invalid };
struct Change {
    Result result;
    std::uint32_t revision;
};

class Dispatcher {
  public:
    // 小さい order を先に、同値なら登録の順。同じ関数は一枠だけ。
    Change Add(Step step, int order = 0);
    Change Remove(Step step);
    // 反映は次のフレーム境界。変更中なら前回の一覧で動く。
    // Remove の世代の反映確認後、その関数の資源を解放できる。
    bool IsApplied(std::uint32_t revision) const;
    void Dispatch();
    // 呼び出し元がフックを外し、実行中の Dispatch が無い時だけ使う。
    void Reset();

  private:
    struct Entry {
        Step step = nullptr;
        int order = 0;
    };
    SpinMutex changes_;
    SpinMutex dispatching_;
    Entry desired_[limits::FrameSteps]{};
    Entry active_[limits::FrameSteps]{};
    std::size_t desiredCount_ = 0;
    std::size_t activeCount_ = 0;
    std::uint32_t revision_ = 0;
    std::atomic<std::uint32_t> applied_{0};
};

Dispatcher &Global();
// ゲーム側は毎フレームこの関数を一つ呼ぶ。
void Dispatch();
} // namespace gohan::core::frame

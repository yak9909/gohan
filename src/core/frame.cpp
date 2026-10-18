#include "core/frame.hpp"

namespace gohan::core::frame {
Change Dispatcher::Add(Step step, int order) {
    if (!step)
        return {Result::Invalid, 0};
    if (!changes_.TryLock())
        return {Result::Busy, 0};
    for (std::size_t i = 0; i < desiredCount_; ++i) {
        if (desired_[i].step == step) {
            const auto revision = revision_;
            changes_.Unlock();
            return {Result::Unchanged, revision};
        }
    }
    if (desiredCount_ == limits::FrameSteps) {
        changes_.Unlock();
        return {Result::Full, 0};
    }
    std::size_t pos = desiredCount_++;
    while (pos && desired_[pos - 1].order > order) {
        desired_[pos] = desired_[pos - 1];
        --pos;
    }
    desired_[pos] = {step, order};
    const auto revision = ++revision_;
    changes_.Unlock();
    return {Result::Queued, revision};
}

Change Dispatcher::Remove(Step step) {
    if (!step)
        return {Result::Invalid, 0};
    if (!changes_.TryLock())
        return {Result::Busy, 0};
    for (std::size_t i = 0; i < desiredCount_; ++i) {
        if (desired_[i].step != step)
            continue;
        for (std::size_t j = i + 1; j < desiredCount_; ++j)
            desired_[j - 1] = desired_[j];
        --desiredCount_;
        const auto revision = ++revision_;
        changes_.Unlock();
        return {Result::Queued, revision};
    }
    const auto revision = revision_;
    changes_.Unlock();
    return {Result::Unchanged, revision};
}

bool Dispatcher::IsApplied(std::uint32_t revision) const {
    // 未確認の変更を約21億回以上ためないこと。世代の周回にも対応する。
    return applied_.load(std::memory_order_acquire) - revision <= 2147483647u;
}

void Dispatcher::Dispatch() {
    if (!dispatching_.TryLock())
        return;
    if (changes_.TryLock()) {
        activeCount_ = desiredCount_;
        for (std::size_t i = 0; i < activeCount_; ++i)
            active_[i] = desired_[i];
        applied_.store(revision_, std::memory_order_release);
        changes_.Unlock();
    }
    for (std::size_t i = 0; i < activeCount_; ++i)
        active_[i].step();
    dispatching_.Unlock();
}

void Dispatcher::Reset() {
    LockGuard dispatchLock(dispatching_);
    LockGuard changesLock(changes_);
    desiredCount_ = activeCount_ = 0;
    applied_.store(++revision_, std::memory_order_release);
}

namespace {
Dispatcher dispatcher;
}
Dispatcher &Global() { return dispatcher; }
void Dispatch() { dispatcher.Dispatch(); }
} // namespace gohan::core::frame

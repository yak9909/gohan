#include "core/jobs.hpp"
#include "core/frame.hpp"
#include "core/signal.hpp"
#include <optional>
#ifdef __3DS__
#include <CTRPluginFramework.hpp>
#else
#include <thread>
#include <chrono>
#endif

namespace gohan::core::jobs {
bool Queue::Submit(Job job) {
    if (!job.work || !job.done || !mutex_.TryLock()) return false;
    const bool accepted = !stopping_.Get() && count_ < limits::Jobs;
    if (accepted) entries_[(head_ + count_++) % limits::Jobs] = {job, State::Queued};
    mutex_.Unlock();
    return accepted;
}
bool Queue::WorkOne() {
    mutex_.Lock();
    Entry *entry = nullptr;
    for (std::size_t i = 0; i < count_; ++i) {
        auto &candidate = entries_[(head_ + i) % limits::Jobs];
        if (candidate.state == State::Queued) { entry = &candidate; break; }
    }
    if (!entry) { mutex_.Unlock(); return false; }
    entry->state = State::Running;
    const auto job = entry->job;
    mutex_.Unlock();
    job.work(job.context);
    mutex_.Lock();
    entry->state = State::Complete;
    mutex_.Unlock();
    return true;
}
void Queue::Deliver() {
    for (std::size_t i = 0; i < limits::Jobs; ++i) {
        if (!mutex_.TryLock()) return;
        if (!count_ || entries_[head_].state != State::Complete) { mutex_.Unlock(); return; }
        const auto job = entries_[head_].job;
        head_ = (head_ + 1) % limits::Jobs;
        --count_;
        mutex_.Unlock();
        job.done(job.context);
    }
}
bool Queue::Pending() {
    if (!mutex_.TryLock()) return false;
    bool pending = false;
    for (std::size_t i = 0; i < count_; ++i)
        pending = pending || entries_[(head_ + i) % limits::Jobs].state == State::Queued;
    mutex_.Unlock();
    return pending;
}
void Queue::Stop() { stopping_.Set(); }
void Queue::Discard() {
    // 呼び手が作業の終了を保証するので、返却の間のロックは要らない。
    while (count_) {
        const auto job = entries_[head_].job;
        head_ = (head_ + 1) % limits::Jobs;
        --count_;
        if (job.discard) job.discard(job.context);
    }
}
Queue &Global() { static Queue queue; return queue; }
bool Submit(Job job) { return Global().Submit(job); }
namespace {
Signal running, finished, stopTried;
#ifdef __3DS__
std::optional<CTRPluginFramework::Task> task;
std::int32_t Work(void *) {
    while (Global().WorkOne()) {}
    finished.Set();
    return 0;
}
bool Idle() {
    if (!task) return true;
    const auto status = task->Status();
    return status == CTRPluginFramework::Task::Idle || status == CTRPluginFramework::Task::Finished;
}
void Sleep() { svcSleepThread(1000000); }
#else
std::optional<std::thread> task;
void Work() { while (Global().WorkOne()) {} finished.Set(); }
bool Idle() { return !task || finished.Get(); }
void Sleep() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
#endif
void Pump() {
    if (!running.Get()) return;
    Global().Deliver();
    if (!Idle() || !Global().Pending()) return;
    finished.Set(false);
#ifdef __3DS__
    task->Start();
#else
    if (task) task->join();
    task.emplace(Work);
#endif
}
bool Wire() {
#ifdef __3DS__
    task.emplace(Work, nullptr, CTRPluginFramework::Task::AppCore);
#endif
    const auto change = frame::Global().Add(Pump, -50);
    if (change.result != frame::Result::Queued && change.result != frame::Result::Unchanged) return false;
    running.Set();
    return true;
}
void Shutdown() {
    StopForExit();
    frame::Global().Remove(Pump);
    // 時間内に戻らなかった I/O の文脈は保持する。終了中に解放しない。
    if (!Idle()) return;
#ifndef __3DS__
    if (task) task->join();
#endif
    Global().Discard();
    task.reset();
}
const Feature feature{"core.jobs", Wire, Shutdown, ShutdownStage::Other};
}
void StopForExit() {
    if (stopTried.Get()) return;
    stopTried.Set();
    running.Set(false);
    Global().Stop();
    for (unsigned i = 0; i < 1000 && !Idle(); ++i) Sleep();
}
const Feature &JobsFeature() { return feature; }
} // namespace gohan::core::jobs

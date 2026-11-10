#pragma once
#include "core/feature.hpp"
#include "core/sync.hpp"
#include "core/signal.hpp"
#include <cstddef>

namespace gohan::core::jobs {
struct Job {
    void *context = nullptr;
    void (*work)(void *) = nullptr; // ファイル I/O・計算だけ。ゲームの関数を呼ばない。
    void (*done)(void *) = nullptr; // Pump を呼んだゲームのスレッドへ返す。
    void (*discard)(void *) = nullptr; // 終了時の所有物の返却だけ。
};
class Queue {
  public:
    bool Submit(Job job);
    bool WorkOne(); // 作業のスレッドは一つだけ。
    void Deliver(); // ゲームのスレッドだけ。
    void Stop();
    void Discard(); // 作業の終了後だけ。
    bool Pending();
  private:
    enum class State { Queued, Running, Complete };
    struct Entry { Job job{}; State state = State::Queued; };
    SpinMutex mutex_;
    Entry entries_[limits::Jobs]{};
    std::size_t head_ = 0, count_ = 0;
    Signal stopping_;
};
Queue &Global();
bool Submit(Job job);
const Feature &JobsFeature();
// CTRPF が作業の管理を止める前に呼ぶ。ゲームや描画には触れない。
void StopForExit();
} // namespace gohan::core::jobs

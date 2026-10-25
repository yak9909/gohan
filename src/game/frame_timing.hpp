#pragma once
#ifdef GOHAN_DEV
#include "core/format.hpp"
#include "core/mailbox.hpp"
#ifdef __3DS__
#include <CTRPluginFramework.hpp>
#else
#include <chrono>
#endif
#include <algorithm>
#include <cstdint>
#endif

namespace gohan::game::frame_timing {
enum class Stage { Total, Update, Menu, Notice, Layout, Commit, Renderer, Present, Record, Count };
#ifdef GOHAN_DEV
inline constexpr unsigned Count = static_cast<unsigned>(Stage::Count);
struct Sample {
    unsigned last = 0, maximum = 0;
    std::uint64_t total = 0;
};
struct Snapshot {
    Sample stages[Count];
    unsigned frames = 0;
};
// 書き手はゲームの描画スレッドだけ。状態の読み手へは Mailbox で渡す。
inline unsigned current[Count]{};
inline Snapshot history;
inline core::Mailbox<Snapshot> published;
inline bool started = false;

inline void Add(Stage stage, unsigned micros) { current[static_cast<unsigned>(stage)] += micros; }
inline void Begin() {
    // 記録は Dispatch の後でゲームが呼ぶ。次の境界で前フレームを完成させる。
    if (started) {
        if (history.frames == UINT32_MAX) history = {};
        ++history.frames;
        current[static_cast<unsigned>(Stage::Total)] += current[static_cast<unsigned>(Stage::Record)];
        for (unsigned i = 0; i < Count; ++i) {
            auto &sample = history.stages[i];
            sample.last = current[i];
            sample.maximum = std::max(sample.maximum, current[i]);
            sample.total += current[i];
        }
        published.TryPublish(history);
    }
    for (auto &value : current) value = 0;
    started = true;
}
class Scope {
  public:
    explicit Scope(Stage stage) : stage_(stage) {}
    ~Scope() { Stop(); }
    void Stop() {
        if (stopped_) return;
        stopped_ = true;
#ifdef __3DS__
        Add(stage_, unsigned(clock_.GetElapsedTime().AsMicroseconds()));
#else
        Add(stage_, unsigned(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - clock_).count()));
#endif
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
  private:
    Stage stage_;
    bool stopped_ = false;
#ifdef __3DS__
    CTRPluginFramework::Clock clock_;
#else
    std::chrono::steady_clock::time_point clock_ = std::chrono::steady_clock::now();
#endif
};
inline void AppendStatus(core::TextWriter &writer) {
    static Snapshot last;
    static std::uint32_t generation = 0;
    published.ReadIfNew(last, generation);
    writer.Append("gohan 毎フレーム us（今/平均/最大）標本 ").Unsigned(last.frames);
    constexpr const char *names[] = {"合計", "更新", "メニュー", "通知", "うち組み版", "確定", "描画部品", "うち木・提出", "ゲームの記録"};
    for (unsigned i = 0; i < Count; ++i) {
        const auto &s = last.stages[i];
        writer.Append("\n").Append(names[i]).Append(" ").Unsigned(s.last).Append("/")
            .Unsigned(last.frames ? unsigned(s.total / last.frames) : 0).Append("/").Unsigned(s.maximum);
    }
}
#else
inline void Begin() {}
class Scope {
  public:
    explicit Scope(Stage) {}
    void Stop() {}
};
#endif
} // namespace gohan::game::frame_timing

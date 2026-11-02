#pragma once
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <map>
#include <vector>

// 実機やゲームのデータに触らない、フック導入の境界だけの代役。
namespace CTRPluginFramework {
enum class HookResult { Success, Failed };
enum HookFlags { USE_LR_TO_RETURN = 1, EXECUTE_OI_BEFORE_CB = 2, EXECUTE_OI_AFTER_CB = 4,
                 HOOK_DEFAULT_PARAMS = USE_LR_TO_RETURN | EXECUTE_OI_BEFORE_CB };
struct FakeHook { std::uintptr_t callback = 0; std::uint32_t flags = 0; bool enabled = false; };
// 作業（Task）の状態。実際の作業のスレッドの代わりに、試験が Fake::RunHeldTask で動かす。
struct FakeTaskState {
    std::int32_t (*func)(void *) = nullptr;
    void *arg = nullptr;
    unsigned flags = 0; // 0: 待機、1: 予約（動くのを待つ）、2: 動作中、4: 終了（CTRPF の Task::Status と同じ値）
    std::int32_t result = 0;
};
struct Fake {
    static inline std::string bytes = "123456789";
    static inline std::uint64_t title = 0x0004000000086200ULL;
    static inline bool paused = false;
    static inline int reads = 0;
    static inline int pauseCalls = 0;
    static inline int playCalls = 0;
    static inline int initializeCalls = 0;
    static inline int enableCalls = 0;
    static inline int disableCalls = 0;
    static inline bool readable = true;
    static inline HookResult enableResult = HookResult::Success;
    static inline std::map<std::uintptr_t, FakeHook> hooks;
    static inline std::map<std::uintptr_t, std::string> memory;
    static inline std::uintptr_t unreadable = 0, failEnable = 0;
    static inline std::uintptr_t thread = 1;
    static inline std::uint64_t ticks = 0;
    static inline bool inputReady = true;
    // CTRPF のメニューを閉じて再開した知らせ（メニューの画面の試験用。取り出すと偽に戻る）。
    static inline bool ctrpfResumed = false;
    template <class... Args> static void Invoke(std::uintptr_t address, Args... args) {
        const auto &hook = hooks.at(address);
        assert(hook.enabled);
        reinterpret_cast<void (*)(Args...)>(hook.callback)(args...);
    }
    // 入力と時刻（メニューの画面の試験用）。keys は CTRPF の Key のビット。
    static inline std::uint32_t keys = 0;
    static inline bool touching = false;
    static inline unsigned touchX = 0, touchY = 0;
    static inline int nowMs = 0;
    // 次の起動に持ち越す物（GohanData・Task の代役）。
    static inline std::vector<std::uint8_t> gohanFile; // 設定ファイルの gohan の欄（空なら無い）
    static inline int gohanLoads = 0, gohanSaves = 0, sleeps = 0, taskStarts = 0;
    static inline bool saveFails = false; // 書き込みに失敗する（書き手の写しは作れても、欄は残す）
    static inline void (*sleepHook)() = nullptr; // Sleep のたびに呼ぶ（待ちの間に別のスレッドが進むことを模す）
    static inline bool taskHold = false;  // 作業を予約だけして、RunHeldTask まで動かさない（作業のスレッドを模す）
    static inline std::shared_ptr<FakeTaskState> heldTask;
    // 読み込み（GohanData::Load）の入口で試験が呼ぶ。毎フレームの処理の登録より前かを確かめる。
    static inline void (*onLoad)() = nullptr;
    static inline int keyReads = 0; // ボタンを読んだ回数（毎フレームの処理が動いたかを数える）
    // 予約された作業を、呼び手のスレッドで動かす（作業のスレッドが動いた時を模す）。
    static void RunHeldTask();
};
enum Key {
    A = 1,
    B = 1 << 1,
    Select = 1 << 2,
    Start = 1 << 3,
    DPadRight = 1 << 4,
    DPadLeft = 1 << 5,
    DPadUp = 1 << 6,
    DPadDown = 1 << 7,
    R = 1 << 8,
    L = 1 << 9,
    X = 1 << 10,
    Y = 1 << 11,
    ZL = 1 << 14,
    ZR = 1 << 15,
};
struct Controller {
    static std::uint32_t GetKeysDown(bool = true) {
        ++Fake::keyReads;
        return Fake::keys;
    }
};
struct Point {
    unsigned x, y;
};
struct Touch {
    static bool IsDown() { return Fake::touching; }
    static Point GetPosition() { return {Fake::touchX, Fake::touchY}; }
};
struct Time {
    int ms;
    int AsMilliseconds() const { return ms; }
};
constexpr Time Milliseconds(int amount) { return Time{amount}; }
// 待つ間も時計は進む（実機の 1 ms の待ちを模す）。
inline void Sleep(Time time) {
    Fake::nowMs += time.ms;
    ++Fake::sleeps;
    if (Fake::sleepHook) Fake::sleepHook();
}
struct Clock {
    Time GetElapsedTime() const { return {Fake::nowMs}; }
};
struct Process {
    using OnPauseResumeCallback = void (*)(bool);
    static inline OnPauseResumeCallback OnPauseResume = nullptr;
    static bool IsPaused() { return Fake::paused; }
    static void Pause() {
        if (OnPauseResume) OnPauseResume(true);
        Fake::paused = true;
        ++Fake::pauseCalls;
    }
    static void Play() {
        if (OnPauseResume) OnPauseResume(false);
        Fake::paused = false;
        ++Fake::playCalls;
    }
    static std::uint64_t GetTitleID() { return Fake::title; }
    static bool Read8(std::uintptr_t address, std::uint8_t &byte) {
        assert(Fake::paused);
        ++Fake::reads;
        if (!Fake::readable || address == Fake::unreadable)
            return false;
        for (const auto &[base, data] : Fake::memory) {
            if (address >= base && address - base < data.size()) {
                byte = static_cast<std::uint8_t>(data.at(address - base));
                return true;
            }
        }
        byte = static_cast<std::uint8_t>(Fake::bytes.at(address - 512));
        return true;
    }
};
// 作業（Task）の代役。Start は同じスレッドで直ちに動かす。Fake::taskHold が真の時は予約だけして、
// Start の二度目は動作中の -1 を返す（CTRPF の Scheduler と同じ答え）。
// 状態の値は実機の Task::Status と同じ。Wait は代役に無い（実機の Wait は、作業が Scheduled のまま
// 残ると戻らないため、gohan は Status を上限つきで見る）。
class Task {
  public:
    enum Affinity { AppCore = 1 << 0, SysCore = 1 << 1, NewAppCore = 1 << 2 };
    // enum Status は Status の関数と名前が重なるので、値だけを静的な定数で持つ。
    static constexpr std::uint32_t Idle = 0, Scheduled = 1, Processing = 2, Finished = 4;
    explicit Task(std::int32_t (*func)(void *), void *arg = nullptr, std::int32_t affinity = -1)
        : state_(std::make_shared<FakeTaskState>()) {
        state_->func = func;
        state_->arg = arg;
        affinity_ = affinity;
    }
    int Start() const {
        if (state_->flags == Scheduled || state_->flags == Processing) return -1;
        ++Fake::taskStarts;
        state_->flags = Scheduled;
        if (Fake::taskHold) {
            Fake::heldTask = state_; // 作業のスレッドが動くまで Scheduled のまま残る（Fake::RunHeldTask）
            return 0;
        }
        state_->flags = Processing;
        state_->result = state_->func(state_->arg);
        state_->flags = Finished;
        return 0;
    }
    std::uint32_t Status() const { return state_->flags; }

  private:
    std::shared_ptr<FakeTaskState> state_;
    std::int32_t affinity_ = -1;
};

// 設定ファイルの gohan の欄の代役。書き手と読み手は実機と同じ約束（空の写しは失敗、欄が無ければ大きさ 0）。
class GohanData {
  public:
    using WriteFn = void (*)(std::vector<std::uint8_t> &out);
    using ReadFn = void (*)(const std::uint8_t *data, std::uint32_t size);
    static inline const char *FileName = "GohanCTRPFData.bin";
    static constexpr std::uint32_t Magic = 0x4E484F47;

    static void SetHandlers(WriteFn writer, ReadFn reader) {
        writer_ = writer;
        reader_ = reader;
    }
    static bool Load() {
        ++Fake::gohanLoads;
        if (Fake::onLoad) Fake::onLoad();
        const bool found = !Fake::gohanFile.empty();
        if (reader_) reader_(found ? Fake::gohanFile.data() : nullptr, found ? static_cast<std::uint32_t>(Fake::gohanFile.size()) : 0);
        return found;
    }
    static bool Save() {
        ++Fake::gohanSaves;
        if (!writer_) return true;
        std::vector<std::uint8_t> out;
        writer_(out);
        if (out.empty() || Fake::saveFails) return false; // 空の写しや書き込みの失敗は、既存の欄を残す
        Fake::gohanFile = out;
        return true;
    }

  private:
    static inline WriteFn writer_ = nullptr;
    static inline ReadFn reader_ = nullptr;
};

inline void Fake::RunHeldTask() {
    const auto held = heldTask;
    heldTask.reset();
    if (!held || held->flags != 1) return;
    held->flags = 2;
    held->result = held->func(held->arg);
    held->flags = 4;
}

class Hook {
  public:
    bool IsEnabled() const { return enabled_; }
    void Initialize(std::uintptr_t address, std::uintptr_t callback) {
        assert(callback && Fake::paused);
        address_ = address;
        Fake::hooks[address_] = {callback, HOOK_DEFAULT_PARAMS, false};
        ++Fake::initializeCalls;
    }
    HookResult Enable() {
        assert(Fake::paused);
        ++Fake::enableCalls;
        const auto result = address_ == Fake::failEnable ? HookResult::Failed : Fake::enableResult;
        enabled_ = result == HookResult::Success;
        Fake::hooks[address_].enabled = enabled_;
        return result;
    }
    HookResult Disable() {
        assert(Fake::paused);
        ++Fake::disableCalls;
        enabled_ = false;
        if (address_) Fake::hooks[address_].enabled = false;
        return HookResult::Success;
    }
    Hook &SetFlags(std::uint32_t flags) { Fake::hooks[address_].flags = flags; return *this; }

  private:
    bool enabled_ = false;
    std::uintptr_t address_ = 0;
};
} // namespace CTRPluginFramework

inline std::uint64_t svcGetSystemTick() { return ++CTRPluginFramework::Fake::ticks; }
inline void *getThreadLocalStorage() { return reinterpret_cast<void *>(CTRPluginFramework::Fake::thread); }

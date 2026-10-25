#pragma once
#include <cassert>
#include <cstdint>
#include <string>
#include <map>

// 実機やゲームのデータに触らない、フック導入の境界だけの代役。
namespace CTRPluginFramework {
enum class HookResult { Success, Failed };
enum HookFlags { USE_LR_TO_RETURN = 1, EXECUTE_OI_BEFORE_CB = 2, EXECUTE_OI_AFTER_CB = 4,
                 HOOK_DEFAULT_PARAMS = USE_LR_TO_RETURN | EXECUTE_OI_BEFORE_CB };
struct FakeHook { std::uintptr_t callback = 0; std::uint32_t flags = 0; bool enabled = false; };
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
    static std::uint32_t GetKeysDown(bool = true) { return Fake::keys; }
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

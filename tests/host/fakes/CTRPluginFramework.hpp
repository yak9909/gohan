#pragma once
#include <cassert>
#include <cstdint>
#include <string>

// 実機やゲームのデータに触らない、フック導入の境界だけの代役。
namespace CTRPluginFramework {
enum class HookResult { Success, Failed };
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
    static bool IsPaused() { return Fake::paused; }
    static void Pause() {
        Fake::paused = true;
        ++Fake::pauseCalls;
    }
    static void Play() {
        Fake::paused = false;
        ++Fake::playCalls;
    }
    static std::uint64_t GetTitleID() { return Fake::title; }
    static bool Read8(std::uintptr_t address, std::uint8_t &byte) {
        assert(Fake::paused);
        ++Fake::reads;
        if (!Fake::readable)
            return false;
        byte = static_cast<std::uint8_t>(Fake::bytes.at(address - 512));
        return true;
    }
};
class Hook {
  public:
    bool IsEnabled() const { return enabled_; }
    void Initialize(std::uintptr_t address, std::uintptr_t callback) {
        assert(address == 512 && callback && Fake::paused);
        ++Fake::initializeCalls;
    }
    HookResult Enable() {
        assert(Fake::paused);
        ++Fake::enableCalls;
        enabled_ = Fake::enableResult == HookResult::Success;
        return Fake::enableResult;
    }
    HookResult Disable() {
        assert(Fake::paused);
        ++Fake::disableCalls;
        enabled_ = false;
        return HookResult::Success;
    }

  private:
    bool enabled_ = false;
};
} // namespace CTRPluginFramework

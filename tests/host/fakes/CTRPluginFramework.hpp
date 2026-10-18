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

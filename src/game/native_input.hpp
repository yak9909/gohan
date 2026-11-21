#pragma once
#include "game/write_log.hpp"
#include <array>
#include <cstdint>

namespace gohan::game {
// ゲームの ABI と独立した入力履歴。次の呼び出しへ持ち越す。
struct NativeTouch {
    bool down = false, previous = false;
    int x = 0, y = 0, start_x = 0, start_y = 0;
    int previous_x = 0, previous_y = 0, previous_start_x = 0, previous_start_y = 0;
    unsigned held_frames = 0, released_frames = 200, previous_held_frames = 0, previous_released_frames = 200;
    bool changed = false, previous_changed = false;
    int raw_x = -1, raw_y = -1, previous_raw_y = -1;
    void Step(int px, int py, bool held);
};
struct NativeInputTargets {
    std::array<std::uint8_t, 40> *touch = nullptr;
    unsigned int *pressed = nullptr, *released = nullptr, *pointer_flags = nullptr, *held = nullptr;
    float *x = nullptr, *y = nullptr;
    unsigned int *menu_trig = nullptr, *menu_repeat = nullptr;
};
NativeInputTargets GameNativeInputTargets();
std::array<std::uint8_t, 40> EncodeNativeTouch(const NativeTouch &touch);
class NativeInputInjection {
  public:
    static constexpr unsigned Capacity = 16;
    NativeInputInjection() = default;
    NativeInputInjection(const NativeInputInjection &) = delete;
    NativeInputInjection &operator=(const NativeInputInjection &) = delete;
    bool Begin(const NativeTouch &touch, std::uint32_t trig = 0, std::uint32_t repeat = 0);
    bool Begin(const NativeInputTargets &, const NativeTouch &, std::uint32_t trig, std::uint32_t repeat);
    bool Put(std::uintptr_t *address, std::uintptr_t value) { return log_.Write(address, value); }
    void End() { log_.Restore(); }
    ~NativeInputInjection() { End(); }
  private:
    WriteLog<Capacity> log_;
};
} // namespace gohan::game

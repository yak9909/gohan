#include "game/native_input.hpp"
#include "game/generated/api.hpp"
#include <cstring>

namespace gohan::game {
std::array<std::uint8_t, 40> EncodeNativeTouch(const NativeTouch &v) {
    generated::AcnlTouchState t{};
    t.held = v.down; t.changed = v.changed;
    t.raw_x = v.raw_x; t.raw_y = v.raw_y;
    t.start_x = v.start_x; t.start_y = v.start_y;
    t.position_x = v.x; t.position_y = v.y;
    t.held_frames = v.held_frames; t.released_frames = v.released_frames;
    t.previous_start_x = v.previous_start_x; t.previous_start_y = v.previous_start_y;
    t.previous_position_x = v.previous_x; t.previous_position_y = v.previous_y;
    t.previous_held_frames = v.previous_held_frames; t.previous_released_frames = v.previous_released_frames;
    t.previous_held = v.previous; t.previous_changed = v.previous_changed;
    t.previous_raw_y = v.previous_raw_y;
    t.layout_x = float(v.x) - 160; t.layout_y = 120 - float(v.y);
    std::array<std::uint8_t, 40> bytes{};
    static_assert(sizeof(t) == sizeof(bytes));
    std::memcpy(bytes.data(), &t, sizeof(t));
    return bytes;
}
NativeInputTargets GameNativeInputTargets() {
    using namespace generated;
    NativeInputTargets t;
    t.touch = reinterpret_cast<std::array<std::uint8_t, 40> *>(g_TouchState_addr);
    const auto *mgr = *reinterpret_cast<g_SeadControllerMgr_t *>(g_SeadControllerMgr_addr);
    if (mgr && mgr->controller_count && mgr->controllers && mgr->controllers[0]) {
        auto &c = mgr->controllers[0]->base;
        t.pressed = &c.pressed; t.released = &c.released;
        t.pointer_flags = &c.pointer_flags; t.held = &c.held;
        t.x = &c.pixel_float.x; t.y = &c.pixel_float.y;
    }
    auto *menu = *reinterpret_cast<g_BsMenuMgr_t *>(g_BsMenuMgr_addr);
    if (menu) {
        // メニューの写しはコントローラーとビット配列が異なる。呼び手は変換済みのキーを渡す。
        auto *wrapper = reinterpret_cast<SeadControllerWrapperPrefix *>(static_cast<std::uint8_t *>(menu) + 68);
        t.menu_trig = &wrapper->base.pressed;
        t.menu_repeat = &wrapper->base.repeat;
    }
    return t;
}
bool NativeInputInjection::Begin(const NativeTouch &v, std::uint32_t trig, std::uint32_t repeat) {
    return Begin(GameNativeInputTargets(), v, trig, repeat);
}
} // namespace gohan::game

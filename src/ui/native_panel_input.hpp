#pragma once
#include "ui/input/buttons.hpp"

// 同じ毎フレームのスレッドで、メニューが先に遮断を宣言し、一覧が後で読む。
namespace gohan::ui::native_panels {
class InputGate {
  public:
    void Block(bool blocked) { blocked_ = blocked; }
    bool Blocked() const { return blocked_; }
    input::Snapshot Filter(input::Snapshot pad, bool external_block = false) {
        if (blocked_ || external_block) {
            ignored_ = pad.buttons;
            ignored_touch_ = pad.touching;
            return {};
        }
        ignored_ &= pad.buttons;
        ignored_touch_ = ignored_touch_ && pad.touching;
        pad.buttons &= static_cast<std::uint16_t>(~ignored_);
        if (ignored_touch_) pad.touching = false;
        return pad;
    }
  private:
    bool blocked_ = false;
    bool ignored_touch_ = false;
    std::uint16_t ignored_ = 0;
};
inline InputGate &PanelInput() {
    static InputGate gate;
    return gate;
}
} // namespace gohan::ui::native_panels

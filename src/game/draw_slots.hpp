#pragma once
#include <cstdint>
namespace gohan::game::draw {
struct Slots {
    std::uintptr_t *first = nullptr, *second = nullptr;
    const void *owner = nullptr;
    const void *scene = nullptr;
};
class Pair {
  public:
    bool Install(Slots slots, std::uintptr_t original0, std::uintptr_t original1,
                 std::uintptr_t wrapper0, std::uintptr_t wrapper1) {
        if (!slots.first || !slots.second || slots.first == slots.second || !slots.owner) return false;
        if (slots_.owner) {
            if (!Matches(slots) || (*slots.first != wrap0_ && *slots.first != old0_) ||
                (*slots.second != wrap1_ && *slots.second != old1_)) return false;
            *slots.first = wrap0_; *slots.second = wrap1_; return true;
        }
        if (*slots.first != original0 || *slots.second != original1) return false;
        slots_ = slots; old0_ = original0; old1_ = original1; wrap0_ = wrapper0; wrap1_ = wrapper1;
        *slots.first = wrapper0; *slots.second = wrapper1;
        return true;
    }
    // 後から包んだ相手が外れて自分の値が戻るまで保留する。相手の値は上書きしない。
    bool Restore() {
        if (!slots_.owner) return true;
        if (*slots_.first == wrap0_) *slots_.first = old0_;
        if (*slots_.second == wrap1_) *slots_.second = old1_;
        if (*slots_.first != old0_ || *slots_.second != old1_) return false;
        Abandon(); return true;
    }
    void Abandon() { slots_ = {}; }
    const void *Owner() const { return slots_.owner; }
    bool Matches(Slots slots) const {
        return slots_.owner == slots.owner && slots_.scene == slots.scene &&
            slots_.first == slots.first && slots_.second == slots.second;
    }
  private:
    Slots slots_;
    std::uintptr_t old0_ = 0, old1_ = 0, wrap0_ = 0, wrap1_ = 0;
};
using Callback = void (*)(std::uintptr_t, std::uintptr_t);
Slots CurrentSlots();
Callback Original0();
Callback Original1();
bool Wire();
void Unwire();
} // namespace gohan::game::draw

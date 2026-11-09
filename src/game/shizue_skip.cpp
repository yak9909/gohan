#include "game/shizue_skip.hpp"

namespace gohan::game::shizue_skip {
bool Sequence::Step(bool enabled) {
    if (access_.room() != 0x63) { Reset(); return false; }
    if (!enabled || fired_) { stable_ = 0; return false; }
    Exit exit;
    if (!access_.resolve(exit)) { stable_ = 0; return false; }
    if (stable_ < 30) ++stable_;
    if (stable_ != 30) return false;
    if (!access_.prepared()) access_.prepare();
    if (!access_.prepared() || !access_.resolve(exit)) { stable_ = 0; return false; }
    access_.use_exit(exit);
    fired_ = true;
    return true;
}
} // namespace gohan::game::shizue_skip

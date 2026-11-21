#include "game/shizue_skip.hpp"
#include <limits>

namespace gohan::game::shizue_skip {
bool ResolveExit(const ExitAccess &access, Exit &exit) {
    void *const save = access.save();
    if (!save || !access.readable(reinterpret_cast<std::uintptr_t>(save), 4)) return false;
    const auto garden = reinterpret_cast<std::uintptr_t>(access.garden());
    if (!garden || garden > std::numeric_limits<std::uintptr_t>::max() - 0xA0) return false;
    const auto array = garden + 0xA0;
    if (!access.readable(array, 4)) return false;
    const auto slot = access.slot(reinterpret_cast<void *>(array), save);
    if (slot < 0 || slot > 3) return false;
    void *const manager = access.manager();
    // 部屋管理は固定領域にある。ヒープ内という条件を課すと常に失敗する。
    if (!manager || !access.readable(reinterpret_cast<std::uintptr_t>(manager), 4)) return false;
    exit = {manager, slot};
    return true;
}
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

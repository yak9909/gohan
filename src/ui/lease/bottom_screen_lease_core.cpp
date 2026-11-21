#include "ui/lease/bottom_screen_lease.hpp"
namespace gohan::ui {
bool BottomScreenLease::SetCleanup(unsigned owner, Cleanup cleanup) {
    if (owner >= 32 || owners_ || pending_) return false;
    cleanup_[owner] = cleanup; return true;
}
void BottomScreenLease::Acquire(std::uint32_t owners, bool over_chat) {
    if (stopping_) return;
    owners_ |= owners; pending_ |= owners;
    if (over_chat) chat_ |= owners; else chat_ &= ~owners;
}
void BottomScreenLease::Release(std::uint32_t owners) { owners_ &= ~owners; chat_ &= ~owners; }
void BottomScreenLease::ResourcesReturned(std::uint32_t owners) { pending_ &= ~owners; }
void BottomScreenLease::RequestStop() {
    stopping_ = true; owners_ = chat_ = 0; machine_.Shutdown();
}
LeaseCommand BottomScreenLease::Step(const game::BottomScreenSnapshot &s) {
    for (unsigned i = 0; i < 32; ++i) {
        const auto bit = std::uint32_t(1) << i;
        if ((pending_ & bit) && !(owners_ & bit) && cleanup_[i] && cleanup_[i]()) pending_ &= ~bit;
    }
    LeaseProbe p;
    p.room = s.room; p.owners = owners_ & ~chat_;
    p.tab_settled = s.available && s.tab_settled;
    p.commands_consumed = s.commands_consumed;
    p.map_present = s.map_present; p.other_present = s.other_present;
    p.menu_open = s.menu_open; p.menu_entered = s.menu_entered; p.menu_gone = s.menu_gone;
    p.hide_done = p.tab_settled && p.commands_consumed && !s.tabs_present && !s.map_present && !s.other_present;
    p.game_restored = s.tabs_present || s.map_present || s.other_present;
    // 部屋メニューが地図の代わりになる部屋もある。共有する入場完了通知で確認する。
    p.restore_done = p.tab_settled && p.commands_consumed && s.tabs_present && s.map_enter_done;
    p.resources_returned = !pending_;
    return machine_.Step(p);
}
LeaseStatus BottomScreenLease::Status() const {
    return {machine_.Stage(), machine_.Requested(), machine_.Hidden(), machine_.Restoring(),
            machine_.Restored(), !pending_ && machine_.Drained(), stopping_};
}
} // namespace gohan::ui

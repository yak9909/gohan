// 公開要求・フレーム接続・終了待ちを、ゲームの更新の代役で通す。
#include "ui/lease/bottom_screen_lease.hpp"
#include "core/frame.hpp"
#include <cassert>
#include <cstdio>
using namespace gohan;
namespace {
game::BottomScreenSnapshot image;
unsigned hides = 0, restores = 0, reads = 0, waits = 0, after_restore = 0;
bool finish_restore = false;
void Tick() { core::frame::Dispatch(); }
}
namespace gohan::game {
BottomScreenSnapshot ReadBottomScreen() { ++reads; return image; }
void HideBottomScreen() { ++hides; }
void RestoreBottomScreen() {
    ++restores; image.tab_settled = false; image.map_enter_done = false;
}
void BeginBottomMenuClose() { assert(false); }
void FinishBottomMenuClose() { assert(false); }
void BottomScreenWait(unsigned) {
    ++waits;
    if (restores && ++after_restore >= 4 && finish_restore) {
        image.tab_settled = image.tabs_present = image.map_present = image.map_enter_done = true;
    }
    Tick();
}
}
int main() {
    namespace lease = ui::bottom_screen_lease;
    image.available = image.tab_settled = image.commands_consumed = image.tab_command_consumed = true;
    image.map_present = image.tabs_present = image.map_enter_done = true;
    assert(lease::Wire());
    // フレーム間の連打は最後の所有状態で処理する。
    assert(lease::Acquire(1)); assert(lease::Release(1)); assert(lease::ResourcesReturned(1));
    Tick(); assert(hides == 0);
    assert(lease::Acquire(1)); assert(lease::Release(1)); assert(lease::Acquire(1));
    Tick(); assert(hides == 1);
    image.map_present = image.tabs_present = false;
    Tick(); Tick(); assert(lease::Status().hidden);
    // 復帰が終わらない時は期限でfalse。フレーム接続は残す。
    assert(lease::Release(1)); assert(lease::ResourcesReturned(1));
    assert(!lease::Shutdown());
    assert(restores == 1 && after_restore > 2 && !lease::Status().drained);
    const auto before = reads;
    Tick(); assert(reads == before + 1);
    // 再度の終了要求で復帰完了まで進める。命令2は追加しない。
    finish_restore = true;
    assert(lease::Shutdown());
    assert(restores == 1 && lease::Status().drained && waits > 4);
    const auto drained = reads;
    Tick(); assert(reads == drained);
    assert(!lease::Acquire(1));
    std::puts("test_bottom_screen_lease: OK");
}

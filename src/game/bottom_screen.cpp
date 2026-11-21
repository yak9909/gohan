#include "game/bottom_screen.hpp"
#include "game/generated/api.hpp"
#include <3ds.h>
namespace gohan::game {
namespace {
using namespace generated;
#define CALL(name) reinterpret_cast<name##_t>(name##_addr)
template <class T> T &At(void *object, unsigned offset) {
    return *reinterpret_cast<T *>(static_cast<std::uint8_t *>(object) + offset);
}
template <class T> T &Global(std::uintptr_t address) { return *reinterpret_cast<T *>(address); }
void *Manager() { return Global<g_BsMenuMgr_t>(g_BsMenuMgr_addr); }
void *Tab(void *mgr) { return mgr ? At<void *>(mgr, 480) : nullptr; }
std::uint32_t &Flags() { return Global<std::uint32_t>(vc_MENU_FLAGS_addr); }
void Change(void *tab, std::uintptr_t calc) { CALL(BsMenuTab_ChangeState)(static_cast<std::uint8_t *>(tab) + 20, calc, 0); }
}
BottomScreenSnapshot ReadBottomScreen() {
    BottomScreenImage s;
    s.room = CALL(Room_GetCurrentId)();
    s.flags = Flags();
    s.menu_state = Global<g_MenuOpenState_t>(g_MenuOpenState_addr);
    s.screen_list_empty = !Global<g_MenuScreenListHead_t>(g_MenuScreenListHead_addr);
    auto *mgr = Manager(); auto *tab = Tab(mgr);
    s.manager = mgr != nullptr; s.tab = tab != nullptr;
    if (mgr && tab) {
        s.chat_window = At<void *>(mgr, 492) != nullptr;
        s.map = At<void *>(mgr, 484); s.other = At<void *>(mgr, 488);
        s.shown = At<std::uint16_t>(tab, 6578); s.moving = At<std::uint16_t>(tab, 6580);
        s.calc = At<std::uint32_t>(tab, 32); s.calc_adjustor = At<std::uint32_t>(tab, 36);
        s.tab_command = Global<g_BsMenuTabCommand_t>(g_BsMenuTabCommand_addr);
        s.map_command = Global<g_BsMenuMapCommand_t>(g_BsMenuMapCommand_addr);
        s.other_command = Global<g_BsMenuSubscreenCommand_t>(g_BsMenuSubscreenCommand_addr);
    }
    return DecodeBottomScreen(s);
}
void HideBottomScreen() {
    auto *mgr = Manager();
    if (!mgr) return;
    HideBottomScreen({
        &Global<g_BsMenuTabCommand_t>(g_BsMenuTabCommand_addr),
        &Global<g_BsMenuMapCommand_t>(g_BsMenuMapCommand_addr),
        &Global<g_BsMenuSubscreenCommand_t>(g_BsMenuSubscreenCommand_addr),
        At<void *>(mgr, 484) != nullptr, At<void *>(mgr, 488) != nullptr});
}
void RestoreBottomScreen() { Global<g_BsMenuTabCommand_t>(g_BsMenuTabCommand_addr) = 2; }
void BeginBottomMenuClose() {
    auto *mgr = Manager(); auto *tab = Tab(mgr);
    if (!tab) return;
    const int selected = At<std::int8_t>(tab, 6427);
    for (unsigned i = 0; i < 11; ++i) {
        auto *button = static_cast<std::uint8_t *>(tab) + 1948 + 388 * i;
        if (int(i) != selected && !At<std::uint8_t>(button, 384)) continue;
        At<std::uint8_t>(button, 217) = 1;
        At<std::uint32_t>(button, 204) = 0x010003F2;
        At<std::uint8_t>(button, 218) = 1;
        At<std::uint8_t>(button, 384) = At<std::uint8_t>(button, 385) = 0;
        auto *vt = At<std::uintptr_t *>(button, 0);
        reinterpret_cast<void (*)(void *, unsigned)>(vt[7])(button, 0);
    }
    At<std::uint8_t>(tab, 6427) = 10;
    if (At<std::uint8_t>(mgr, 64) == 1) {
        if (auto *hand = At<void *>(mgr, 500)) CALL(BsHandCursor_Hide)(hand);
        At<std::uint8_t>(mgr, 64) = 0;
    }
    Flags() |= MenuFlags_CloseRequested;
    At<std::uint16_t>(tab, 6576) |= 1;
    At<std::uint8_t>(tab, 6422) = 0;
    Change(tab, BsMenuTab_WaitUpdate_addr);
}
void FinishBottomMenuClose() {
    auto *mgr = Manager(); auto *tab = Tab(mgr);
    if (!tab) return;
    Global<g_BsMenuTabCloseSelectionFlag_t>(g_BsMenuTabCloseSelectionFlag_addr) = 0;
    if (auto *bg = Global<g_BsMenuBg_t>(g_BsMenuBg_addr)) CALL(BsMenuBg_StartLeave)(bg, 10);
    At<std::uint8_t>(tab, 4493) = At<std::uint8_t>(tab, 4881) = 1;
    if (CALL(MenuFlow_IsCurrentKind)(7, false)) CALL(MenuFlow_SignalKind7Complete)();
    else if (CALL(MenuFlow_IsCurrentKind)(13, false)) CALL(MenuFlow_SignalKind13Complete)();
    Flags() = (Flags() & ~(MenuFlags_MapEnterStarted | MenuFlags_MapEnterDone |
                          MenuFlags_MapOutDone | MenuFlags_CloseRequested)) | MenuFlags_MapOutDone;
    At<std::uint8_t>(tab, 6422) = 1;
    At<std::uint8_t>(tab, 6427) = 0xFF;
    Change(tab, BsMenuTab_TabSelectUpdate_addr);
}
void BottomScreenWait(unsigned ms) { svcSleepThread(std::int64_t(ms) * 1000000); }
#undef CALL
} // namespace gohan::game

#include "game/bottom_screen.hpp"
#include "game/generated/api.hpp"
namespace gohan::game {
BottomScreenSnapshot DecodeBottomScreen(const BottomScreenImage &s) {
    using namespace generated;
    BottomScreenSnapshot p;
    p.room = s.room;
    p.menu_open = s.menu_state != 1 || (s.flags & MenuFlags_EnterStarted);
    p.menu_entered = (s.flags & (MenuFlags_EnterStarted | MenuFlags_EnterDone)) ==
        (MenuFlags_EnterStarted | MenuFlags_EnterDone);
    p.menu_gone = s.menu_state == 1 && s.screen_list_empty && !(s.flags & MenuFlags_EnterStarted);
    p.map_enter_done = s.flags & MenuFlags_MapEnterDone;
    if (!s.manager || !s.tab) return p;
    p.available = true;
    p.map_present = s.map; p.other_present = s.other; p.tabs_present = s.shown != 0;
    p.tab_settled = s.calc == BsMenuTab_TabSelectUpdate_addr && !s.calc_adjustor && !s.moving;
    p.commands_consumed = s.tab_command == 11 && s.map_command == 2 && s.other_command == 2;
    return p;
}
void HideBottomScreen(const BottomScreenCommands &c) {
    if (!c.tab) return;
    if (c.map && c.map_present) *c.map = 0;
    if (c.other && c.other_present) *c.other = 0;
    *c.tab = 7;
}
} // namespace gohan::game

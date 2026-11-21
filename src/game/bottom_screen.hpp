#pragma once
#include <cstdint>
namespace gohan::game {
struct BottomScreenSnapshot {
    std::uint32_t room = 0;
    bool chat_over = false;
    bool available = false, tab_settled = false, commands_consumed = false;
    bool map_present = false, other_present = false, tabs_present = false;
    bool menu_open = false, menu_entered = false, menu_gone = false, map_enter_done = false;
};
// PC 試験でも同じ解釈を使うため、読む値と判断を分ける。
struct BottomScreenImage {
    std::uint32_t room = 0, flags = 0, calc_adjustor = 0;
    std::uintptr_t calc = 0;
    std::uint16_t shown = 0, moving = 0;
    std::uint8_t menu_state = 1, tab_command = 11, map_command = 2, other_command = 2;
    bool manager = false, tab = false, map = false, other = false, screen_list_empty = true;
    bool chat_window = false;
};
struct BottomScreenCommands {
    std::uint8_t *tab = nullptr, *map = nullptr, *other = nullptr;
    bool map_present = false, other_present = false;
};
BottomScreenSnapshot DecodeBottomScreen(const BottomScreenImage &);
void HideBottomScreen(const BottomScreenCommands &);
BottomScreenSnapshot ReadBottomScreen();
void HideBottomScreen();
void RestoreBottomScreen();
void BeginBottomMenuClose();
void FinishBottomMenuClose();
void BottomScreenWait(unsigned milliseconds);
} // namespace gohan::game

#pragma once
#include "game/input_hook.hpp"

namespace gohan::game::player_motion {
struct Direction { float x = 0, z = 0; };
Direction ReadDirection(const InputSnapshot &, unsigned key, unsigned hotkey);
int Round8(float value);
unsigned DirectionBits(int x, int z);
float GridCenter(int cell);
bool TouchCoordinates(unsigned room, bool town_opened, int x, int y, Direction &world);
bool MapCoordinates(int x, int y, Direction &world);
bool Wire();
} // namespace gohan::game::player_motion

#pragma once
#include "game/patch_set.hpp"

namespace gohan::game::cheats {
enum class Patch { Wall, Flower, Trap, BedHead, Trample, Dig, LookUp, Shop, Text, Fps, Tree, Dig3, Weather };
Result Wire(Patch id);
Result Set(Patch id, bool on);
bool IsApplied(Patch id);
void Shutdown(Patch id);
bool ReadWeather(std::int32_t *value);
Result WriteWeather(std::int32_t value);
} // namespace gohan::game::cheats

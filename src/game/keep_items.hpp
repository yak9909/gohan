#pragma once
#include "game/patch_set.hpp"
#include <span>

namespace gohan::game::keep_items {
bool GrowthCaller(std::uintptr_t caller, std::span<const std::uintptr_t> returns);
std::span<const std::uintptr_t> GrowthReturns();
std::uint32_t NextGrowth(std::uint32_t tile);
bool Wire();
Result Set(bool on);
bool IsActive();
void Shutdown();
} // namespace gohan::game::keep_items

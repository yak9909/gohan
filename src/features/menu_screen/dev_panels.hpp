#pragma once
#ifdef GOHAN_DEV
#include "menu/binding.hpp"
#include <span>

namespace gohan::features::menu_screen::dev_panels {
std::span<const menu::Bound> Bindings();
void PollCatalogDecision();
} // namespace gohan::features::menu_screen::dev_panels
#endif

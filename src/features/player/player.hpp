#pragma once
#include "core/feature.hpp"
#include "menu/binding.hpp"
#include <span>

namespace gohan::features::player {
const core::Feature &Feature();
std::span<const menu::Bound> Bindings();
} // namespace gohan::features::player

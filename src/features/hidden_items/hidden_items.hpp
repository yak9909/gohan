#pragma once
#include "core/feature.hpp"
#include "menu/binding.hpp"
#include <span>

namespace gohan::features::hidden_items {
const core::Feature &Feature();
std::span<const menu::Bound> Bindings();
}

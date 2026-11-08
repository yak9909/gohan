#pragma once
#include "game/patch_set.hpp"
#include "menu/binding.hpp"

namespace gohan::menu {
inline Result FromGame(game::Result result) {
    switch (result) {
    case game::Result::Ok: return Result::Ok;
    case game::Result::NotReady: return Result::NotReady;
    case game::Result::Unsupported: return Result::Unsupported;
    case game::Result::Failed: return Result::Failed;
    }
    return Result::Failed;
}
} // namespace gohan::menu

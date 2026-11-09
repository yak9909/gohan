#include "features/keep_items/keep_items.hpp"
#include "game/keep_items.hpp"
#include "menu/game_result.hpp"
#include "menu_ids.hpp"

namespace gohan::features::keep_items {
namespace {
menu::Result Set(bool on) { return menu::FromGame(game::keep_items::Set(on)); }
const core::Feature feature{"keep_items", game::keep_items::Wire, game::keep_items::Shutdown, core::ShutdownStage::GameHooks};
const menu::Bound bindings[] = {GOHAN_ITEM(menu::MenuId::TownKeep, menu::Patch{.set = Set, .is_active = game::keep_items::IsActive})};
}
const core::Feature &Feature() { return feature; }
std::span<const menu::Bound> Bindings() { return bindings; }
} // namespace gohan::features::keep_items

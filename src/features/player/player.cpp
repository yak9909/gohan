#include "features/player/player.hpp"
#include "game/cheats.hpp"
#include "menu/game_result.hpp"
#include "menu_ids.hpp"

namespace gohan::features::player {
namespace {
menu::Result SetWall(bool on) { return menu::FromGame(game::cheats::Set(game::cheats::Patch::Wall, on)); }
bool ActiveWall() { return game::cheats::IsApplied(game::cheats::Patch::Wall); }
menu::Result SetFlower(bool on) { return menu::FromGame(game::cheats::Set(game::cheats::Patch::Flower, on)); }
bool ActiveFlower() { return game::cheats::IsApplied(game::cheats::Patch::Flower); }
menu::Result SetTrap(bool on) { return menu::FromGame(game::cheats::Set(game::cheats::Patch::Trap, on)); }
bool ActiveTrap() { return game::cheats::IsApplied(game::cheats::Patch::Trap); }
menu::Result SetBedHead(bool on) { return menu::FromGame(game::cheats::Set(game::cheats::Patch::BedHead, on)); }
bool ActiveBedHead() { return game::cheats::IsApplied(game::cheats::Patch::BedHead); }

bool Wire() {
    // 項目ごとに対応可否を持つ。一項目の照合失敗でほかの項目を止めない。
    game::cheats::Wire(game::cheats::Patch::Wall);
    game::cheats::Wire(game::cheats::Patch::Flower);
    game::cheats::Wire(game::cheats::Patch::Trap);
    game::cheats::Wire(game::cheats::Patch::BedHead);
    return true;
}
void Shutdown() {
    game::cheats::Shutdown(game::cheats::Patch::Wall);
    game::cheats::Shutdown(game::cheats::Patch::Flower);
    game::cheats::Shutdown(game::cheats::Patch::Trap);
    game::cheats::Shutdown(game::cheats::Patch::BedHead);
}
const menu::Bound bindings[] = {
    GOHAN_ITEM(menu::MenuId::PlayerWall, menu::Patch{.set = SetWall, .is_active = ActiveWall}),
    GOHAN_ITEM(menu::MenuId::PlayerFlower, menu::Patch{.set = SetFlower, .is_active = ActiveFlower}),
    GOHAN_ITEM(menu::MenuId::PlayerTrap, menu::Patch{.set = SetTrap, .is_active = ActiveTrap}),
    GOHAN_ITEM(menu::MenuId::PlayerBedHead, menu::Patch{.set = SetBedHead, .is_active = ActiveBedHead}),
};
const core::Feature feature{"player", Wire, Shutdown, core::ShutdownStage::Other};
} // namespace
const core::Feature &Feature() { return feature; }
std::span<const menu::Bound> Bindings() { return bindings; }
} // namespace gohan::features::player

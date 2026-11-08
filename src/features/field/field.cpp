#include "features/field/field.hpp"
#include "game/cheats.hpp"
#include "menu/game_result.hpp"
#include "menu_ids.hpp"

namespace gohan::features::field {
namespace {
menu::Result SetTrample(bool on) { return menu::FromGame(game::cheats::Set(game::cheats::Patch::Trample, on)); }
bool ActiveTrample() { return game::cheats::IsApplied(game::cheats::Patch::Trample); }
menu::Result SetDig(bool on) { return menu::FromGame(game::cheats::Set(game::cheats::Patch::Dig, on)); }
bool ActiveDig() { return game::cheats::IsApplied(game::cheats::Patch::Dig); }
menu::Result SetTree(bool on) { return menu::FromGame(game::cheats::Set(game::cheats::Patch::Tree, on)); }
bool ActiveTree() { return game::cheats::IsApplied(game::cheats::Patch::Tree); }
menu::Result SetDig3(bool on) { return menu::FromGame(game::cheats::Set(game::cheats::Patch::Dig3, on)); }
bool ActiveDig3() { return game::cheats::IsApplied(game::cheats::Patch::Dig3); }

bool Wire() {
    // 項目ごとに対応可否を持つ。一項目の照合失敗でほかの項目を止めない。
    game::cheats::Wire(game::cheats::Patch::Trample);
    game::cheats::Wire(game::cheats::Patch::Dig);
    game::cheats::Wire(game::cheats::Patch::Tree);
    game::cheats::Wire(game::cheats::Patch::Dig3);
    return true;
}
void Shutdown() {
    game::cheats::Shutdown(game::cheats::Patch::Trample);
    game::cheats::Shutdown(game::cheats::Patch::Dig);
    game::cheats::Shutdown(game::cheats::Patch::Tree);
    game::cheats::Shutdown(game::cheats::Patch::Dig3);
}
const menu::Bound bindings[] = {
    GOHAN_ITEM(menu::MenuId::ItemsTrample, menu::Patch{.set = SetTrample, .is_active = ActiveTrample}),
    GOHAN_ITEM(menu::MenuId::ItemsDig, menu::Patch{.set = SetDig, .is_active = ActiveDig}),
    GOHAN_ITEM(menu::MenuId::ItemsTree, menu::Patch{.set = SetTree, .is_active = ActiveTree}),
    GOHAN_ITEM(menu::MenuId::ItemsDig3, menu::Patch{.set = SetDig3, .is_active = ActiveDig3}),
};
const core::Feature feature{"field", Wire, Shutdown, core::ShutdownStage::Other};
} // namespace
const core::Feature &Feature() { return feature; }
std::span<const menu::Bound> Bindings() { return bindings; }
} // namespace gohan::features::field

#include "features/town/town.hpp"
#include "game/cheats.hpp"
#include "menu/game_result.hpp"
#include "menu_ids.hpp"

namespace gohan::features::town {
namespace {
menu::Result SetLookUp(bool on) { return menu::FromGame(game::cheats::Set(game::cheats::Patch::LookUp, on)); }
bool ActiveLookUp() { return game::cheats::IsApplied(game::cheats::Patch::LookUp); }
bool ReadWeather(menu::s32 *value) { return game::cheats::ReadWeather(value); }
menu::Result WriteWeather(menu::s32 value) { return menu::FromGame(game::cheats::WriteWeather(value)); }

bool Wire() {
    // 項目ごとに対応可否を持つ。一項目の照合失敗でほかの項目を止めない。
    game::cheats::Wire(game::cheats::Patch::LookUp);
    game::cheats::Wire(game::cheats::Patch::Weather);
    return true;
}
void Shutdown() {
    game::cheats::Shutdown(game::cheats::Patch::LookUp);
    game::cheats::Shutdown(game::cheats::Patch::Weather);
}
const menu::Bound bindings[] = {
    GOHAN_ITEM(menu::MenuId::TownLookUp, menu::Patch{.set = SetLookUp, .is_active = ActiveLookUp}),
    GOHAN_ITEM(menu::MenuId::TownWeather, menu::GameValue{.read = ReadWeather, .write = WriteWeather}),
};
const core::Feature feature{"town", Wire, Shutdown, core::ShutdownStage::Other};
} // namespace
const core::Feature &Feature() { return feature; }
std::span<const menu::Bound> Bindings() { return bindings; }
} // namespace gohan::features::town

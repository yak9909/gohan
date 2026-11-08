#include "features/gameplay/gameplay.hpp"
#include "game/cheats.hpp"
#include "menu/game_result.hpp"
#include "menu_ids.hpp"

namespace gohan::features::gameplay {
namespace {
menu::Result SetShop(bool on) { return menu::FromGame(game::cheats::Set(game::cheats::Patch::Shop, on)); }
bool ActiveShop() { return game::cheats::IsApplied(game::cheats::Patch::Shop); }
menu::Result SetText(bool on) { return menu::FromGame(game::cheats::Set(game::cheats::Patch::Text, on)); }
bool ActiveText() { return game::cheats::IsApplied(game::cheats::Patch::Text); }
menu::Result SetFps(bool on) { return menu::FromGame(game::cheats::Set(game::cheats::Patch::Fps, on)); }
bool ActiveFps() { return game::cheats::IsApplied(game::cheats::Patch::Fps); }

bool Wire() {
    // 項目ごとに対応可否を持つ。一項目の照合失敗でほかの項目を止めない。
    game::cheats::Wire(game::cheats::Patch::Shop);
    game::cheats::Wire(game::cheats::Patch::Text);
    game::cheats::Wire(game::cheats::Patch::Fps);
    return true;
}
void Shutdown() {
    game::cheats::Shutdown(game::cheats::Patch::Shop);
    game::cheats::Shutdown(game::cheats::Patch::Text);
    game::cheats::Shutdown(game::cheats::Patch::Fps);
}
const menu::Bound bindings[] = {
    GOHAN_ITEM(menu::MenuId::GameShopsOpen, menu::Patch{.set = SetShop, .is_active = ActiveShop}),
    GOHAN_ITEM(menu::MenuId::GameText, menu::Patch{.set = SetText, .is_active = ActiveText}),
    GOHAN_ITEM(menu::MenuId::GameFps, menu::Patch{.set = SetFps, .is_active = ActiveFps}),
};
const core::Feature feature{"gameplay", Wire, Shutdown, core::ShutdownStage::Other};
} // namespace
const core::Feature &Feature() { return feature; }
std::span<const menu::Bound> Bindings() { return bindings; }
} // namespace gohan::features::gameplay

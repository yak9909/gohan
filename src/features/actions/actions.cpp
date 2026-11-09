#include "features/actions/actions.hpp"
#include "game/player_actions.hpp"
#include "menu/game_result.hpp"
#include "menu_ids.hpp"

namespace gohan::features::actions {
namespace {
bool ready = false;
menu::Result Idle() { return ready ? menu::FromGame(game::player_actions::Run(false)) : menu::Result::Unsupported; }
menu::Result Faint() { return ready ? menu::FromGame(game::player_actions::Run(true)) : menu::Result::Unsupported; }
bool Wire() { ready = game::player_actions::Wire(); return ready; }
void Shutdown() { ready = false; }
const core::Feature feature{"actions", Wire, Shutdown, core::ShutdownStage::Other};
const menu::Bound bindings[] = {
    GOHAN_ITEM(menu::MenuId::PlayerIdle, menu::Trigger{.run = Idle}),
    GOHAN_ITEM(menu::MenuId::PlayerFaint, menu::Trigger{.run = Faint}),
};
}
const core::Feature &Feature() { return feature; }
std::span<const menu::Bound> Bindings() { return bindings; }
} // namespace gohan::features::actions

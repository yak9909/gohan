#include "features/shizue_skip/shizue_skip.hpp"
#include "core/frame.hpp"
#include "game/shizue_skip.hpp"
#include "game/hook_batch.hpp"
#include "menu/game_result.hpp"
#include "menu/notice.hpp"
#include "menu_ids.hpp"

namespace gohan::features::shizue_skip {
namespace {
bool notify = false;
void Step() {
    if (game::shizue_skip::Step()) notify = true;
    if (notify && menu::PostNotice({"しずえスキップ", "村へ移動しました", false})) notify = false;
}
bool Wire() {
    if (!game::shizue_skip::Wire()) return false;
    const auto result = core::frame::Global().Add(Step, 60).result;
    if (result != core::frame::Result::Queued && result != core::frame::Result::Unchanged) {
        game::shizue_skip::Shutdown(); return false;
    }
    return true;
}
void Shutdown() { game::PausedGame pause; game::shizue_skip::Shutdown(); core::frame::Global().Remove(Step); notify = false; }
menu::Result Set(bool on) { return menu::FromGame(game::shizue_skip::Set(on)); }
const core::Feature feature{"shizue_skip", Wire, Shutdown, core::ShutdownStage::GameHooks};
const menu::Bound bindings[] = {GOHAN_ITEM(menu::MenuId::GameShizue, menu::Patch{.set = Set, .is_active = game::shizue_skip::IsActive})};
}
const core::Feature &Feature() { return feature; }
std::span<const menu::Bound> Bindings() { return bindings; }
} // namespace gohan::features::shizue_skip

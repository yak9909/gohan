#include "features/hidden_items/hidden_items.hpp"
#include "game/pocket_items.hpp"
#include "game/hidden_icons.hpp"
#include "game/hook_batch.hpp"
#include "core/frame.hpp"
#include "core/request.hpp"
#include "core/signal.hpp"
#include "menu/game_result.hpp"
#include "menu/notice.hpp"
#include "menu_ids.hpp"

namespace gohan::features::hidden_items {
namespace {
core::Request<menu::s32> pending;
core::Signal ready;
bool waitingIcons = false;
void Step() {
    game::items::PollHidden();
    menu::s32 value;
    if (pending.TryTake(value)) {
        const auto result = value >= 0 && value <= 2
                                ? game::items::SetHidden(static_cast<game::items::HiddenMode>(value))
                                : game::Result::Failed;
        if (result != game::Result::Ok) {
            menu::Notice notice;
            menu::NoticeFailed(notice, "没アイテム表示", menu::FromGame(result));
            menu::PostNotice(notice);
        }
        if (result == game::Result::Ok)
            waitingIcons = value == 2;
    }
    if (waitingIcons && game::items::HiddenIconsSettled()) {
        waitingIcons = false;
        if (!game::items::HiddenIconsAvailable()) {
            const menu::Notice notice{"没アイテム表示", "アイコンが無い（名前だけ）", true};
            menu::PostNotice(notice);
        }
    }
}
void Applied(menu::s32 value) {
    // 起動時の保存の戻しはメインのスレッドから来る。ゲームの関数はここでは呼ばない。
    if (!ready.Get() || !pending.TrySubmit(value)) {
        menu::Notice notice;
        menu::NoticeFailed(notice, "没アイテム表示", ready.Get() ? menu::Result::Busy : menu::Result::Unsupported);
        menu::PostNotice(notice);
    }
}
bool Wire() {
    if (!game::items::WireHidden())
        return false;
    const auto change = core::frame::Global().Add(Step, 56);
    if (change.result != core::frame::Result::Queued && change.result != core::frame::Result::Unchanged) {
        game::items::ShutdownHidden();
        return false;
    }
    ready.Set();
    return true;
}
void Shutdown() {
    game::PausedGame pause;
    ready.Set(false);
    waitingIcons = false;
    core::frame::Global().Remove(Step);
    game::items::ShutdownHidden();
}
const core::Feature feature{"hidden_items", Wire, Shutdown, core::ShutdownStage::GameHooks};
const menu::Bound bindings[] = {GOHAN_ITEM(menu::MenuId::ItemsHidden, menu::SettingValue{.applied = Applied})};
} // namespace
const core::Feature &Feature() { return feature; }
std::span<const menu::Bound> Bindings() { return bindings; }
} // namespace gohan::features::hidden_items

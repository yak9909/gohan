#include "features/pocket_items/pocket_items.hpp"
#include "game/pocket_items.hpp"
#include "game/item_names.hpp"
#include "game/hook_batch.hpp"
#include "core/frame.hpp"
#include "core/format.hpp"
#include "menu/game_result.hpp"
#include "menu/notice.hpp"
#include "menu_ids.hpp"

namespace gohan::features::pocket_items {
namespace {
menu::u16 pending[core::limits::Jobs];
std::size_t count = 0;
void Poll() {
    auto &names = game::items::SharedNames();
    if (!count || !names.Settled()) return;
    {
        char name[64];
        names.NameUtf8(pending[0], name, sizeof(name));
        menu::Notice notice{};
        core::TextWriter(notice.title, sizeof(notice.title)).Append("ポケットアイテム");
        core::TextWriter message(notice.message, sizeof(notice.message));
        message.Append("0x").Unsigned(pending[0], 16, 4);
        if (*name) message.Append(" ").Append(name);
        message.Append(" を入れました");
        if (!menu::PostNotice(notice)) return;
    }
    for (std::size_t i = 1; i < count; ++i) pending[i - 1] = pending[i];
    --count;
}
menu::Result Run(menu::u32 input) {
    if (input > 0x7FFF) return menu::Result::Failed;
    if (count == std::size(pending)) return menu::Result::Busy;
    const auto result = game::items::AddPocket(static_cast<menu::u16>(input));
    if (result == game::Result::Ok) {
        pending[count++] = static_cast<menu::u16>(input);
        game::items::SharedNames().Request();
        Poll();
    }
    return menu::FromGame(result);
}
bool Wire() {
    if (!game::items::WirePocket()) return false;
    const auto change = core::frame::Global().Add(Poll, 40);
    if (change.result != core::frame::Result::Queued && change.result != core::frame::Result::Unchanged) {
        game::items::ShutdownPocket(); return false;
    }
    return true;
}
void Shutdown() { game::PausedGame pause; count = 0; core::frame::Global().Remove(Poll); game::items::ShutdownPocket(); }
const core::Feature feature{"pocket_items", Wire, Shutdown, core::ShutdownStage::GameHooks};
const menu::Bound bindings[] = {GOHAN_ITEM(menu::MenuId::ItemsPocket, menu::Trigger{.run_with = Run})};
}
const core::Feature &Feature() { return feature; }
std::span<const menu::Bound> Bindings() { return bindings; }
}

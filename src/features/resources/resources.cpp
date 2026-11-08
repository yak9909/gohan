#include "features/resources/resources.hpp"
#include "game/encrypted_value.hpp"
#include "menu/game_result.hpp"
#include "menu_ids.hpp"

namespace gohan::features::resources {
namespace {
bool ReadWallet(menu::s32 *value) { return game::ReadResource(game::Resource::Wallet, value); }
menu::Result WriteWallet(menu::s32 value) { return menu::FromGame(game::WriteResource(game::Resource::Wallet, value)); }
bool ReadBank(menu::s32 *value) { return game::ReadResource(game::Resource::Bank, value); }
menu::Result WriteBank(menu::s32 value) { return menu::FromGame(game::WriteResource(game::Resource::Bank, value)); }
bool ReadCoupons(menu::s32 *value) { return game::ReadResource(game::Resource::Coupons, value); }
menu::Result WriteCoupons(menu::s32 value) { return menu::FromGame(game::WriteResource(game::Resource::Coupons, value)); }
bool ReadMedals(menu::s32 *value) { return game::ReadResource(game::Resource::Medals, value); }
menu::Result WriteMedals(menu::s32 value) { return menu::FromGame(game::WriteResource(game::Resource::Medals, value)); }
bool ReadTurnips(menu::s32 *value) { return game::ReadResource(game::Resource::Turnips, value); }
menu::Result WriteTurnips(menu::s32 value) { return menu::FromGame(game::WriteResource(game::Resource::Turnips, value)); }

bool Wire() {
    // 項目ごとに対応可否を持つ。一項目の照合失敗でほかの項目を止めない。
    game::WireResources();
    return true;
}
void Shutdown() {
    game::ShutdownResources();
}
const menu::Bound bindings[] = {
    GOHAN_ITEM(menu::MenuId::PlayerWallet, menu::GameValue{.read = ReadWallet, .write = WriteWallet}),
    GOHAN_ITEM(menu::MenuId::PlayerBank, menu::GameValue{.read = ReadBank, .write = WriteBank}),
    GOHAN_ITEM(menu::MenuId::PlayerCoupons, menu::GameValue{.read = ReadCoupons, .write = WriteCoupons}),
    GOHAN_ITEM(menu::MenuId::PlayerMedals, menu::GameValue{.read = ReadMedals, .write = WriteMedals}),
    GOHAN_ITEM(menu::MenuId::GameTurnips, menu::GameValue{.read = ReadTurnips, .write = WriteTurnips}),
};
const core::Feature feature{"resources", Wire, Shutdown, core::ShutdownStage::Other};
} // namespace
const core::Feature &Feature() { return feature; }
std::span<const menu::Bound> Bindings() { return bindings; }
} // namespace gohan::features::resources

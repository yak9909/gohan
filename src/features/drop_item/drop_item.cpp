#include "features/drop_item/drop_item.hpp"
#include "menu_ids.hpp"

namespace gohan::features::drop_item {
namespace {
bool Wire() { return true; }
void Shutdown() {}
const core::Feature feature{"drop_item", Wire, Shutdown, core::ShutdownStage::Other};
const menu::Bound bindings[] = {GOHAN_ITEM(menu::MenuId::ItemsDropItem, menu::SettingValue{})};
}
const core::Feature &Feature() { return feature; }
std::span<const menu::Bound> Bindings() { return bindings; }
}

#include "features/menu_screen/dev_panels.hpp"
#ifdef GOHAN_DEV
#include "menu_ids.hpp"
#include "ui/native_panels.hpp"
#include "menu/notice.hpp"

namespace gohan::features::menu_screen::dev_panels {
namespace {
menu::Result ToggleCatalog() {
    if (!ui::native_panels::Catalog().Drained()) ui::native_panels::Catalog().Close();
    else {
        std::vector<ui::lists::Row> rows;
        for (unsigned i = 0; i < 40; ++i)
            rows.push_back({"試しの行 " + std::to_string(i), 0, std::to_string(i), " 番"});
        if (!ui::native_panels::Catalog().Show(std::move(rows))) return menu::Result::Busy;
    }
    return menu::Result::Ok;
}
menu::Result ToggleLabels() {
    std::vector<ui::LabelLine> lines;
    if (ui::native_panels::Labels().Drained()) lines = {{"上画面の箱", 0}, {"二段目の試し", 1}};
    if (!ui::native_panels::Labels().Declare(0, std::move(lines))) return menu::Result::Busy;
    return menu::Result::Ok;
}
menu::Result ToggleLease() {
    if (!ui::native_panels::LeaseOnly(!ui::bottom_screen_lease::HasOwner(4))) return menu::Result::Busy;
    return menu::Result::Ok;
}

const menu::Bound bindings[] = {
    GOHAN_ITEM(menu::MenuId::DevCatalog, menu::Trigger{.run = ToggleCatalog}),
    GOHAN_ITEM(menu::MenuId::DevLabels, menu::Trigger{.run = ToggleLabels}),
    GOHAN_ITEM(menu::MenuId::DevLease, menu::Trigger{.run = ToggleLease}),
};
} // namespace
void PollCatalogDecision() {
    const int n = ui::native_panels::Catalog().TakeDecision();
    if (n >= 0) menu::PostNotice({"一覧", "選んだ行: " + std::to_string(n), false});
}

std::span<const menu::Bound> Bindings() { return bindings; }
} // namespace gohan::features::menu_screen::dev_panels
#endif

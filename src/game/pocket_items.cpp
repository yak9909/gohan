#include "game/pocket_items.hpp"

namespace gohan::game::items {
static_assert(sizeof(generated::ItemID) == 4);
static_assert(offsetof(generated::AcnlPlayerInventoryFinanceF010, pocket) == 27600);
static_assert(offsetof(generated::AcnlPlayerInventoryFinanceF010, pocket_flags) == 27664);
int FindPocketSlot(const PocketAccess &access, generated::AcnlPlayerInventoryFinanceF010 &player, HiddenMode mode) {
    if (mode == HiddenMode::Hide) return access.findEmpty(player.pocket);
    for (unsigned i = 0; i < 16; ++i) if (access.empty(&player.pocket[i])) return static_cast<int>(i);
    return -1;
}
Result AddPocket(const PocketAccess &access, std::uint16_t id, HiddenMode mode) {
    if (id > 0x7FFF) return Result::Failed;
    auto *player = access.player();
    if (!player) return Result::NotReady;
    const auto slot = FindPocketSlot(access, *player, mode);
    if (slot < 0 || slot >= 16) return Result::Failed;
    generated::ItemID item{};
    access.init(&item, id);
    player->pocket[slot] = item;
    player->pocket_flags[slot] = 0;
    if (auto *grid = access.iconGrid()) access.loadIcon(grid, static_cast<unsigned>(slot));
    return Result::Ok;
}
void RefreshPocket(const PocketAccess &access) {
    if (auto *grid = access.iconGrid()) for (unsigned i = 0; i < 16; ++i) access.loadIcon(grid, i);
}
} // namespace gohan::game::items

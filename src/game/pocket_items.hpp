#pragma once
#include "game/generated/api.hpp"
#include "game/patch_set.hpp"

namespace gohan::game::items {
enum class HiddenMode { Hide, Show, Custom };
struct PocketAccess {
    generated::AcnlPlayerInventoryFinanceF010 *(*player)();
    bool (*empty)(const generated::ItemID *);
    int (*findEmpty)(void *);
    void (*init)(generated::ItemID *, std::uint16_t);
    void *(*iconGrid)();
    void (*loadIcon)(void *, unsigned);
};
int FindPocketSlot(const PocketAccess &, generated::AcnlPlayerInventoryFinanceF010 &, HiddenMode);
Result AddPocket(const PocketAccess &, std::uint16_t id, HiddenMode);
void RefreshPocket(const PocketAccess &);
bool WirePocket();
void ShutdownPocket();
Result AddPocket(std::uint16_t id);
bool WireHidden();
void ShutdownHidden();
Result SetHidden(HiddenMode mode);
HiddenMode GetHidden();
} // namespace gohan::game::items

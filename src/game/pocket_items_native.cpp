#include "game/pocket_items.hpp"
#include "game/hook_batch.hpp"
#include "game/item_names.hpp"
#include "game/hidden_icons.hpp"
#include "game/lyt/icon_cache.hpp"
#include "game/memory.hpp"

namespace gohan::game::items {
namespace {
bool pocketReady = false, hiddenReady = false;
bool namesSettled = false;
std::uint32_t iconGeneration = 0;
HiddenMode mode = HiddenMode::Hide;
generated::AcnlPlayerInventoryFinanceF010 *Player() {
    return reinterpret_cast<generated::AcnlPlayerInventoryFinanceF010 *(*)()>(generated::Save_GetCurrentPlayer_addr)();
}
bool Empty(const generated::ItemID *item) { return reinterpret_cast<int (*)(const generated::ItemID *)>(generated::Item_IsEmpty_addr)(item) != 0; }
int Find(void *inventory) { return reinterpret_cast<int (*)(void *)>(generated::Inventory_FindEmptySlot_addr)(inventory); }
void Init(generated::ItemID *item, std::uint16_t id) {
    reinterpret_cast<generated::ItemID *(*)(generated::ItemID *, unsigned)>(generated::Item_InitWithId_addr)(item, id);
}
void *IconGrid() {
    const auto address = reinterpret_cast<std::uintptr_t>(*reinterpret_cast<void **>(generated::g_ActiveBsMenuItem_addr));
    if (!memory::IsHeapPointer(address) || !memory::Readable(address, 0x25)) return nullptr;
    if (*reinterpret_cast<const std::uint32_t *>(address) != generated::BsMenuItem_vtable ||
        *reinterpret_cast<const std::uint8_t *>(address + 0x24)) return nullptr;
    return reinterpret_cast<void *>(address + 7872);
}
void LoadIcon(void *grid, unsigned slot) { reinterpret_cast<int (*)(void *, unsigned)>(generated::vc_LOADICON_addr)(grid, slot); }
const PocketAccess access{Player, Empty, Find, Init, IconGrid, LoadIcon};
const PatchWord words[] = {{generated::PocketHidden_addr, 0xE1A00000u, generated::PocketHidden_crc32}};
PatchSet hidden(NativePatchMemory(), words);
const CodeCheck iconCheck[] = {{generated::PocketLoadIcon_addr, 4, generated::PocketLoadIcon_crc32}};
}
bool WirePocket() {
    const CodeCheck checks[] = {
        iconCheck[0],
        {generated::CurrentSaveEntry_addr, 4, generated::CurrentSaveEntry_crc32},
        {generated::PocketInit_addr, 4, generated::PocketInit_crc32},
        {generated::PocketEmpty_addr, 4, generated::PocketEmpty_crc32},
        {generated::PocketFindEmpty_addr, 4, generated::PocketFindEmpty_crc32},
    };
    pocketReady = VerifyNative(checks);
    return pocketReady;
}
void ShutdownPocket() { pocketReady = false; }
Result AddPocket(std::uint16_t id) { return pocketReady ? AddPocket(access, id, mode) : Result::Unsupported; }
bool WireHidden() {
    if (!VerifyNative(iconCheck)) return false;
    PausedGame pause;
    hiddenReady = hidden.Wire() == Result::Ok;
    return hiddenReady;
}
Result SetHidden(HiddenMode value) {
    if (!hiddenReady || (value == HiddenMode::Custom && !NamesHookReady())) return Result::Unsupported;
    const auto icons = SetHiddenIcons(value == HiddenMode::Custom);
    if (icons != Result::Ok) return icons;
    const auto result = hidden.Set(value != HiddenMode::Hide);
    if (result != Result::Ok) {
        SetHiddenIcons(mode == HiddenMode::Custom);
        return result;
    }
    const bool changed = mode != value;
    mode = value;
    SharedNames().SetCustom(value == HiddenMode::Custom);
    if (changed) RefreshPocket(access);
    return Result::Ok;
}
HiddenMode GetHidden() { return mode; }
void PollHidden() {
    const bool loaded = PollHiddenIcons();
    const bool namesNow = SharedNames().Settled();
    const bool namesChanged = namesNow && !namesSettled;
    namesSettled = namesNow;
    const auto &cache = lyt::SharedIconCache();
    const bool cacheChanged = iconGeneration != cache.Generation() && cache.Data();
    iconGeneration = cache.Generation();
    // 表・arc・描画の導入の順序によらず、使えるようになった次のフレームで反映する。
    if (mode == HiddenMode::Custom && (loaded || namesChanged || cacheChanged)) RefreshPocket(access);
}
void ShutdownHidden() {
    PausedGame pause;
    const bool wasShown = mode != HiddenMode::Hide;
    SharedNames().SetCustom(false);
    SetHiddenIcons(false);
    hidden.Shutdown();
    mode = HiddenMode::Hide;
    if (hiddenReady && wasShown) RefreshPocket(access);
    ShutdownHiddenIcons();
    hiddenReady = false;
}
} // namespace gohan::game::items

#include "game/hidden_icons.hpp"
#include "game/hook_batch.hpp"
#include "game/lyt/icon_cache.hpp"
#include <CTRPluginFramework.hpp>

namespace gohan::game::items {
namespace {
using Widget = generated::AcnlItemDragIconNodeF009;
static_assert(offsetof(Widget, item_id) == 672);
static_assert(offsetof(Widget, layout_storage) == 308);
static_assert(offsetof(generated::NwLytMaterial, variable_data) == 52);
static_assert(offsetof(generated::NwLytMaterial, flags) == 77);
static_assert(offsetof(generated::NwLytPaneVtable, unk_28) == 40);
bool enabled = false, requested = false, started = false, settled = false;
core::files::AsyncLoad load;
core::files::Buffer data;
HiddenIconArchive archive;

__attribute__((noinline)) std::uint32_t SetItem(Widget *widget, const generated::ItemID *item, unsigned kind) {
    const auto result =
        CTRPluginFramework::HookContext::GetCurrent().OriginalFunction<std::uint32_t>(widget, item, kind);
    auto &cache = lyt::SharedIconCache();
    cache.Release(widget);
    if (!widget)
        return result;
    const auto id = widget->item_id & 0x7FFF;
    const auto *entry = SharedNames().CustomName(id);
    const auto *pixels = SelectHiddenIcon(enabled, kind, id, entry, archive);
    if (!pixels)
        return result;
    const auto find =
        reinterpret_cast<generated::SsysMaLayoutNode_FindPicture_t>(generated::SsysMaLayoutNode_FindPicture_addr);
    auto *picture = find(reinterpret_cast<generated::SsysMaLayoutNode *>(widget->layout_storage), "P_itemIcon");
    if (!picture || !picture->pane.vptr)
        return result;
    const auto get =
        reinterpret_cast<generated::NwLytMaterial *(*)(generated::NwLytPane *, unsigned)>(picture->pane.vptr->unk_28);
    auto *material = get ? get(&picture->pane, 0) : nullptr;
    if (!material || !material->variable_data)
        return result;
    if (const auto physical = cache.Place(widget, entry->icon, pixels))
        WriteHiddenIcon(
            *material, physical,
            reinterpret_cast<generated::nwlyt_TexMap_UpdateGpuRegs_t>(generated::nwlyt_TexMap_UpdateGpuRegs_addr));
    return result;
}
__attribute__((noinline)) std::uint32_t Destroy(Widget *widget, std::uint32_t nativeVtable) {
    lyt::SharedIconCache().Release(widget);
    // +0 の LDR が R1 に置いた値を、元の +4 へそのまま渡す。
    return CTRPluginFramework::HookContext::GetCurrent().OriginalFunction<std::uint32_t>(widget, nativeVtable);
}
const HookSpec hooks[] = {
    {{generated::HiddenIconSet_addr, generated::HiddenIconSet_len, generated::HiddenIconSet_crc32},
     reinterpret_cast<std::uintptr_t>(SetItem),
     HookKind::Mitm},
    {{generated::HiddenIconDtor_addr, generated::HiddenIconDtor_len, generated::HiddenIconDtor_crc32},
     reinterpret_cast<std::uintptr_t>(Destroy),
     HookKind::Mitm},
    {{generated::HiddenIconDelete_addr, generated::HiddenIconDelete_len, generated::HiddenIconDelete_crc32},
     reinterpret_cast<std::uintptr_t>(Destroy),
     HookKind::Mitm},
};
const CodeCheck checks[] = {
    {generated::HiddenIconDtorEntry_addr, generated::HiddenIconDtorEntry_len, generated::HiddenIconDtorEntry_crc32},
    {generated::HiddenIconDeleteEntry_addr, generated::HiddenIconDeleteEntry_len,
     generated::HiddenIconDeleteEntry_crc32},
    {generated::HiddenIconFind_addr, generated::HiddenIconFind_len, generated::HiddenIconFind_crc32},
    {generated::HiddenIconUpdate_addr, generated::HiddenIconUpdate_len, generated::HiddenIconUpdate_crc32},
};
HookBatch batch(NativeHookBackend(), hooks);
bool hooked = false;
} // namespace
Result SetHiddenIcons(bool on) {
    if (on && !hooked) {
        PausedGame pause;
        if (!VerifyNative(checks))
            return Result::Unsupported;
        const auto result = batch.Wire();
        if (result != Result::Ok)
            return result;
        hooked = true;
    }
    enabled = on;
    if (on)
        requested = true;
    return Result::Ok;
}
bool PollHiddenIcons() {
    if (!requested || settled)
        return false;
    if (!started) {
        char path[96];
        if (core::files::CommonPath(path, sizeof(path), "hidden_item_icons.arc"))
            started = load.Start(path, HiddenIconArchive::Limit);
    }
    core::files::Buffer buffer;
    if (!load.Take(buffer))
        return false;
    data = std::move(buffer);
    if (data && !archive.Parse(data.data.get(), data.size))
        data = {};
    settled = true;
    return true;
}
bool HiddenIconsSettled() { return settled; }
bool HiddenIconsAvailable() { return archive.Count() != 0; }
void ShutdownHiddenIcons() {
    enabled = false;
    requested = false;
    batch.Shutdown();
    hooked = false;
    archive = {};
    data = {};
}
} // namespace gohan::game::items

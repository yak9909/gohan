#include "KeepItems.hpp"
#include "Cheats.hpp"
#include "GuiMenu.hpp"
#include "NativePortGuard.hpp"
#include <CTRPluginFramework.hpp>
using namespace CTRPluginFramework;

namespace KeepItems {
namespace {
Hook s_hook;
bool s_installed;
u32 s_active;
const GuiMenu::TogglePatch kPatches[] = {
    {0x6FA8A8,4,0xE3A00000,0xEBF00828},
    {0x52734C,4,0xE1A00000,0x0A000006}
};
const NativePortGuard::Word kAnchors[] = {
    {0x2FEE38,0xE92D4FF0}, {0x3159F2,0xEFAEF7E6}, {0x315A7A,0xEF6AF7E6},
    {0x315B08,0xEF22F7E6}, {0x316392,0xEADEF7E6}, {0x316412,0xEA9EF7E6}
};
__attribute__((noinline)) u32 SetItem(u32 map,const u32 *item,u32 x,u32 y,u32 heightBits) {
    HookContext &ctx = HookContext::GetCurrent();
    const u32 caller = reinterpret_cast<u32>(__builtin_return_address(0));
    const u32 sites[] = {0x3159F2,0x315A7A,0x315B08,0x316392,0x316412};
    bool growth = false;
    for (u32 site : sites) if ((caller & ~1u) == site+4u) growth = true;
    u32 replacement;
    if (__atomic_load_n(&s_active,__ATOMIC_ACQUIRE) && growth) {
        const u32 *tile = reinterpret_cast<const u32 *(*)(u32,u32,u32,u32)>(0x2FEE38)(map,x,y,0);
        if (tile) {
            // 成長を失敗させて消す経路だけ、今の植物を次の成長IDへ進める。flagsは残す。
            replacement = (*tile & 0xFFFF0000u) | static_cast<u16>(static_cast<u16>(*tile)+1u);
            item = &replacement;
        }
    }
    // 第5引数のfloatはnativeのstack ABIを維持するため生bitsで転送する。
    return ctx.OriginalFunction<u32>(map,item,x,y,heightBits);
}
bool IsActive(int) { return __atomic_load_n(&s_active,__ATOMIC_ACQUIRE) != 0; }
void SetActive(int index,bool on) {
    if (on) {
        if (!NativePortGuard::Matches(kAnchors)) { GuiMenu::NotifyRed(Cheats::kKeepItems,u8"対象の版が一致しません。"); return; }
        for (const auto &p : kPatches) if (NativePortGuard::Read(p.address) != p.offValue) {
            GuiMenu::NotifyRed(Cheats::kKeepItems,u8"ほかのパッチと競合しています。"); return;
        }
        if (!s_installed) {
            if (NativePortGuard::Read(0x2FC950) != 0xE92D4FF0) return;
            s_hook.InitializeForMitm(0x2FC950,reinterpret_cast<u32>(SetItem));
            if (s_hook.Enable() != HookResult::Success) return;
            s_installed = true;
        }
        __atomic_store_n(&s_active,1u,__ATOMIC_RELEASE);
        GuiMenu::PatchListSetActive(kPatches,2,true);
    } else {
        __atomic_store_n(&s_active,0u,__ATOMIC_RELEASE);
        GuiMenu::PatchListSetActive(kPatches,2,false);
    }
    (void)index;
}
const GuiMenu::ToggleEffectFuncs kFuncs = {IsActive,SetActive};
}
void Wire() {
    const int index = GuiMenu::FindItem(Cheats::kKeepItems);
    if (index >= 0) GuiMenu::RegisterToggleEffect(index,&kFuncs);
}
}

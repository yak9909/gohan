// ============================================================================
// HiddenIcons — 没アイテムの自前のアイコン（gohan.md §17.5.1。利用者の選択 2026-09-27: 案 B = 3gx に内蔵）
// ============================================================================
//
// ゲームのアイコン: ItemIconWidget_SetItem 0x2B8EAC(widget, &item, kind) が ItemParam_GetIconBaseName の名前 + ".bclim" を
//   アクセサから取り、P_itemIcon の material の TexMap（material +52）へ書く。没アイテムは表に記録が無いので icn_000（IDA-opus-5.5-F049）。
// 自前: その後（kind 0 のときだけ）、没アイテムで絵の指定があれば、同じ TexMap を自前の絵へ書き換える（ゲームと同じ書き方:
//   TexMap +0 = 0 / +4 = PA / +8 = 大きさ / +12 = 2 の冪の大きさ / +16 の書式 = 11（ETC1A4）→ nwlyt_TexMap_UpdateGpuRegs、material +77 の bit2 を落とす）。
// 絵は 3gx の .rodata（GPU から読めない）にあるので、借りたヒープの末尾のキャッシュ（GuiRenderer::GpuIconCache、4,096 B x 16 枠）へ写して使う。
//   枠は絵と大きさで引き、無ければ一番長く使っていない枠へ写す（持ち物は 16 枠なので 16 種類までは同時に出せる）。
// 絵のデータ（HiddenItemIcons.h、解析 repo tools/items/export_hidden_icons.py の生成物）は Git に入れない。無ければ何もしない。

#include "HiddenIcons.hpp"

#include "GuiRenderer.hpp"
#include "ItemNames.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstdio>
#include <cstring>

#if defined(__has_include)
#if __has_include("HiddenItemIcons.h")
#include "HiddenItemIcons.h"
#endif
#endif

namespace HiddenIcons {

namespace {

using CTRPluginFramework::Hook;
using CTRPluginFramework::HookContext;
using CTRPluginFramework::HookResult;

const u32 kSetItem = 0x002B8EAC;            // ItemIconWidget_SetItem(widget, &item, kind)
const u32 kSetItemOrig = 0xE92D4FF7;        // PUSH {R0-R2,R4-R11,LR}
const u32 kFindPane = 0x00567E44;           // (layout, name) -> pane（widget +308 の layout から）
const u32 kFindPaneOrig = 0xE92D4070;
const u32 kTexMapUpdate = 0x004B9830;       // nwlyt_TexMap_UpdateGpuRegs(texMap)
const u32 kTexMapUpdateOrig = 0xE52D4004;
const u32 kWidgetItem = 672;                // widget +672 = 表示中のアイテム（SetItem の最初に写す）
const u32 kWidgetLayout = 308;
const u32 kMaterialTexMap = 52;
const u32 kMaterialFlags = 77;
const u32 kFmtEtc1a4 = 11;
const u32 kSlotBytes = 4096;
const u32 kSlots = 16;

struct Slot {
    s16 image;      // HiddenItemIcons の番号（-1 = 空）
    u16 size;       // 32 / 64
    u32 stamp;
};

Hook s_hook;
bool s_hooked;
volatile bool s_on;
volatile u32 s_size = 32;
Slot s_slots[kSlots];
u32 s_clock;
u32 s_gen = 0xFFFFFFFFu;
// 診断（利用者の実機確認で、どこで抜けたかを見るため。2026-09-27）
volatile u32 s_calls, s_hidden, s_noCache, s_noPane, s_applied, s_cacheBytes;

inline u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
inline u8 R8(u32 a) { return *reinterpret_cast<const volatile u8 *>(a); }
inline void W32(u32 a, u32 v) { *reinterpret_cast<volatile u32 *>(a) = v; }
inline void W8(u32 a, u8 v) { *reinterpret_cast<volatile u8 *>(a) = v; }

#ifdef GOHAN_HIDDEN_ITEM_ICONS
int ImageIndex(u16 number) {
    u32 lo = 0, hi = HiddenItemIcons::kCount;
    while (lo < hi) {
        const u32 mid = (lo + hi) / 2;
        const u16 v = HiddenItemIcons::kImageIds[mid];
        if (v == number)
            return (int)mid;
        if (v < number)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

// 絵をキャッシュへ置いて VA を返す（無ければ 0）。ゲームのスレッド
u32 Place(int image, u32 size) {
    u32 bytes = 0;
    const u32 base = CTRPluginFramework::GuiRenderer::GpuIconCache(bytes);
    s_cacheBytes = bytes;
    if (base == 0 || bytes < kSlotBytes * kSlots)
        return 0;
    if (s_gen != CTRPluginFramework::GuiRenderer::Generation()) {
        s_gen = CTRPluginFramework::GuiRenderer::Generation();     // 描画器が組み直された: 中身は無い
        for (u32 i = 0; i < kSlots; ++i)
            s_slots[i].image = -1;
    }
    int empty = -1, oldest = 0;
    for (u32 i = 0; i < kSlots; ++i) {
        if (s_slots[i].image == image && s_slots[i].size == size) {
            s_slots[i].stamp = ++s_clock;
            return base + i * kSlotBytes;
        }
        if (s_slots[i].image < 0) {
            if (empty < 0)
                empty = (int)i;
        } else if (s_slots[i].stamp < s_slots[oldest].stamp || s_slots[oldest].image < 0)
            oldest = (int)i;
    }
    const u32 pick = (u32)(empty >= 0 ? empty : oldest);
    const u8 *src = size == 64 ? HiddenItemIcons::k64[image] : HiddenItemIcons::k32[image];
    const u32 n = size == 64 ? 4096u : 1024u;
    const u32 va = base + pick * kSlotBytes;
    std::memcpy(reinterpret_cast<void *>(va), src, n);
    svcFlushProcessDataCache(CUR_PROCESS_HANDLE, va, n);             // GPU は D-cache を見ない
    s_slots[pick].image = (s16)image;
    s_slots[pick].size = (u16)size;
    s_slots[pick].stamp = ++s_clock;
    return va;
}
#endif

__attribute__((noinline)) u32 SetItemHook(u32 widget, u16 *item, u32 kind) {
    HookContext &ctx = HookContext::GetCurrent();
    const u32 r = ctx.OriginalFunction<u32>(widget, item, kind);

#ifdef GOHAN_HIDDEN_ITEM_ICONS
    if (!s_on || widget == 0)
        return r;
    s_calls = s_calls + 1;
    if (kind != 0)
        return r;
    const u16 id = *reinterpret_cast<const volatile u16 *>(widget + kWidgetItem) & 0x7FFF;
    if (id == 0x7FFE)
        return r;
    const HiddenItemTable::Entry *e = ItemNames::FindHidden(id);
    if (e == nullptr || e->icon == 0)
        return r;
    const int image = ImageIndex(e->icon);
    if (image < 0)
        return r;
    s_hidden = s_hidden + 1;
    const u32 size = s_size == 64 ? 64 : 32;
    const u32 va = Place(image, size);
    if (va == 0) {
        s_noCache = s_noCache + 1;
        return r;
    }

    typedef u32 (*FindPaneFn)(u32 layout, const char *name);
    typedef u32 (*GetMaterialFn)(u32 pane, u32 index);
    typedef void (*UpdateFn)(u32 texMap);
    const u32 pane = reinterpret_cast<FindPaneFn>(kFindPane)(widget + kWidgetLayout, "P_itemIcon");
    const u32 material = pane != 0 ? reinterpret_cast<GetMaterialFn>(R32(R32(pane) + 40))(pane, 0) : 0;
    const u32 texMap = material != 0 ? R32(material + kMaterialTexMap) : 0;
    if (texMap == 0) {
        s_noPane = s_noPane + 1;
        return r;
    }
    W32(texMap + 0, 0);
    W32(texMap + 4, va - 0x10000000u);
    W32(texMap + 8, size | size << 16);
    W32(texMap + 12, size | size << 16);
    W32(texMap + 16, ((kFmtEtc1a4 << 8) & 0xF00u) | (R32(texMap + 16) & 0xFFFFF0FFu));
    reinterpret_cast<UpdateFn>(kTexMapUpdate)(texMap);
    W8(material + kMaterialFlags, R8(material + kMaterialFlags) & ~4u);
    s_applied = s_applied + 1;
#else
    (void)kind;
    (void)item;
#endif
    return r;
}

}  // namespace

bool Available(void) {
#ifdef GOHAN_HIDDEN_ITEM_ICONS
    return true;
#else
    return false;
#endif
}

bool SetEnabled(bool on) {
    if (on && !s_hooked) {
        if (!Available())
            return false;
        if (CTRPluginFramework::Process::GetTitleID() != 0x0004000000086200ULL || R32(kSetItem) != kSetItemOrig
            || R32(kFindPane) != kFindPaneOrig || R32(kTexMapUpdate) != kTexMapUpdateOrig)
            return false;
        s_hook.InitializeForMitm(kSetItem, reinterpret_cast<u32>(SetItemHook));
        if (s_hook.Enable() != HookResult::Success)
            return false;
        s_hooked = true;
    }
    s_on = on;          // フックは入れたまま（切っている間は元のアイコンのまま）
    return true;
}

bool Enabled(void) {
    return s_on;
}

void SetSize(u32 size) {
    s_size = size == 64 ? 64 : 32;
}

u32 Size(void) {
    return s_size;
}

void Status(char *out, u32 cap) {
    u32 bytes = 0;
    const u32 base = CTRPluginFramework::GuiRenderer::GpuIconCache(bytes);
    std::snprintf(out, cap, u8"呼出 %lu / 没 %lu / 置けず %lu / 枠なし %lu / 貼替 %lu / キャッシュ 0x%08lX (0x%lX)",
                  (unsigned long)s_calls, (unsigned long)s_hidden, (unsigned long)s_noCache, (unsigned long)s_noPane,
                  (unsigned long)s_applied, (unsigned long)base, (unsigned long)bytes);
}

}  // namespace HiddenIcons

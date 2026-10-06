// ============================================================================
// HiddenIcons — 没アイテムの自前のアイコン（gohan.md §17.5.1。2026-10-06: 3gx 内蔵 → SD の arc に。利用者）
// ============================================================================
//
// ゲームのアイコン: ItemIconWidget_SetItem 0x2B8EAC(widget, &item, kind) が ItemParam_GetIconBaseName の名前 + ".bclim" を
//   アクセサから取り、P_itemIcon の material の TexMap（material +52）へ書く。没アイテムは表に記録が無いので icn_000（IDA-opus-5.5-F049）。
// 自前: その後（kind 0 のときだけ）、没アイテムで絵の指定があれば、同じ TexMap を自前の絵へ書き換える（ゲームと同じ書き方:
//   TexMap +0 = 0 / +4 = PA / +8 = 大きさ / +12 = 2 の冪の大きさ / +16 の書式 = 11（ETC1A4）→ nwlyt_TexMap_UpdateGpuRegs、material +77 の bit2 を落とす）。
// 絵は 32x32（利用者が見比べて決めた。2026-09-27）。sdmc:/gohan/common/hidden_item_icons.arc（DARC。timg/<番号>.bclim、各 1,064 B =
//   ETC1A4 1,024 B + CLIM 見出し 0x28。ゲームの Layout/ItemWin/item_icon_tex.arc と同じ形。解析 repo tools/items/export_hidden_icons.py）を
//   起動時にプラグインのヒープへ読む（LoadIcons）。プラグインのヒープは GPU から読めないので、
//   借りたヒープの末尾のキャッシュ（GuiRenderer::GpuIconCache、1,024 B x 16 枠）へ写して使う。
//   枠は絵と大きさで引き、無ければ一番長く使っていない枠へ写す（持ち物は 16 枠なので 16 種類までは同時に出せる）。
// arc が無ければ何もしない（ゲームのアイコンのまま）。

#include "HiddenIcons.hpp"

#include "GohanFiles.hpp"
#include "GuiRenderer.hpp"
#include "ItemNames.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include <cstring>

#include <new>

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
const u32 kSlotBytes = 1024;               // 32x32 ETC1A4
const u32 kIconSize = 32;
const u32 kSlots = 16;

struct Slot {
    s16 image;      // s_images の番号（-1 = 空）
    u32 stamp;
};

// 読んだ arc の絵（番号の昇順）
struct Image {
    u16 number;     // timg/<番号>.bclim
    const u8 *data; // ETC1A4 1,024 B（arc の中）
};
const u32 kArcMaxBytes = 512 * 1024;
const u32 kMaxImages = 512;
u8 *s_arc;
Image *s_images;
u32 s_imageCount;
bool s_loadTried;

Hook s_hook;
bool s_hooked;
volatile bool s_on;
Slot s_slots[kSlots];
u32 s_clock;
u32 s_gen = 0xFFFFFFFFu;

inline u32 R32(u32 a) { return *reinterpret_cast<const volatile u32 *>(a); }
inline u8 R8(u32 a) { return *reinterpret_cast<const volatile u8 *>(a); }
inline void W32(u32 a, u32 v) { *reinterpret_cast<volatile u32 *>(a) = v; }
inline void W8(u32 a, u8 v) { *reinterpret_cast<volatile u8 *>(a) = v; }

inline u32 Le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
inline u16 Le16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }

// DARC の 1 項目の名前（UTF-16LE）が "timg/" の下の "<10 進>.bclim" なら番号を返す（違えば -1）。親は見ず名前だけで判断する
int BclimNumber(const u8 *name, const u8 *end) {
    u32 v = 0, digits = 0;
    const u8 *p = name;
    for (; p + 1 < end && Le16(p) >= '0' && Le16(p) <= '9'; p += 2) {
        v = v * 10 + (Le16(p) - '0');
        if (++digits > 5)
            return -1;
    }
    const char ext[] = ".bclim";
    for (u32 i = 0; i < 6; ++i, p += 2)
        if (p + 1 >= end || Le16(p) != (u16)ext[i])
            return -1;
    if (p + 1 >= end || Le16(p) != 0 || digits == 0 || v > 0xFFFF)
        return -1;
    return (int)v;
}

int ImageIndex(u16 number) {
    u32 lo = 0, hi = s_imageCount;
    while (lo < hi) {
        const u32 mid = (lo + hi) / 2;
        const u16 v = s_images[mid].number;
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
u32 Place(int image) {
    u32 bytes = 0;
    const u32 base = CTRPluginFramework::GuiRenderer::GpuIconCache(bytes);
    if (base == 0 || bytes < kSlotBytes * kSlots)
        return 0;
    if (s_gen != CTRPluginFramework::GuiRenderer::Generation()) {
        s_gen = CTRPluginFramework::GuiRenderer::Generation();     // 描画器が組み直された: 中身は無い
        for (u32 i = 0; i < kSlots; ++i)
            s_slots[i].image = -1;
    }
    int empty = -1, oldest = 0;
    for (u32 i = 0; i < kSlots; ++i) {
        if (s_slots[i].image == image) {
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
    const u32 va = base + pick * kSlotBytes;
    std::memcpy(reinterpret_cast<void *>(va), s_images[image].data, kSlotBytes);
    svcFlushProcessDataCache(CUR_PROCESS_HANDLE, va, kSlotBytes);    // GPU は D-cache を見ない
    s_slots[pick].image = (s16)image;
    s_slots[pick].stamp = ++s_clock;
    return va;
}

__attribute__((noinline)) u32 SetItemHook(u32 widget, u16 *item, u32 kind) {
    HookContext &ctx = HookContext::GetCurrent();
    const u32 r = ctx.OriginalFunction<u32>(widget, item, kind);

    if (!s_on || widget == 0 || kind != 0)
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
    const u32 size = kIconSize;
    const u32 va = Place(image);
    if (va == 0)
        return r;

    typedef u32 (*FindPaneFn)(u32 layout, const char *name);
    typedef u32 (*GetMaterialFn)(u32 pane, u32 index);
    typedef void (*UpdateFn)(u32 texMap);
    const u32 pane = reinterpret_cast<FindPaneFn>(kFindPane)(widget + kWidgetLayout, "P_itemIcon");
    const u32 material = pane != 0 ? reinterpret_cast<GetMaterialFn>(R32(R32(pane) + 40))(pane, 0) : 0;
    const u32 texMap = material != 0 ? R32(material + kMaterialTexMap) : 0;
    if (texMap == 0)
        return r;
    W32(texMap + 0, 0);
    W32(texMap + 4, va - 0x10000000u);
    W32(texMap + 8, size | size << 16);
    W32(texMap + 12, size | size << 16);
    W32(texMap + 16, ((kFmtEtc1a4 << 8) & 0xF00u) | (R32(texMap + 16) & 0xFFFFF0FFu));
    reinterpret_cast<UpdateFn>(kTexMapUpdate)(texMap);
    W8(material + kMaterialFlags, R8(material + kMaterialFlags) & ~4u);
    (void)item;
    return r;
}

}  // namespace

u32 LoadIcons(void) {
    if (s_loadTried)
        return s_imageCount;
    s_loadTried = true;
    char path[64];
    u32 size = 0;
    if (!GohanFiles::CommonPath(path, sizeof(path), "hidden_item_icons.arc"))
        return 0;
    u8 *arc = GohanFiles::ReadAll(path, kArcMaxBytes, size);
    if (arc == nullptr)
        return 0;
    // DARC: 'darc' FFFE 001C 版 全長 表の位置 表の長さ データの位置。表 = 12 B の項目 {名前の位置 | 0x01000000 = フォルダ, データの位置, 大きさ}、
    //   項目の数 = 根の項目の 3 語目、名前の区画は項目の後ろ（tools/hhd/darc_pack.py と同じ並べ方）
    const u32 tableOff = size >= 0x1C ? Le32(arc + 0x10) : 0, tableLen = size >= 0x1C ? Le32(arc + 0x14) : 0;
    const u32 entries = tableOff + 12 <= size ? Le32(arc + tableOff + 8) : 0;
    if (std::memcmp(arc, "darc", 4) != 0 || Le16(arc + 4) != 0xFEFF || tableLen > size - tableOff || entries == 0
        || 12 * (u64)entries > tableLen) {
        delete[] arc;
        return 0;
    }
    const u8 *names = arc + tableOff + 12 * entries;
    const u8 *namesEnd = arc + tableOff + tableLen;
    Image *images = new (std::nothrow) Image[kMaxImages];
    if (images == nullptr) {
        delete[] arc;
        return 0;
    }
    u32 count = 0;
    for (u32 i = 0; i < entries && count < kMaxImages; ++i) {
        const u8 *e = arc + tableOff + 12 * i;
        const u32 nameOff = Le32(e), off = Le32(e + 4), len = Le32(e + 8);
        if ((nameOff & 0x01000000u) != 0 || names + nameOff >= namesEnd)
            continue;
        const int number = BclimNumber(names + nameOff, namesEnd);
        // 32x32 の ETC1A4 だけ（見出し: CLIM / imag の幅・高さ・書式 11 / データ長 1,024）
        if (number < 0 || len != kSlotBytes + 0x28 || off > size || len > size - off)
            continue;
        const u8 *foot = arc + off + kSlotBytes;
        if (std::memcmp(foot, "CLIM", 4) != 0 || std::memcmp(foot + 0x14, "imag", 4) != 0 || Le16(foot + 0x1C) != kIconSize
            || Le16(foot + 0x1E) != kIconSize || Le32(foot + 0x20) != kFmtEtc1a4 || Le32(foot + 0x24) != kSlotBytes)
            continue;
        images[count].number = (u16)number;
        images[count].data = arc + off;
        ++count;
    }
    for (u32 i = 1; i < count; ++i) {   // 番号順（二分探索のため）
        const Image v = images[i];
        u32 j = i;
        while (j > 0 && images[j - 1].number > v.number) {
            images[j] = images[j - 1];
            --j;
        }
        images[j] = v;
    }
    s_arc = arc;
    s_images = images;
    s_imageCount = count;
    return count;
}

bool Available(void) {
    return s_imageCount != 0;
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



}  // namespace HiddenIcons

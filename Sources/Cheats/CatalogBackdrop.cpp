#include "CatalogBackdrop.hpp"
#include <CTRPluginFramework.hpp>

// IDA-gpt-6.1-sol-F015: change only draw-time globalAlpha, then restore it.
// Scope: editor main, chip roots/children, range, and our opening tabs.
namespace CatalogBackdrop {
namespace {
using namespace CTRPluginFramework;
Hook s_hook;
bool s_installed, s_drawing;
u32 s_editor, s_top, s_scene, s_editorSlot;
u8 s_alpha = 255;
struct SavedAlpha { u8 *address; u8 value; };
SavedAlpha s_saved[4096];
u32 s_count;

u32 R32(u32 address) { return *reinterpret_cast<const u32 *>(address); }
bool EditorValid(void) {
    return s_editor && s_scene == R32(0x00948E70) && s_editorSlot
        && Process::CheckAddress(s_editorSlot, MEMPERM_READ) && R32(s_editorSlot) == s_editor;
}
void Dirty(u32 layout) {
    if (layout && Process::CheckAddress(layout + 285, MEMPERM_WRITE))
        *reinterpret_cast<u8 *>(layout + 285) = 1;
}
void Invalidate(void) {
    if (s_scene != R32(0x00948E70))
        return;
    Dirty(s_top);
    if (!EditorValid())
        return;
    // F016: draw-time alpha also needs the cached command list re-recorded.
    Dirty(s_editor + 664);
    Dirty(s_editor + 128200 + 36);
    for (u32 i = 0; i < 112; ++i) {
        const u32 chip = s_editor + 1176 + i * 1120;
        if (*reinterpret_cast<const u8 *>(chip + 1117))
            Dirty(chip + 36);
    }
}
bool Matches(u32 innerLayout) {
    if (s_alpha == 255 || s_scene != R32(0x00948E70))
        return false;
    if (s_editor && (!s_editorSlot || !Process::CheckAddress(s_editorSlot, MEMPERM_READ)
        || R32(s_editorSlot) != s_editor))
        return false;
    const u32 layout = innerLayout - 16;
    if (s_top && layout == s_top)
        return true;
    if (!s_editor)
        return false;
    if (layout == s_editor + 664 || layout == s_editor + 128200 + 36)
        return true;
    const u32 first = s_editor + 1176 + 36;
    return layout >= first && layout - first < 112u * 1120u
        && (layout - first) % 1120u == 0;
}
bool ScaleTree(u32 pane, u32 depth) {
    if (!pane)
        return true;
    if (depth >= 64 || s_count == sizeof(s_saved) / sizeof(s_saved[0]))
        return false;
    u8 *alpha = reinterpret_cast<u8 *>(pane + 181);
    s_saved[s_count++] = {alpha, *alpha};
    *alpha = static_cast<u8>(static_cast<u32>(*alpha) * s_alpha / 255u);
    const u32 sentinel = pane + 20;
    for (u32 link = R32(sentinel); link != sentinel; link = R32(link))
        if (!ScaleTree(link - 4, depth + 1))
            return false;
    return true;
}
void Restore(void) {
    while (s_count) {
        const SavedAlpha &saved = s_saved[--s_count];
        *saved.address = saved.value;
    }
}
__attribute__((noinline)) u32 DrawHook(u32 drawInfo, u32 innerLayout, u32 arg) {
    HookContext &context = HookContext::GetCurrent();
    if (!innerLayout || s_drawing || !Matches(innerLayout))
        return context.OriginalFunction<u32>(drawInfo, innerLayout, arg);
    s_drawing = true;
    s_count = 0;
    if (!ScaleTree(R32(innerLayout + 16), 0))
        Restore();                             // Oversized/deep tree: native draw.
    const u32 result = context.OriginalFunction<u32>(drawInfo, innerLayout, arg);
    Restore();                                 // Never retain animation/alpha edits.
    s_drawing = false;
    return result;
}
}

bool Install(void) {
    if (s_installed)
        return s_hook.IsEnabled();
    if (Process::GetTitleID() != 0x0004000000086200ULL)
        return false;
    const struct { u32 address, word; } checks[] = {
        {0x004D7174, 0xE92D4070}, {0x004D7178, 0xE1A05000},
        {0x0073B524, 0xE92D41F0}, {0x004B6154, 0xE92D40F0},
        {0x001CBBA8, 0xE92D4010}, {0x001A15B0, 0xE92D4010},
        {0x00568704, 0xE5D5001D}, {0x005687B0, 0xE5C4011D},
    };
    for (const auto &check : checks)
        if (R32(check.address) != check.word)
            return false;
    s_hook.InitializeForMitm(0x004D7174, reinterpret_cast<u32>(DrawHook));
    s_installed = s_hook.Enable() == HookResult::Success;
    return s_installed;
}
void Set(u32 editor, u32 topLayout, u8 alpha, u32 editorSlot) {
    const u32 scene = R32(0x00948E70);
    const bool changed = s_editor != editor || s_top != topLayout || s_alpha != alpha
        || s_editorSlot != editorSlot || s_scene != scene;
    if (changed && s_alpha != 255)
        Invalidate();
    s_editor = editor;
    s_editorSlot = editorSlot;
    s_top = topLayout;
    s_scene = scene;
    s_alpha = alpha;
    if (changed)
        Invalidate();
}
void Reset(void) {
    if (s_alpha != 255)
        Invalidate();
    s_editor = s_top = 0;
    s_alpha = 255;
}
}

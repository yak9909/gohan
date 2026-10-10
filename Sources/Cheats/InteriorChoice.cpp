#include "InteriorChoice.hpp"
#include "DecorLayout.hpp"
#include "GuiMenu.hpp"
#include <CTRPluginFramework.hpp>
#include <cstring>

// IDA-gpt-6.1-sol-F016: native window lifecycle/key injection from MapEditor.
namespace InteriorChoice {
namespace {
using namespace CTRPluginFramework;
alignas(8) u8 s_window[2880], s_anchor[80];
bool s_made, s_loaded, s_built, s_wanted, s_opened;
u32 s_wait, s_prev, s_hold[32];
float s_x, s_y;
int s_result = -1;
u32 &W(u32 off) { return *reinterpret_cast<u32 *>(s_window + off); }
u8 &B(u32 off) { return s_window[off]; }
float &F(u32 off) { return *reinterpret_cast<float *>(s_window + off); }
u32 R32(u32 at) { return *reinterpret_cast<const u32 *>(at); }
const u32 kIdle = 0x002B9F5C, kDecided = 0x002BA29C, kClose = 0x002BA2A0;
bool NativeBusy(void) { return s_built && (W(12) != kIdle || W(16) != 0); }
void Close(void) {
    if (NativeBusy() && W(12) != kClose)
        reinterpret_cast<void (*)(void *, u32, u32)>(0x00818528)(s_window, kClose, 0);
    s_wanted = false;
}
void Free(void) {
    if (s_made) {
        if (s_built)
            reinterpret_cast<void (*)(void *)>(0x00133A5C)(s_window + 624);
        reinterpret_cast<void (*)(void *)>(0x002BB13C)(s_window);
    }
    s_made = s_loaded = s_built = s_wanted = s_opened = false;
    s_wait = 0;
}
bool BuildStep(void) {
    if (!s_made) {
        if (DecorLayout::HeapFreeBytes() < 0x40000 || DecorLayout::HeapMaxAllocBytes(0x80) < 0x20000)
            return false;
        reinterpret_cast<void (*)(void *)>(0x002BAFC0)(s_window);
        s_made = true;
    }
    if (!s_loaded) {
        if (!reinterpret_cast<int (*)(void *)>(0x002BAC48)(s_window))
            return false;
        s_loaded = true;
        return false;
    }
    if (!s_built) {
        reinterpret_cast<int (*)(void *, void *)>(0x002BA8B0)(s_window, nullptr);
        s_built = true;
        for (u32 i = 0; i < 6; ++i) {
            void *box = reinterpret_cast<void *>(W(956 + 12 * i));
            if (!box) { Close(); s_result = 1; return false; }
            const u32 draw = R32(reinterpret_cast<u32>(box) + 260);
            const u32 flags = draw ? *reinterpret_cast<const u8 *>(draw + 9) : 0;
            const u32 vt = *reinterpret_cast<u32 *>(box);
            reinterpret_cast<void (*)(void *, u32, u32)>(R32(vt + 112))(box, 8, flags);
        }
    }
    return true;
}
u32 Keys(void) {
    const u32 keys = Controller::GetKeysDown();
    return ((keys & (u32)Key::A) ? 5u : 0u)
        | ((keys & (u32)Key::B) ? 10u : 0u)
        | ((keys & (u32)Key::DPadUp) ? 0x400u : 0u)
        | ((keys & (u32)Key::DPadDown) ? 0x800u : 0u)
        | ((keys & (u32)Key::DPadLeft) ? 0x1000u : 0u)
        | ((keys & (u32)Key::DPadRight) ? 0x2000u : 0u);
}
void OpenNative(void) {
    const u16 *const rows[] = {
        reinterpret_cast<const u16 *>(u"複製"), reinterpret_cast<const u16 *>(u"やめる")
    };
    const u32 sounds[] = {0x01000395, 0x01000392};
    const u16 empty[] = {0};
    for (u32 i = 0; i < 6; ++i) {
        void *box = reinterpret_cast<void *>(W(956 + 12 * i));
        void *line = reinterpret_cast<void *>(W(960 + 12 * i));
        const u16 *text = i < 2 ? rows[i] : empty;
        u32 n = 0;
        while (text[n]) ++n;
        DecorLayout::SetText(box, text, n);
        if (i < 2) {
            const float width = DecorLayout::MeasureText(box, text, n);
            DecorLayout::SetSize(box, width, DecorLayout::Height(box));
            DecorLayout::SetSize(line, width, DecorLayout::Height(line));
            W(964 + 12 * i) = sounds[i];
        }
    }
    F(1236) = s_x;
    F(1240) = s_y;
    F(1244) = 0;
    *reinterpret_cast<float *>(s_anchor + 72) = 20;
    *reinterpret_cast<float *>(s_anchor + 76) = 20;
    W(1204) = reinterpret_cast<u32>(s_anchor);
    reinterpret_cast<void (*)(void *, u32)>(0x002B9F64)(s_window, 0);
    B(1251) = 0;
    reinterpret_cast<void (*)(void *, s32)>(0x002BAF48)(s_window, 1);
    s_prev = Keys();                           // Opening held buttons are not a new press.
    std::memset(s_hold, 0, sizeof(s_hold));
    s_opened = true;
}
void Update(void) {
    const u32 held = Keys();
    const u32 trig = held & ~s_prev;
    u32 repeat = 0;
    for (u32 i = 0; i < 32; ++i) {
        const u32 bit = 1u << i, count = s_hold[i];
        if (!(held & bit)) { s_hold[i] = 0; continue; }
        if ((0x77F00u & bit) && (count == 20 || (count > 20 && (count - 20) % 5 == 0)))
            repeat |= bit;
        s_hold[i] = count + 1;
    }
    s_prev = held;
    const u32 menu = R32(0x00949D4C);
    const u32 oldTrig = menu ? R32(menu + 72) : 0, oldRepeat = menu ? R32(menu + 80) : 0;
    if (menu) {
        *reinterpret_cast<u32 *>(menu + 72) = trig;
        *reinterpret_cast<u32 *>(menu + 80) = repeat;
    }
    reinterpret_cast<void (*)(void *)>(0x002BABAC)(s_window);
    if (menu) {
        *reinterpret_cast<u32 *>(menu + 72) = oldTrig;
        *reinterpret_cast<u32 *>(menu + 80) = oldRepeat;
    }
    if (W(12) == kDecided && W(16) == 0) {
        s_result = B(1228) || W(1212) != 0 ? 1 : 0;
        Close();
    }
}
}
bool Open(float x, float y) {
    if (Busy()) return false;
    s_x = x; s_y = y;
    s_wanted = true;
    s_result = -1;
    s_wait = 0;
    return true;
}
bool Busy(void) { return s_wanted || s_made; }
int TakeResult(void) { const int result = s_result; s_result = -1; return result; }
void FrameStep(bool contextValid) {
    if (!Busy()) return;
    if (!contextValid || GuiMenu::IsVisible()) {
        s_result = 1;
        Close();
    }
    if (s_wanted && !s_opened) {
        if (!BuildStep()) {
            if (++s_wait > 120) { s_result = 1; Close(); }
            return;
        }
        OpenNative();
        s_wait = 0;
    }
    if (NativeBusy()) {
        Update();
        reinterpret_cast<void (*)(void *, void *, u32)>(0x0056928C)
            (reinterpret_cast<void *>(R32(0x0096FC38)), s_window + 624, 1);
        s_wait = 0;
    } else if (!s_wanted && ++s_wait >= 3) {
        Free();
    }
}
}

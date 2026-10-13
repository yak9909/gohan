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
alignas(4) u8 s_touch[40];
bool s_touchBlocked;
bool s_made, s_loaded, s_built, s_wanted, s_opened;
u32 s_wait, s_prev, s_hold[32];
float s_x, s_y;
const u16 *s_action = reinterpret_cast<const u16 *>(u"複製");
u32 s_sound = 0x01000395;
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
        s_action, reinterpret_cast<const u16 *>(u"やめる")
    };
    const u32 sounds[] = {s_sound, 0x01000392};
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
    std::memset(s_touch, 0, sizeof(s_touch));
    *reinterpret_cast<s16 *>(s_touch+2)=-1;
    *reinterpret_cast<s16 *>(s_touch+4)=-1;
    s_touch[15]=s_touch[25]=200;
    s_touchBlocked=Touch::IsDown();
    s_opened = true;
}
void UpdateTouch(void) {
    const bool down=Touch::IsDown();
    if(!down)s_touchBlocked=false;
    const bool held=down && !s_touchBlocked;
    // 背景を遮断しても選択窓へはpress/release一式を渡す。開いた指の再入力は離上後だけ。
    std::memcpy(s_touch+16,s_touch+6,8);
    std::memcpy(s_touch+24,s_touch+14,2);
    s_touch[26]=s_touch[0];s_touch[27]=s_touch[1];
    std::memcpy(s_touch+28,s_touch+4,2);
    s_touch[1]=s_touch[0]^(held?1u:0u);s_touch[0]=held?1u:0u;
    if(held) {
        const UIntVector pos=Touch::GetPosition();
        *reinterpret_cast<s16 *>(s_touch+2)=static_cast<s16>(pos.x);
        *reinterpret_cast<s16 *>(s_touch+4)=static_cast<s16>(pos.y);
        if(!s_touch[26]) {std::memcpy(s_touch+6,s_touch+2,4);s_touch[14]=0;}
        if(s_touch[14]<200)++s_touch[14];
        std::memcpy(s_touch+10,s_touch+2,4);
    } else if(s_touch[26])s_touch[15]=1;
    else if(s_touch[15]<200)++s_touch[15];
    *reinterpret_cast<float *>(s_touch+32)=static_cast<float>(*reinterpret_cast<s16 *>(s_touch+10))-160.0f;
    *reinterpret_cast<float *>(s_touch+36)=120.0f-static_cast<float>(*reinterpret_cast<s16 *>(s_touch+12));
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
    UpdateTouch();
    u8 oldTouch[sizeof(s_touch)];
    void *gameTouch=reinterpret_cast<void *>(0x00ACF6E0);
    std::memcpy(oldTouch,gameTouch,sizeof(oldTouch));
    std::memcpy(gameTouch,s_touch,sizeof(s_touch));
    reinterpret_cast<void (*)(void *)>(0x002BABAC)(s_window);
    std::memcpy(gameTouch,oldTouch,sizeof(oldTouch));
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
    return Open(x,y,reinterpret_cast<const u16 *>(u"複製"),0x01000395);
}
bool Open(float x, float y, const u16 *action, u32 sound) {
    if (Busy()) return false;
    if (!action || !action[0]) return false;
    s_action = action; s_sound = sound;
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

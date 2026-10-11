#include "PlayerActions.hpp"
#include "Cheats.hpp"
#include "GuiMenu.hpp"
#include "GridCursor.hpp"
#include "NativePortGuard.hpp"
#include <cstring>
using namespace CTRPluginFramework;

namespace PlayerActions {
namespace {
const NativePortGuard::Word kWords[] = {
    {0x305F6C,0xE59F0004}, {0x5C27D8,0xE3500004}, {0x6561F0,0xE92D4070},
    {0x5D3504,0xED910A00}, {0x64C688,0xE92D5FF0}, {0x676284,0xE92D41F0}
};
int s_idle = -1;
bool s_pressed;
u32 s_request, s_result;

void FrameStep() {
    const u32 state = __atomic_exchange_n(&s_request, 0u, __ATOMIC_ACQ_REL);
    if (!state) return;
    if (!NativePortGuard::Heap(NativePortGuard::Read(0x94D644))) {
        __atomic_store_n(&s_result, state, __ATOMIC_RELEASE); return;
    }
    const u32 index = reinterpret_cast<u32 (*)()>(0x305F6C)();
    const u32 actor = reinterpret_cast<u32 (*)(u32,u32)>(0x5C27D8)(index,1);
    if (!NativePortGuard::Heap(actor)) {
        __atomic_store_n(&s_result, state, __ATOMIC_RELEASE); return;
    }
    // 立ち状態も気絶も現在位置のpacketが必要。前の動作の目標座標を使うと復帰で跳ぶ。
    const void *source = reinterpret_cast<void *(*)(u32)>(0x6561F0)(actor);
    alignas(4) u8 packet[0x26];
    std::memcpy(packet, source, sizeof(packet));
    u32 packed;
    reinterpret_cast<void (*)(void *,const void *)>(0x5D3504)(&packed, reinterpret_cast<void *>(actor+0x14));
    std::memcpy(packet+2,&packed,sizeof(packed));
    if (state == 0x9D) {
        packet[0xE] = 0;
        std::memcpy(packet+0xF,&packed,sizeof(packed));
        packet[0x13] = *reinterpret_cast<const u8 *>(actor+0x2F);
    }
    // native SetStateが自分の状態packetを通信へ送る。別の送信呼出しは重ねない。
    reinterpret_cast<void (*)(u32,u32,const void *,u32)>(0x64C688)(actor,state,packet,0);
}
void Request(u32 state) {
    if (!NativePortGuard::Matches(kWords) || !GridCursor::InstallFrameHook() || !GridCursor::AddExtraFrameStep(FrameStep)) {
        GuiMenu::NotifyRed(Cheats::kIdle,u8"この版の処理を確認できません。"); return;
    }
    u32 expected = 0;
    if (!__atomic_compare_exchange_n(&s_request,&expected,state,false,__ATOMIC_RELEASE,__ATOMIC_RELAXED))
        GuiMenu::NotifyRed(Cheats::kIdle,u8"前の操作を処理中です。");
}
void Faint(int) { Request(0x9D); }
}
void Wire() {
    s_idle = GuiMenu::FindItem(Cheats::kIdle);
    const int faint = GuiMenu::FindItem(Cheats::kFaint);
    if (faint >= 0) GuiMenu::RegisterExecute(faint,Faint);
}
bool Tick(int index,u16 held) {
    if (index != s_idle || index < 0) return false;
    const u16 key = GuiMenu::ItemAppliedHotkey(index);
    const bool pressed = key == 0 || (held & key) == key;
    if (pressed && !s_pressed) Request(6);
    s_pressed = pressed;
    return true;
}
void Service() {
    const u32 failure = __atomic_exchange_n(&s_result,0u,__ATOMIC_ACQ_REL);
    if (failure) GuiMenu::NotifyRed(failure == 0x9D ? Cheats::kFaint : Cheats::kIdle,u8"プレイヤーを取得できません。");
}
bool Disable(int index) {
    if (index != s_idle || index < 0) return false;
    s_pressed = false; return true;
}
}

#include <CTRPluginFramework.hpp>

#include "HhdRoomCamera.hpp"
#include "HhdRoomCameraControl.hpp"
#include "Cheats.hpp"
#include "DecorCatalog.hpp"
#include "GuiMenu.hpp"
#include "InteriorTools.hpp"

#include <cstring>
#include <3ds.h>

// CTRPF's existing MITM trampoline accessor. OriginalFunction<float> cannot
// compile in this CTRPF version because its null fallback casts a pointer to
// float; call the same trampoline with the native hard-float signature.
extern "C" void *__ctrpfHookCtx__GetCallerCode(CTRPluginFramework::HookContext *context);

// IDA-gpt-6.1-sol-F014: ACNL JPN original + update only.
// Input follows the native player-state gates. Camera base/follow, eye creation
// and wall transparency stay in the native camera update.
namespace HhdRoomCamera {
namespace {
using namespace CTRPluginFramework;
namespace Control = HhdRoomCameraControl;

const u32 kInput = 0x001A386C, kInputOriginal = 0xE92D41F0;
const u32 kApplyTarget = 0x001A5124, kApplyOriginal = 0xE92D41F0;
Hook s_inputHook, s_applyHook;
bool s_hooked;
int s_index = -1;
u32 s_request;  // bit0 enabled, remaining bits revision; atomically published.

// Game thread only. ON/OFF requests never write these from the menu thread.
u32 s_camera, s_scene, s_seenEpoch;
u8 s_room;
bool s_live, s_applied, s_inputSeen;
Control::State s_control;
Control::PanState s_pan;

u32 R32(u32 address) { return *reinterpret_cast<const volatile u32 *>(address); }
u8 R8(u32 address) { return *reinterpret_cast<const volatile u8 *>(address); }
u32 Request(void) { return __atomic_load_n(&s_request, __ATOMIC_ACQUIRE); }
bool HeapPointer(u32 p) {
    return p >= 0x08000000u && p < 0x40000000u && (p & 3u) == 0u;
}
void StopVelocity(void) {
    s_control.yawVelocity = s_control.zoomVelocity = 0.0f;
    s_inputSeen = false;
}
void Clear(void) {
    StopVelocity();
    s_pan = {0.0f, 0.0f};
    s_live = s_applied = false;
}
bool InHouse(u32 camera) {
    if (!HeapPointer(camera) || R32(0x0094A880) != camera
        || !Process::CheckAddress(camera + 444, MEMPERM_READ))
        return false;
    const u32 room = R8(0x0095133A);
    if (room >= 200)
        return false;
    const u32 flags = R32(0x00883328 + room * 4);
    return (flags & 0x108u) == 0x108u && (flags & 0xC0u) != 0;
}
bool SameContext(u32 camera) {
    return s_live && camera == s_camera && R8(0x0095133A) == s_room
        && R32(0x00948E70) == s_scene;
}
bool RoomMode(u32 camera) {
    const u8 mode = R8(camera + 242);
    return (mode == 0 || mode == 25) && mode == R8(camera + 243);
}
bool InputAllowed(u32 camera) {
    if (!RoomMode(camera) || R8(camera + 442) || GuiMenu::IsVisible() || InteriorTools::InputBusy())
        return false;
    const s8 interpolation = *reinterpret_cast<const s8 *>(camera + 309);
    if (interpolation == -1)
        return true;
    if (interpolation < 0)
        return false;
    typedef const float *(*ParamsFn)(int);
    const float *params = reinterpret_cast<ParamsFn>(0x006E158C)(interpolation);
    return params[1] * 0.5f <= *reinterpret_cast<const float *>(camera + 340);
}
u32 ControllerPtr(void) {
    const u32 manager = R32(0x0096F2EC);
    if (!HeapPointer(manager) || !Process::CheckAddress(manager + 216, MEMPERM_READ)
        || R32(manager + 208) == 0)
        return 0;
    const u32 array = R32(manager + 216);
    if (!HeapPointer(array) || !Process::CheckAddress(array, MEMPERM_READ))
        return 0;
    const u32 controller = R32(array);
    return HeapPointer(controller) && Process::CheckAddress(controller + 272, MEMPERM_READ)
        ? controller : 0;
}
u32 Held(void) {
    // Catalog keeps native editor/player input blocked. Read only camera keys
    // from CTRPF while it has focus, translating to the native camera mask.
    if (DecorCatalog::IsListOpen()) {
        const u32 keys = Controller::GetKeysDown();
        return ((keys & (u32)Key::DPadUp) ? 0x10000u : 0u)
             | ((keys & (u32)Key::DPadDown) ? 0x20000u : 0u)
             | ((keys & (u32)Key::DPadLeft) ? 0x40000u : 0u)
             | ((keys & (u32)Key::DPadRight) ? 0x80000u : 0u);
    }
    typedef bool (*BlockedFn)(void);
    if (reinterpret_cast<BlockedFn>(0x005CDAE4)())
        return 0;
    const u32 controller = ControllerPtr();
    return controller ? R32(controller + 272) : 0;
}
void EnsureContext(u32 camera, u32 request) {
    if (SameContext(camera) && s_seenEpoch == request)
        return;
    s_camera = camera;
    s_room = R8(0x0095133A);
    s_scene = R32(0x00948E70);
    s_seenEpoch = request;
    s_control = {0.0f, 0.0f, 1.0f, *reinterpret_cast<const u16 *>(camera + 30)};
    s_pan = {0.0f, 0.0f};
    s_live = true;
    s_applied = s_inputSeen = false;
}
void ApplyProfile(u32 camera, const Control::Profile &profile) {
    typedef void (*ApplyFn)(u32, const Control::Profile *);
    reinterpret_cast<ApplyFn>(0x001A3160)(camera, &profile);
}
void RestoreNativeProfile(u32 camera) {
    const u32 table = R32(camera + 316);
    if (table == 0 || !Process::CheckAddress(table + 2, MEMPERM_READ))
        return;
    const u8 vertical = R8(camera + 425);
    const u8 profile = R8(table + (vertical < 3 ? vertical : 1));
    Control::Profile value;
    typedef void (*CopyFn)(u32, Control::Profile *, int, const u16 *);
    reinterpret_cast<CopyFn>(0x001A4DB8)(camera, &value, profile, nullptr);
    ApplyProfile(camera, value);
}

__attribute__((noinline)) u32 InputHook(u32 camera) {
    HookContext &context = HookContext::GetCurrent();
    const u32 request = Request();
    if (!(request & 1u) || !InHouse(camera))
        return context.OriginalFunction<u32>(camera);
    if (!InputAllowed(camera)) {
        StopVelocity();
        return 0;
    }
    EnsureContext(camera, request);
    Control::Step(s_control, Held());
    // Only decoration uses the circle pad for camera movement. Ordinary room
    // movement remains native. Catalog focus permits camera input (F016).
    typedef bool (*BlockedFn)(void);
    if (R8(camera + 242) == 25 && DecorCatalog::IsEditorOpen()
        && ControllerPtr() != 0
        && (DecorCatalog::IsListOpen() || !reinterpret_cast<BlockedFn>(0x005CDAE4)())) {
        circlePosition pad = {0, 0};
        hidCircleRead(&pad);
        typedef float (*AngleFn)(u32);
        const float sine = reinterpret_cast<AngleFn>(0x0047D2C4)(s_control.yaw);
        const float cosine = reinterpret_cast<AngleFn>(0x0047D28C)(s_control.yaw);
        Control::Pan(s_pan, pad.dx / 156.0f, pad.dy / 156.0f, sine, cosine);
    }
    s_inputSeen = true;
    return 0;
}

// Native ApplyTarget returns its transition fraction in S0. C++ is built with
// -mfloat-abi=hard: float preserves that ABI both through MITM and on return.
__attribute__((noinline)) float ApplyTargetHook(u32 camera, float incomingFraction) {
    HookContext &context = HookContext::GetCurrent();
    const bool house = InHouse(camera), mode = house && RoomMode(camera);
    const u32 request = Request();
    const bool on = (request & 1u) != 0;
    bool apply = false;
    if (!house || (s_live && !SameContext(camera)))
        Clear();
    if (on && mode && !R8(camera + 442)) {
        EnsureContext(camera, request);
        if (!s_inputSeen)
            StopVelocity();
        ApplyProfile(camera, Control::Sample(s_control));
        s_applied = apply = true;
    } else {
        StopVelocity();
        if (!on && SameContext(camera)) {
            if (mode && !R8(camera + 442)) {
                if (s_applied)
                    RestoreNativeProfile(camera);
                Clear();
            } else if (!mode) {
                Clear();    // Native special-mode entry already owns its profile.
            }
        }
    }
    s_inputSeen = false;
    // FieldCamera's outdoor POP patch returns the incoming S0 unchanged. Capture
    // it at callback entry and supply it again to the original trampoline;
    // the unpatched native function produces its usual fraction itself.
    typedef float (*OriginalApplyFn)(u32, float);
    const float fraction = reinterpret_cast<OriginalApplyFn>(__ctrpfHookCtx__GetCallerCode(&context))(camera, incomingFraction);
    if (apply) {
        // Native ApplyTarget reconstructs the base at +4..+12 each frame before
        // adding the private decoration pan. The controller smooths the profile;
        // a second discrete transition would lag it.
        std::memcpy(reinterpret_cast<void *>(camera + 16), reinterpret_cast<const void *>(camera + 288), 12);
        std::memcpy(reinterpret_cast<void *>(camera + 28), reinterpret_cast<const void *>(camera + 300), 8);
        if (R8(camera + 242) == 25) {
            *reinterpret_cast<float *>(camera + 4) += s_pan.x;
            *reinterpret_cast<float *>(camera + 12) += s_pan.z;
        }
    }
    return fraction;
}

bool Install(void) {
    if (s_hooked)
        return s_inputHook.IsEnabled() && s_applyHook.IsEnabled();
    if (Process::GetTitleID() != 0x0004000000086200ULL)
        return false;
    // Independent version sites; field-camera's 0x1A5128 may be owned outdoors.
    static const struct { u32 address, word; } expected[] = {
        {kInput, kInputOriginal}, {kApplyTarget, kApplyOriginal},
        {0x001A3160, 0xE92D4070}, {0x001A4DB8, 0xE92D4070},
        {0x006E158C, 0xE59F1004}, {0x005CDAE4, 0xE59F0024},
        {0x0047D2C4, 0xE20010FF}, {0x0047D28C, 0xE20010FF},
    };
    for (const auto &entry : expected)
        if (R32(entry.address) != entry.word)
            return false;
    s_applyHook.InitializeForMitm(kApplyTarget, reinterpret_cast<u32>(ApplyTargetHook));
    if (s_applyHook.Enable() != HookResult::Success)
        return false;
    s_inputHook.InitializeForMitm(kInput, reinterpret_cast<u32>(InputHook));
    if (s_inputHook.Enable() != HookResult::Success) {
        s_applyHook.Disable();
        return false;
    }
    s_hooked = true;
    return true;
}
bool Active(int index) {
    return index == s_index && (Request() & 1u) != 0;
}
void SetActive(int index, bool on) {
    if (index != s_index)
        return;
    if (on && !Install()) {
        GuiMenu::NotifyRed(Cheats::kHhdRoomCamera, u8"カメラの処理を使えません。");
        return;
    }
    const u32 request = Request();
    if (((request & 1u) != 0) != on)
        __atomic_store_n(&s_request, ((request + 2u) & ~1u) | (on ? 1u : 0u), __ATOMIC_RELEASE);
}
const GuiMenu::ToggleEffectFuncs kEffect = {Active, SetActive};
}  // namespace

void Wire(void) {
    s_index = CTRPluginFramework::GuiMenu::FindItem(CTRPluginFramework::Cheats::kHhdRoomCamera);
    if (s_index >= 0)
        CTRPluginFramework::GuiMenu::RegisterToggleEffect(s_index, &kEffect);
}
}  // namespace HhdRoomCamera

#include "game/hook_batch.hpp"
#include "core/limits.hpp"
#include <CTRPluginFramework.hpp>

namespace gohan::game {
namespace {
using namespace CTRPluginFramework;
struct Slot { Hook hook; void *owner = nullptr; unsigned index = 0; };
Slot slots[core::limits::GameHooks];
bool Pause() { const bool resume = !Process::IsPaused(); if (resume) Process::Pause(); return resume; }
void Resume(bool resume) { if (resume) Process::Play(); }
bool Read(std::uintptr_t address, std::uint8_t &byte) { return Process::Read8(address, byte); }
bool Install(void *owner, unsigned index, const HookSpec &spec) {
    for (auto &slot : slots) if (!slot.owner) {
        if (spec.kind == HookKind::Mitm) slot.hook.InitializeForMitm(spec.check.address, spec.callback);
        else { slot.hook.Initialize(spec.check.address, spec.callback); slot.hook.SetFlags(spec.flags); }
        if (slot.hook.Enable() != HookResult::Success) { slot.hook.Disable(); return false; }
        slot.owner = owner; slot.index = index;
        return true;
    }
    return false;
}
void Remove(void *owner, unsigned index) {
    for (auto &slot : slots) if (slot.owner == owner && slot.index == index) {
        slot.hook.Disable(); slot.owner = nullptr; return;
    }
}
const HookBackend backend{IsSupportedTitle, Pause, Resume, Read, Install, Remove};
}
const HookBackend &NativeHookBackend() { return backend; }
bool VerifyNative(std::span<const CodeCheck> checks) {
    if (!backend.supported()) return false;
    const bool resume = backend.pause();
    const bool ok = VerifyCode(backend, checks);
    backend.resume(resume);
    return ok;
}
} // namespace gohan::game

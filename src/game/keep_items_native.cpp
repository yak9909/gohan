#include "game/keep_items.hpp"
#include "game/generated/api.hpp"
#include "game/hook_batch.hpp"
#include <CTRPluginFramework.hpp>

namespace gohan::game::keep_items {
namespace {
using namespace CTRPluginFramework;
bool active = false, ready = false;
__attribute__((noinline)) std::uint32_t SetItem(std::uint32_t map, const std::uint32_t *item, unsigned x, unsigned y, std::uint32_t height_bits) {
    auto &context = HookContext::GetCurrent();
    const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    std::uint32_t replacement;
    if (active && GrowthCaller(caller, GrowthReturns())) {
        const auto *tile = reinterpret_cast<const std::uint32_t *(*)(std::uint32_t, unsigned, unsigned, unsigned)>(generated::Field_GetItemAtXY_addr)(map, x, y, 0);
        if (tile) { replacement = NextGrowth(*tile); item = &replacement; }
    }
    // 第 5 引数の float は生のビットで渡し、元の stack ABI を保つ。
    return context.OriginalFunction<std::uint32_t>(map, item, x, y, height_bits);
}
const HookSpec hooks[] = {{{generated::KeepSetItem_addr, generated::KeepSetItem_len, generated::KeepSetItem_crc32}, reinterpret_cast<std::uintptr_t>(SetItem), HookKind::Mitm}};
HookBatch batch(NativeHookBackend(), hooks);
const PatchWord words[] = {
    {generated::KeepDelete_addr, 0xE3A00000u, generated::KeepDelete_crc32},
    {generated::KeepFootprint_addr, 0xE1A00000u, generated::KeepFootprint_crc32},
};
PatchSet patches(NativePatchMemory(), words);
class Pause {
  public:
    Pause() : resume_(!Process::IsPaused()) { if (resume_) Process::Pause(); }
    ~Pause() { if (resume_) Process::Play(); }
  private: bool resume_;
};
}
bool Wire() {
    const CodeCheck checks[] = {
        {generated::GetTileEntry_addr, 4, generated::GetTileEntry_crc32},
        {generated::KeepBushCaller_addr, 4, generated::KeepBushCaller_crc32},
        {generated::KeepSaplingCaller0_addr, 4, generated::KeepSaplingCaller0_crc32},
        {generated::KeepSaplingCaller1_addr, 4, generated::KeepSaplingCaller1_crc32},
        {generated::KeepItem3Caller_addr, 4, generated::KeepItem3Caller_crc32},
        {generated::KeepItem4Caller_addr, 4, generated::KeepItem4Caller_crc32},
    };
    active = false;
    if (!VerifyNative(checks)) return false;
    Pause pause;
    if (patches.Wire() != Result::Ok) return false;
    if (batch.Wire() != Result::Ok) { patches.Shutdown(); return false; }
    ready = true; return true;
}
Result Set(bool on) {
    if (!ready) return Result::Unsupported;
    const auto result = patches.Set(on);
    active = on && result == Result::Ok;
    return result;
}
bool IsActive() { return active && patches.IsApplied(); }
void Shutdown() { Pause pause; active = false; patches.Shutdown(); batch.Shutdown(); ready = false; }
} // namespace gohan::game::keep_items

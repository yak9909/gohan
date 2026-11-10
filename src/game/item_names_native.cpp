#include "game/item_names.hpp"
#include "game/hook_batch.hpp"
#include "game/script_word_res.hpp"
#include "core/frame.hpp"
#include <CTRPluginFramework.hpp>

namespace gohan::game::items {
namespace {
bool ready = false;
__attribute__((noinline)) std::uint32_t Resolve(void *phrase, const std::uint16_t *item, std::uint32_t a3) {
    const auto result = CTRPluginFramework::HookContext::GetCurrent().OriginalFunction<std::uint32_t>(phrase, item, a3);
    const auto *entry = item ? SharedNames().CustomName(*item) : nullptr;
    if (phrase && entry) {
        const auto *vtable = *static_cast<const std::uintptr_t **>(phrase);
        const script::WordRes word(entry->name16);
        reinterpret_cast<void (*)(void *)>(vtable[7])(phrase);
        reinterpret_cast<void (*)(void *, const script::WordRes *)>(vtable[5])(phrase, &word);
    }
    return result;
}
const HookSpec hooks[] = {{{generated::ItemNameResolve_addr, generated::ItemNameResolve_len, generated::ItemNameResolve_crc32},
    reinterpret_cast<std::uintptr_t>(Resolve), HookKind::Mitm}};
HookBatch batch(NativeHookBackend(), hooks);
void Poll() { SharedNames().Poll(); }
bool Wire() {
    // 名前の差し替えが導入できなくても、通知用の名前表の読み取りは使える。
    ready = batch.Wire() == Result::Ok;
    const auto change = core::frame::Global().Add(Poll, -40);
    if (change.result != core::frame::Result::Queued && change.result != core::frame::Result::Unchanged) {
        ready = false; batch.Shutdown(); return false;
    }
    return true;
}
void Shutdown() {
    PausedGame pause;
    ready = false;
    batch.Shutdown();
    core::frame::Global().Remove(Poll);
    SharedNames().Shutdown();
}
const core::Feature feature{"game.item_names", Wire, Shutdown, core::ShutdownStage::GameHooks};
}
const core::Feature &NamesFeature() { return feature; }
bool NamesHookReady() { return ready; }
} // namespace gohan::game::items

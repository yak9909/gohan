#include "game/shizue_skip.hpp"
#include "game/generated/api.hpp"
#include "game/hook_batch.hpp"
#include "game/memory.hpp"
#include "core/signal.hpp"
#include <3ds.h>

extern "C" void gohan_shizue_skip_callback();

namespace gohan::game::shizue_skip {
namespace {
bool ready = false;
core::Signal active, notice;
bool Query(std::uintptr_t address, memory::Region &region) {
    MemInfo info; PageInfo page;
    if (R_FAILED(svcQueryMemory(&info, &page, address))) return false;
    region = {info.base_addr, info.size, info.state != MEMSTATE_FREE && (info.perm & MEMPERM_READ) != 0};
    return true;
}
// 描画スレッド用の可読性キャッシュは共有しない。
memory::Reader reader({Query, nullptr});
bool Readable(std::uintptr_t address, std::size_t size) { return reader.Readable(address, size); }
unsigned Room() { return *reinterpret_cast<const generated::g_CurrentRoomId_t *>(generated::g_CurrentRoomId_addr); }
bool Resolve(Exit &exit) {
    const ExitAccess access{
        reinterpret_cast<void *(*)()>(generated::Save_GetCurrentPlayer_addr),
        reinterpret_cast<void *(*)()>(generated::vc_D_GARDEN_addr),
        reinterpret_cast<int (*)(void *, void *)>(generated::PlayerSave_GetSlotIndex_addr),
        reinterpret_cast<void *(*)()>(generated::vc_ROOMDAT_addr),
        Readable,
    };
    return ResolveExit(access, exit);
}
bool Prepared() { return *reinterpret_cast<const generated::g_VillagerDailyUpdateGuard_t *>(generated::g_VillagerDailyUpdateGuard_addr) != 0; }
void Prepare() { reinterpret_cast<generated::Villager_RunDailyUpdateOnce_t>(generated::Villager_RunDailyUpdateOnce_addr)(); }
void UseExit(const Exit &exit) { reinterpret_cast<generated::Room_UseExit_t>(generated::Room_UseExit_addr)(exit.manager, exit.slot, 1, 0, 1, 1); }
Sequence sequence({Room, Resolve, Prepared, Prepare, UseExit});
// 旧版と同じ ProcOneFrame 入口。PUSH {r4-r6,lr} の CRC32。
const HookSpec hooks[] = {{{0x0054C424, 4, 0xC31DF273u},
    reinterpret_cast<std::uintptr_t>(gohan_shizue_skip_callback)}};
HookBatch batch(NativeHookBackend(), hooks);
}
bool Wire() {
    const CodeCheck checks[] = {
        {generated::CurrentSaveEntry_addr, 4, generated::CurrentSaveEntry_crc32},
        {generated::GardenEntry_addr, 4, generated::GardenEntry_crc32},
        {generated::SlotIndexEntry_addr, 4, generated::SlotIndexEntry_crc32},
        {generated::RoomManagerEntry_addr, 4, generated::RoomManagerEntry_crc32},
        {generated::DailyUpdateEntry_addr, 4, generated::DailyUpdateEntry_crc32},
        {generated::UseExitEntry_addr, 4, generated::UseExitEntry_crc32},
    };
    PausedGame pause;
    if (ready) return true;
    sequence.Reset(); active.Set(false); notice.Set(false);
    if (!VerifyNative(checks) || batch.Wire() != Result::Ok) return false;
    ready = true; return true;
}
void Shutdown() {
    PausedGame pause;
    batch.Shutdown(); active.Set(false); notice.Set(false); ready = false; sequence.Reset();
}
Result Set(bool on) { if (!ready) return Result::Unsupported; active.Set(on); return Result::Ok; }
bool IsActive() { return active.Get(); }
bool TakeNotice() { return notice.Take(); }
// この入口は ASM からだけ呼ぶ。Sequence とゲームの関数はメインスレッド専用。
extern "C" void gohan_shizue_skip_tick() {
    reader.BeginFrame();
    if (sequence.Step(active.Get())) notice.Set();
}
} // namespace gohan::game::shizue_skip

#include "game/shizue_skip.hpp"
#include "game/generated/api.hpp"
#include "game/hook_batch.hpp"
#include "game/memory.hpp"

namespace gohan::game::shizue_skip {
namespace {
bool ready = false, active = false;
unsigned Room() { return *reinterpret_cast<const generated::g_CurrentRoomId_t *>(generated::g_CurrentRoomId_addr); }
bool Resolve(Exit &exit) {
    auto *save = reinterpret_cast<void *(*)()>(generated::Save_GetCurrentPlayer_addr)();
    auto *garden = reinterpret_cast<std::uint8_t *(*)()>(generated::vc_D_GARDEN_addr)();
    if (!memory::IsHeapPointer(reinterpret_cast<std::uintptr_t>(save)) ||
        !memory::Readable(reinterpret_cast<std::uintptr_t>(save), 4) ||
        !memory::IsHeapPointer(reinterpret_cast<std::uintptr_t>(garden))) return false;
    const auto array = reinterpret_cast<std::uintptr_t>(garden) + 0xA0;
    if (!memory::Readable(array, 4)) return false;
    exit.slot = reinterpret_cast<generated::PlayerSave_GetSlotIndex_t>(generated::PlayerSave_GetSlotIndex_addr)(reinterpret_cast<void *>(array), save);
    if (exit.slot < 0 || exit.slot > 3) return false;
    exit.manager = reinterpret_cast<void *(*)()>(generated::vc_ROOMDAT_addr)();
    return memory::IsHeapPointer(reinterpret_cast<std::uintptr_t>(exit.manager)) && memory::Readable(reinterpret_cast<std::uintptr_t>(exit.manager), 4);
}
bool Prepared() { return *reinterpret_cast<const generated::g_VillagerDailyUpdateGuard_t *>(generated::g_VillagerDailyUpdateGuard_addr) != 0; }
void Prepare() { reinterpret_cast<generated::Villager_RunDailyUpdateOnce_t>(generated::Villager_RunDailyUpdateOnce_addr)(); }
void UseExit(const Exit &exit) { reinterpret_cast<generated::Room_UseExit_t>(generated::Room_UseExit_addr)(exit.manager, exit.slot, 1, 0, 1, 1); }
Sequence sequence({Room, Resolve, Prepared, Prepare, UseExit});
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
    sequence.Reset(); active = false; ready = VerifyNative(checks); return ready;
}
void Shutdown() { active = ready = false; sequence.Reset(); }
Result Set(bool on) { if (!ready) return Result::Unsupported; active = on; return Result::Ok; }
bool IsActive() { return active; }
bool Step() { return ready && sequence.Step(active); }
} // namespace gohan::game::shizue_skip

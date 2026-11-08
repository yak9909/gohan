#include "game/patch_set.hpp"
#include <CTRPluginFramework.hpp>
#include <3ds.h>
#include <csvc.h>

namespace gohan::game {
namespace {
bool Read(std::uintptr_t address, std::uint32_t &word) {
    return CTRPluginFramework::Process::Read32(address, word);
}
bool Write(std::uintptr_t address, std::uint32_t word) {
    return CTRPluginFramework::Process::Write32(address, word);
}
void Sync(std::uintptr_t address, std::size_t bytes) {
    svcFlushProcessDataCache(CUR_PROCESS_HANDLE, address, bytes);
    svcInvalidateEntireInstructionCache();
}
const PatchMemory memory{Read, Write, Sync};
} // namespace
const PatchMemory &NativePatchMemory() { return memory; }
bool IsSupportedTitle() { return CTRPluginFramework::Process::GetTitleID() == 0x0004000000086200ULL; }
} // namespace gohan::game

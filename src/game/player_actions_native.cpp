#include "game/player_actions.hpp"
#include "game/player.hpp"
#include "game/memory.hpp"
#include "game/hook_batch.hpp"

namespace gohan::game::player_actions {
namespace {
bool ready = false;
bool NetworkReady() {
    auto *manager = *reinterpret_cast<generated::g_NetGameMgrPtr_t *>(generated::g_NetGameMgrPtr_addr);
    const auto address = reinterpret_cast<std::uintptr_t>(manager);
    return memory::IsHeapPointer(address) && memory::Readable(address, 4);
}
const void *Packet(void *actor) { return reinterpret_cast<generated::Player_CopyStatePacketForEdit_t>(generated::Player_CopyStatePacketForEdit_addr)(actor); }
void Pack(void *out, const float *xyz) { reinterpret_cast<generated::StatePacket_PackXZ_t>(generated::StatePacket_PackXZ_addr)(static_cast<std::int16_t *>(out), xyz); }
void SetState(void *actor, unsigned state, const void *packet, unsigned zero) {
    reinterpret_cast<void (*)(void *, unsigned, const void *, unsigned)>(generated::Actor_SetState_addr)(actor, state, packet, zero);
}
const Access access{NetworkReady, player::LocalActor, Packet, memory::Readable, Pack, SetState};
}
bool Wire() {
    const CodeCheck checks[] = {
        {generated::LocalIndexEntry_addr, 4, generated::LocalIndexEntry_crc32},
        {generated::LocalActorEntry_addr, 4, generated::LocalActorEntry_crc32},
        {generated::CopyStateEntry_addr, 4, generated::CopyStateEntry_crc32},
        {generated::PackXZEntry_addr, 4, generated::PackXZEntry_crc32},
        {generated::SetStateEntry_addr, 4, generated::SetStateEntry_crc32},
        {generated::FaintEntry_addr, 4, generated::FaintEntry_crc32},
    };
    ready = VerifyNative(checks);
    return ready;
}
Result Run(bool faint) { return ready ? Run(access, faint) : Result::Unsupported; }
} // namespace gohan::game::player_actions

#include "game/player.hpp"
#include "game/memory.hpp"

namespace gohan::game::player {
unsigned LocalIndex() {
    const auto manager = reinterpret_cast<std::uintptr_t>(*reinterpret_cast<generated::g_NetGameMgrPtr_t *>(generated::g_NetGameMgrPtr_addr));
    if (!memory::IsHeapPointer(manager) || !memory::Readable(manager, 4)) return 4;
    return reinterpret_cast<std::uint8_t (*)()>(generated::vc_A_GETONLINEPLAYERINDEX_addr)();
}
void *LocalActor() {
    const auto index = LocalIndex();
    if (index > 3) return nullptr;
    auto *actor = reinterpret_cast<void *(*)(unsigned, unsigned)>(generated::vc_PLAYERINSTANCE_addr)(index, 1);
    const auto address = reinterpret_cast<std::uintptr_t>(actor);
    return memory::Readable(address, 0x1AA) && memory::IsClass(address, "8AcPlayer") ? actor : nullptr;
}
generated::AcnlPlayerSave *LocalSave() {
    auto *save = reinterpret_cast<generated::Save_GetPlayerByOnlineSlot_t>(generated::Save_GetPlayerByOnlineSlot_addr)(4);
    return memory::IsHeapPointer(reinterpret_cast<std::uintptr_t>(save)) &&
        memory::Readable(reinterpret_cast<std::uintptr_t>(save), sizeof(*save)) ? save : nullptr;
}
bool Digging(void *actor) {
    const auto state = static_cast<const std::uint8_t *>(actor)[0x1A9];
    return state == 0x4F || state == 0x52;
}
bool WorldGrid(unsigned &x, unsigned &z) {
    const auto index = LocalIndex();
    return index < 4 && reinterpret_cast<bool (*)(unsigned *, unsigned *, unsigned, unsigned)>(generated::vc_WORLDCOORDS_addr)(&x, &z, index, 1);
}
void Translate(void *actor, float x, float z) {
    auto *position = reinterpret_cast<float *>(static_cast<std::uint8_t *>(actor) + 0x14);
    position[0] += x; position[2] += z;
}
void SetXZ(void *actor, float x, float z) {
    auto *position = reinterpret_cast<float *>(static_cast<std::uint8_t *>(actor) + 0x14);
    position[0] = x; position[2] = z;
}
} // namespace gohan::game::player

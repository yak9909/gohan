#include "game/player_motion.hpp"
#include "game/generated/api.hpp"
#include "game/hook_batch.hpp"
#include "game/memory.hpp"

namespace gohan::game::player_motion {
bool Wire() {
    const CodeCheck checks[] = {
        {generated::LocalIndexEntry_addr, generated::LocalIndexEntry_len, generated::LocalIndexEntry_crc32},
        {generated::LocalActorEntry_addr, generated::LocalActorEntry_len, generated::LocalActorEntry_crc32},
        {generated::WorldCoordsEntry_addr, generated::WorldCoordsEntry_len, generated::WorldCoordsEntry_crc32},
    };
    return VerifyNative(checks);
}
bool MapCoordinates(int x, int y, Direction &world) {
    if (!*reinterpret_cast<const generated::vc_MAPBOOL_t *>(generated::vc_MAPBOOL_addr)) return false;
    const unsigned room = *reinterpret_cast<const generated::g_CurrentRoomId_t *>(generated::g_CurrentRoomId_addr);
    bool opened = false;
    if (room == 0) {
        const auto info = *reinterpret_cast<const std::uint32_t *>(generated::vc_MAPBOOL_addr + 0x1C);
        if (info && memory::Readable(info + 0x5D8, 1)) opened = *reinterpret_cast<const std::uint8_t *>(info + 0x5D8) != 0;
    }
    return TouchCoordinates(room, opened, x, y, world);
}
} // namespace gohan::game::player_motion

#include "game/player_actions.hpp"
#include <cstring>

namespace gohan::game::player_actions {
Result Run(const Access &access, bool faint) {
    if (!access.network_ready()) return Result::NotReady;
    auto *actor = access.actor();
    if (!actor || !access.readable(reinterpret_cast<std::uintptr_t>(actor), 0x30)) return Result::NotReady;
    const void *source = access.packet(actor);
    if (!source || !access.readable(reinterpret_cast<std::uintptr_t>(source), 0x26)) return Result::NotReady;
    alignas(4) std::uint8_t packet[0x26];
    std::memcpy(packet, source, sizeof(packet));
    std::uint32_t packed;
    access.pack(&packed, reinterpret_cast<const float *>(static_cast<const std::uint8_t *>(actor) + 0x14));
    std::memcpy(packet + 2, &packed, 4);
    if (faint) {
        packet[0xE] = 0;
        std::memcpy(packet + 0xF, &packed, 4);
        packet[0x13] = static_cast<const std::uint8_t *>(actor)[0x2F];
    }
    // この呼び出しが通信の送信も受け持つ。別の送信を重ねない。
    access.set_state(actor, faint ? 0x9D : 6, packet, 0);
    return Result::Ok;
}
} // namespace gohan::game::player_actions

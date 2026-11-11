#include "game/hidden_icons.hpp"
#include <cstring>

namespace gohan::game::items {
const std::uint8_t *SelectHiddenIcon(bool on, unsigned kind, std::uint16_t id, const HiddenEntry *entry,
                                     const HiddenIconArchive &archive) {
    if (!on || kind || (id & 0x7FFF) == 0x7FFE || !entry || !entry->icon)
        return nullptr;
    return archive.Find(entry->icon);
}
bool WriteHiddenIcon(generated::NwLytMaterial &material, std::uint32_t physical,
                     generated::nwlyt_TexMap_UpdateGpuRegs_t update) {
    if (!physical || !material.variable_data || !update)
        return false;
    auto &map = *material.variable_data;
    std::memset(map.unk_0, 0, sizeof(map.unk_0));
    map.texture_address = physical;
    map.image_width = map.image_height = map.texture_width = map.texture_height = 32;
    map.sampler_bits = (map.sampler_bits & ~0xF00u) | (11u << 8);
    update(&map);
    material.flags &= ~4u;
    return true;
}
} // namespace gohan::game::items

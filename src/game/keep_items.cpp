#include "game/keep_items.hpp"
#include "game/generated/api.hpp"

namespace gohan::game::keep_items {
std::span<const std::uintptr_t> GrowthReturns() {
    static constexpr std::uintptr_t returns[] = {
        generated::KeepBushCaller_addr + generated::KeepBushCaller_len,
        generated::KeepSaplingCaller0_addr + generated::KeepSaplingCaller0_len,
        generated::KeepSaplingCaller1_addr + generated::KeepSaplingCaller1_len,
        generated::KeepItem3Caller_addr + generated::KeepItem3Caller_len,
        generated::KeepItem4Caller_addr + generated::KeepItem4Caller_len,
    };
    return returns;
}
bool GrowthCaller(std::uintptr_t caller, std::span<const std::uintptr_t> returns) {
    for (auto address : returns) if ((caller & ~std::uintptr_t(1)) == (address & ~std::uintptr_t(1))) return true;
    return false;
}
std::uint32_t NextGrowth(std::uint32_t tile) { return (tile & 0xFFFF0000u) | static_cast<std::uint16_t>(tile + 1); }
} // namespace gohan::game::keep_items

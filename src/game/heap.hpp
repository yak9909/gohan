#pragma once
#include <cstdint>
namespace gohan::game::heap {
enum class Kind { Layout, Resource, Command };
struct Room {
    std::uint32_t free_bytes = 0, contiguous_bytes = 0;
    bool measured = false;
    bool Allows(std::uint32_t total, std::uint32_t contiguous) const {
        return measured && free_bytes >= total && contiguous_bytes >= contiguous;
    }
};
Room Measure(Kind kind, int alignment = 4);
} // namespace gohan::game::heap

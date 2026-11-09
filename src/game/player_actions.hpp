#pragma once
#include "game/patch_set.hpp"

namespace gohan::game::player_actions {
struct Access {
    bool (*network_ready)();
    void *(*actor)();
    const void *(*packet)(void *);
    bool (*readable)(std::uintptr_t, std::size_t);
    void (*pack)(void *, const float *);
    void (*set_state)(void *, unsigned, const void *, unsigned);
};
Result Run(const Access &, bool faint);
bool Wire();
Result Run(bool faint);
} // namespace gohan::game::player_actions

#pragma once
#include "game/generated/api.hpp"

namespace gohan::game::script {
struct WordRes {
    std::uintptr_t vtable;
    const char16_t *text;
    std::uint32_t length;
    explicit WordRes(const char16_t *value) : vtable(generated::script_WordRes_vtable), text(value), length(0x3FFFFFFF) {}
};
#ifdef __3DS__
static_assert(sizeof(WordRes) == 12);
#endif
} // namespace gohan::game::script

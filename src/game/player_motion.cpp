#include "game/player_motion.hpp"

namespace gohan::game::player_motion {
Direction ReadDirection(const InputSnapshot &input, unsigned key, unsigned hotkey) {
    if (key == 0) {
        const auto buttons = input.buttons & ~hotkey;
        return {float(bool(buttons & (1u << 7))) - float(bool(buttons & (1u << 6))),
                float(bool(buttons & (1u << 5))) - float(bool(buttons & (1u << 4)))};
    }
    float x = key == 1 ? input.slide_x : input.cstick_x / 146.f;
    float y = key == 1 ? input.slide_y : input.cstick_y / 146.f;
    const auto clamp = [](float v) { return v < -1 ? -1.f : v > 1 ? 1.f : v; };
    x = clamp(x); y = clamp(y);
    if (x * x + y * y < .15f * .15f) return {};
    return {x, -y};
}
int Round8(float value) { return value >= .5f ? 1 : value <= -.5f ? -1 : 0; }
unsigned DirectionBits(int x, int z) { return (x < 0 ? 1u : x > 0 ? 2u : 0u) | (z < 0 ? 4u : z > 0 ? 8u : 0u); }
float GridCenter(int cell) { return 32.f * cell + 16; }
bool TouchCoordinates(unsigned room, bool opened, int x, int y, Direction &world) {
    struct Rect { int x, y, w, h; float offset_x, offset_z, scale; };
    constexpr Rect town_closed{70, 32, 180, 175, 30, 5, 14.2f}, town_opened{7, 32, 180, 175, -33, 6, 14.2f};
    constexpr Rect island{76, 36, 168, 168, 72, 23, 12.1f}, street{4, 43, 312, 159, -16, 55, 6.2f};
    constexpr Rect tour{65, 34, 190, 170, 24, -7, 13.5f};
    const Rect *rect = room == 0 ? (opened ? &town_opened : &town_closed) : room == 0x68 ? &island
        : room == 1 ? &street : room >= 0x69 && room < 0x80 ? &tour : nullptr;
    if (!rect || x < rect->x || y < rect->y || x >= rect->x + rect->w || y >= rect->y + rect->h) return false;
    world = {(x - rect->offset_x) * rect->scale, (y - rect->offset_z) * rect->scale};
    return true;
}
} // namespace gohan::game::player_motion

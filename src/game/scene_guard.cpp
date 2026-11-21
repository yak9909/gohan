#include "game/scene_guard.hpp"
#include "game/generated/api.hpp"
namespace gohan::game {
namespace {
std::uint32_t Room() {
    return reinterpret_cast<generated::Room_GetCurrentId_t>(generated::Room_GetCurrentId_addr)();
}
}
void SceneGuard::Capture(const void *scene) { Capture(scene, Room()); }
bool SceneGuard::Matches(const void *scene) const { return Matches(scene, Room()); }
} // namespace gohan::game

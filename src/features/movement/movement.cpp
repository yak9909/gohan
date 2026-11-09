#include "features/movement/movement.hpp"
#include "core/input_block.hpp"
#include "core/frame.hpp"
#include "game/player.hpp"
#include "game/hook_batch.hpp"
#include "game/player_motion.hpp"
#include "menu_ids.hpp"
#include "ui/input/buttons.hpp"
#include "ui/timed_repeat.hpp"

namespace gohan::features::movement {
namespace {
namespace block = core::input_block;
namespace motion = game::player_motion;
block::Owner owner;
bool ready = false;
unsigned key = 0, mode = 0, dpad = 0;
menu::s32 amount = 50;
ui::TimedRepeat repeat;
block::Claim claim;
void PublishClaim() { if (ready) block::Global().Set(owner, claim); }
void Move(bool holding, menu::u16 hotkey) {
    // OFF への遷移・キーを離した時は、アクターが見つからなくても要求を取り下げる。
    claim = {holding && key == 0 && dpad == 1 ? block::group::DPad : 0, 0};
    PublishClaim();
    if (!ready || !holding) { repeat.Reset(); return; }
    auto *actor = game::player::LocalActor();
    if (!actor || game::player::Digging(actor)) { repeat.Reset(); return; }
    const auto direction = motion::ReadDirection(game::ReadInputSnapshot(), key, hotkey);
    if (!mode) {
        repeat.Reset();
        game::player::Translate(actor, direction.x * amount / 10.f, direction.z * amount / 10.f);
        return;
    }
    const auto x = motion::Round8(direction.x), z = motion::Round8(direction.z);
    if (!repeat.Step(motion::DirectionBits(x, z), ui::input::NowMs())) return;
    unsigned wx, wz;
    if (game::player::WorldGrid(wx, wz))
        game::player::SetXZ(actor, motion::GridCenter(static_cast<int>(wx) + x), motion::GridCenter(static_cast<int>(wz) + z));
    else repeat.Reset();
}
void Warp(bool holding) {
    if (!ready || !holding) return;
    const auto input = game::ReadInputSnapshot();
    if (!input.touching) return;
    auto *actor = game::player::LocalActor();
    if (!actor || game::player::Digging(actor)) return;
    motion::Direction world;
    if (motion::MapCoordinates(input.touch_x, input.touch_y, world)) game::player::SetXZ(actor, world.x, world.z);
}
void Key(menu::s32 value) { key = value; }
void Amount(menu::s32 value) { amount = value; }
void Mode(menu::s32 value) { mode = value; repeat.Reset(); }
void Dpad(menu::s32 value) { dpad = value; }
bool Wire() {
    ready = motion::Wire() && game::InputControllerReady();
    if (!ready) return false;
    if (block::Global().Acquire("movement", owner) != block::Result::Ok) { ready = false; return false; }
    const auto result = core::frame::Global().Add(PublishClaim, 55).result;
    if (result != core::frame::Result::Queued && result != core::frame::Result::Unchanged) {
        block::Global().Release(owner); ready = false; return false;
    }
    return true;
}
void Shutdown() { game::PausedGame pause; ready = false; repeat.Reset(); core::frame::Global().Remove(PublishClaim); block::Global().Release(owner); }
const core::Feature feature{"movement", Wire, Shutdown, core::ShutdownStage::GameHooks};
const menu::Bound bindings[] = {
    GOHAN_ITEM(menu::MenuId::PlayerMove, menu::Hold{.step_with = Move}),
    GOHAN_ITEM(menu::MenuId::PlayerWarp, menu::Hold{.step = Warp}),
    GOHAN_ITEM(menu::MenuId::PlayerMoveKey, menu::SettingValue{.applied = Key}),
    GOHAN_ITEM(menu::MenuId::PlayerMoveAmount, menu::SettingValue{.applied = Amount}),
    GOHAN_ITEM(menu::MenuId::PlayerMoveMode, menu::SettingValue{.applied = Mode}),
    GOHAN_ITEM(menu::MenuId::PlayerMoveDpad, menu::SettingValue{.applied = Dpad}),
};
}
const core::Feature &Feature() { return feature; }
std::span<const menu::Bound> Bindings() { return bindings; }
} // namespace gohan::features::movement

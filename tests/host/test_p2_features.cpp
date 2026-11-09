#include "features/movement/movement.hpp"
#include "features/actions/actions.hpp"
#include "features/keep_items/keep_items.hpp"
#include "features/shizue_skip/shizue_skip.hpp"
#include "core/frame.hpp"
#include "core/input_block.hpp"
#include "game/hook_batch.hpp"
#include "game/player.hpp"
#include "game/player_motion.hpp"
#include "game/player_actions.hpp"
#include "game/keep_items.hpp"
#include "game/shizue_skip.hpp"
#include "menu/engine.hpp"
#include "menu_ids.hpp"
#include <cassert>
#include <cstring>

namespace {
gohan::game::InputSnapshot input;
unsigned now = 1000, translations = 0, warps = 0, idles = 0, faints = 0;
float last_x = 0, last_z = 0;
bool digging = false, keep = false, skip = false, fire = false;
unsigned hold_calls[2]{};
bool hold_values[2]{};
void HoldMove(bool value) { ++hold_calls[0]; hold_values[0] = value; }
void HoldWarp(bool value) { ++hold_calls[1]; hold_values[1] = value; }
bool Pause() { return false; }
void Resume(bool) {}
}
namespace gohan::game {
const HookBackend &NativeHookBackend() { static const HookBackend backend{nullptr, Pause, Resume, nullptr, nullptr, nullptr}; return backend; }
bool InputControllerReady() { return true; }
InputSnapshot ReadInputSnapshot() { return input; }
namespace player {
void *LocalActor() { return reinterpret_cast<void *>(128); }
bool Digging(void *) { return digging; }
bool WorldGrid(unsigned &x, unsigned &z) { x = z = 10; return true; }
void Translate(void *, float x, float z) { ++translations; last_x = x; last_z = z; }
void SetXZ(void *, float x, float z) { ++warps; last_x = x; last_z = z; }
}
namespace player_motion {
bool Wire() { return true; }
bool MapCoordinates(int, int, Direction &world) { world = {123, 456}; return true; }
}
namespace player_actions {
bool Wire() { return true; }
Result Run(bool faint) { faint ? ++faints : ++idles; return Result::Ok; }
}
namespace keep_items {
bool Wire() { return true; }
void Shutdown() { keep = false; }
Result Set(bool on) { keep = on; return Result::Ok; }
bool IsActive() { return keep; }
}
namespace shizue_skip {
bool Wire() { return true; }
void Shutdown() { skip = false; }
Result Set(bool on) { skip = on; return Result::Ok; }
bool IsActive() { return skip; }
bool Step() { if (!skip || !fire) return false; fire = false; return true; }
}
}
namespace gohan::ui::input { std::uint32_t NowMs() { return now; } }

int main() {
    using namespace gohan;
    using menu::MenuId;
    const auto id = [](MenuId value) { return static_cast<menu::u16>(value); };
    menu::Engine engine(menu::kMenuItems, menu::kMenuItemCount);
    for (auto bindings : {features::movement::Bindings(), features::actions::Bindings(), features::keep_items::Bindings(), features::shizue_skip::Bindings()})
        assert(engine.BindAll(bindings.data(), bindings.size()));
    const core::Feature *features[] = {&features::movement::Feature(), &features::actions::Feature(), &features::keep_items::Feature(), &features::shizue_skip::Feature()};
    for (const auto *feature : features) assert(feature->Wire());
    auto apply = [&](MenuId item, menu::s32 value) { assert(engine.SetValue(id(item), value) && engine.Commit(id(item), now)); };
    auto frame = [&](unsigned time, menu::u16 buttons) { now = time; engine.Frame(now, buttons); };
    core::input_block::Effective effective;
    std::uint32_t generation = 0;
    auto blocked = [&] { core::input_block::Global().Read(effective, generation); return effective.block & core::input_block::group::DPad; };
    engine.Open(now);
    apply(MenuId::PlayerMoveDpad, 1);
    apply(MenuId::PlayerMove, 1);
    input.buttons = menu::key::Right;
    assert(engine.RequestClose(now));
    frame(1016, menu::key::A);
    assert(translations == 1 && last_x == 5 && last_z == 0 && blocked());
    frame(1032, 0); assert(translations == 1 && !blocked());
    digging = true; frame(1048, menu::key::A); assert(translations == 1);
    digging = false;
    engine.Open(now);
    assert(!engine.Disabled(id(MenuId::PlayerMoveAmount)));
    assert(engine.SetValue(id(MenuId::PlayerMoveMode), 1) && engine.Disabled(id(MenuId::PlayerMoveAmount)));
    assert(engine.Commit(id(MenuId::PlayerMoveMode), now));
    assert(engine.RequestClose(now));
    frame(1100, menu::key::A); assert(warps == 1 && last_x == 368 && last_z == 336);
    frame(1299, menu::key::A); assert(warps == 1);
    frame(1300, menu::key::A); assert(warps == 2);
    frame(1360, menu::key::A); assert(warps == 3);
    frame(2000, menu::key::A); assert(warps == 4);
    frame(2001, menu::key::A); assert(warps == 4);
    engine.Open(now); engine.Frame(++now, 0); assert(!blocked());
    apply(MenuId::PlayerMove, 0);
    const auto before = warps;
    input.touching = true;
    apply(MenuId::PlayerWarp, 1);
    assert(engine.RequestClose(now));
    frame(2100, 0); frame(2116, 0); assert(warps == before + 2 && last_x == 123 && last_z == 456);
    engine.Open(now); apply(MenuId::PlayerWarp, 0); assert(engine.RequestClose(now));
    const auto off_warps = warps; frame(2200, 0); assert(warps == off_warps);
    engine.Open(now); assert(engine.SetHotkey(id(MenuId::PlayerWarp), menu::key::R)); apply(MenuId::PlayerWarp, 1);
    assert(engine.RequestClose(now));
    frame(2300, 0); assert(warps == off_warps);
    frame(2316, menu::key::R); assert(warps == off_warps + 1);
    // trigger は押し始めだけ。確認ダイアログは出さない。
    frame(2400, menu::key::L | menu::key::Down);
    frame(2416, menu::key::L | menu::key::Down);
    assert(idles == 1 && engine.Dialog() == menu::DialogKind::None);
    frame(2432, 0); frame(2448, menu::key::L | menu::key::Down); assert(idles == 2);
    engine.Open(now);
    // プレイヤーのフォルダへ入り、その場の実行を選ぶ。
    assert(engine.Select(id(MenuId::Player))); engine.Press(menu::key::A, now);
    assert(engine.Select(id(MenuId::PlayerFaint))); engine.Press(menu::key::A, now);
    assert(faints == 1 && engine.Dialog() == menu::DialogKind::None);
    assert(engine.Select(id(MenuId::PlayerIdle))); engine.Press(menu::key::A, now); assert(idles == 3);
    apply(MenuId::TownKeep, 1); apply(MenuId::GameShizue, 1); assert(keep && skip);
    fire = true; core::frame::Dispatch(); assert(!fire);
    menu::Notice notice; assert(menu::TakeNotice(notice) && std::strcmp(notice.message, "村へ移動しました") == 0);
    core::frame::Dispatch(); assert(!menu::TakeNotice(notice));
    for (const auto *feature : features) feature->Shutdown();
    assert(!keep && !skip && !blocked());

    // holding の旗と OFF 時の契約を、効果の有無ではなく callback の呼出回数で確かめる。
    menu::Engine holds(menu::kMenuItems, menu::kMenuItemCount);
    constexpr menu::Bound hold_bindings[] = {
        GOHAN_ITEM(MenuId::PlayerMove, menu::Hold{.step = HoldMove}),
        GOHAN_ITEM(MenuId::PlayerWarp, menu::Hold{.step = HoldWarp}),
    };
    assert(holds.BindAll(hold_bindings, 2));
    holds.Open(now);
    assert(holds.SetHotkey(id(MenuId::PlayerMove), 0));
    for (auto item : {MenuId::PlayerMove, MenuId::PlayerWarp})
        assert(holds.SetValue(id(item), 1) && holds.Commit(id(item), now));
    assert(holds.RequestClose(now));
    holds.Frame(++now, 0);
    assert(hold_calls[0] == 1 && !hold_values[0] && hold_calls[1] == 1 && hold_values[1]);
    holds.Open(now); holds.Frame(++now, 0); assert(!hold_values[1]);
    assert(holds.SetValue(id(MenuId::PlayerWarp), 0) && holds.Commit(id(MenuId::PlayerWarp), now));
    const auto off_calls = hold_calls[1];
    assert(holds.RequestClose(now)); holds.Frame(++now, 0); holds.Frame(++now, 0);
    assert(hold_calls[1] == off_calls);
}

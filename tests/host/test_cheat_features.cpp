#include "features/player/player.hpp"
#include "features/field/field.hpp"
#include "features/town/town.hpp"
#include "features/gameplay/gameplay.hpp"
#include "features/resources/resources.hpp"
#include "game/cheats.hpp"
#include "game/encrypted_value.hpp"
#include "menu/engine.hpp"
#include "menu/sample_items.hpp"
#include "menu_ids.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using Patch = gohan::game::cheats::Patch;
std::array<bool, 13> wired{}, applied{}, stopped{};
std::array<gohan::menu::s32, 5> resources{};
bool supported = true, save_ready = false, save_stopped = false;
gohan::menu::s32 weather = 0;
unsigned notices = 0;
void Notice(void *, const gohan::menu::Notice &notice) {
    if (notice.red) ++notices;
}
} // namespace

namespace gohan::game::cheats {
Result Wire(Patch id) { wired[static_cast<unsigned>(id)] = true; return Result::Ok; }
Result Set(Patch id, bool on) {
    if (!supported) return Result::Unsupported;
    applied[static_cast<unsigned>(id)] = on;
    return Result::Ok;
}
bool IsApplied(Patch id) { return applied[static_cast<unsigned>(id)]; }
void Shutdown(Patch id) { applied[static_cast<unsigned>(id)] = false; stopped[static_cast<unsigned>(id)] = true; }
bool ReadWeather(std::int32_t *value) { *value = weather; return true; }
Result WriteWeather(std::int32_t value) { weather = value; return Result::Ok; }
} // namespace gohan::game::cheats
namespace gohan::game {
bool WireResources() { save_ready = true; return true; }
void ShutdownResources() { save_stopped = true; }
bool ReadResource(Resource id, std::int32_t *value) { *value = resources[static_cast<unsigned>(id)]; return save_ready; }
Result WriteResource(Resource id, std::int32_t value) {
    if (!save_ready) return Result::NotReady;
    resources[static_cast<unsigned>(id)] = value;
    return Result::Ok;
}
} // namespace gohan::game

int main() {
    namespace features = gohan::features;
    namespace menu = gohan::menu;
    using menu::MenuId;
    menu::Engine engine(menu::kMenuItems, menu::kMenuItemCount);
    engine.SetNoticeSink({Notice, nullptr});
    std::size_t count = 0;
    for (auto bindings : {features::player::Bindings(), features::field::Bindings(), features::town::Bindings(),
                          features::gameplay::Bindings(), features::resources::Bindings()}) {
        assert(engine.BindAll(bindings.data(), bindings.size()));
        count += bindings.size();
    }
    assert(count == 18 && engine.BindAll(menu::sample::kBindings, menu::sample::kBindingCount));
    const gohan::core::Feature *all[] = {&features::player::Feature(), &features::field::Feature(),
        &features::town::Feature(), &features::gameplay::Feature(), &features::resources::Feature()};
    for (auto *feature : all) assert(feature->Wire());
    for (bool done : wired) assert(done);
    engine.Open(1000);
    // 全ての値の読み書きと、既定の固定可否を検査する。
    for (auto id : {MenuId::PlayerWallet, MenuId::PlayerBank, MenuId::PlayerCoupons, MenuId::PlayerMedals,
                    MenuId::GameTurnips, MenuId::TownWeather}) {
        auto item = static_cast<menu::u16>(id);
        assert(menu::kMenuItems[item].lockable);
        menu::ItemAction actions[menu::kMaxActions];
        const auto action_count = engine.Actions(item, actions);
        bool can_fix = false;
        for (std::size_t i = 0; i < action_count; ++i)
            can_fix = can_fix || (actions[i].kind == menu::ActionKind::Fix && !actions[i].disabled);
        assert(can_fix);
        assert(engine.SetValue(item, 3) && engine.Commit(item, 1000));
    }
    for (auto value : resources) assert(value == 3);
    assert(weather == 3);
    // ホットキー無しの状態は適用時に入る。外す操作も同じ結び付けを通る。
    const menu::MenuId patches[] = {MenuId::PlayerFlower, MenuId::PlayerTrap, MenuId::PlayerBedHead,
        MenuId::ItemsDig, MenuId::ItemsTree, MenuId::ItemsDig3, MenuId::TownLookUp, MenuId::GameShopsOpen, MenuId::GameText};
    for (auto id : patches) {
        const auto item = static_cast<menu::u16>(id);
        assert(engine.SetValue(item, 1) && engine.Commit(item, 1000));
    }
    // 共有する既定キーは、明示的に両方を有効にした時に両方へ届く。
    for (auto id : {MenuId::SampleFlag, MenuId::SampleHold, MenuId::PlayerWall, MenuId::ItemsTrample, MenuId::GameFps}) {
        const auto item = static_cast<menu::u16>(id);
        assert(engine.SetValue(item, 1) && engine.Commit(item, 1000));
    }
    assert(engine.RequestClose(1000));
    engine.Frame(1016, menu::key::L | menu::key::Up);
    assert(menu::sample::Get().flag && applied[static_cast<unsigned>(Patch::Wall)]);
    engine.Frame(1032, 0);
    engine.Frame(1048, menu::key::R | menu::key::A);
    assert(menu::sample::Get().holding && applied[static_cast<unsigned>(Patch::Trample)]);
    engine.Frame(1064, 0);
    engine.Frame(1080, menu::key::B | menu::key::Up);
    assert(applied[static_cast<unsigned>(Patch::Fps)]);
    engine.Open(1100);
    for (auto id : patches) {
        const auto item = static_cast<menu::u16>(id);
        assert(engine.SetValue(item, 0) && engine.Commit(item, 1100));
    }
    // 対応外の結果は赤い通知に結び付く。
    supported = false;
    const auto flower = static_cast<menu::u16>(MenuId::PlayerFlower);
    assert(engine.SetValue(flower, 1));
    const auto before = notices;
    assert(engine.Commit(flower, 1100) && notices == before + 1 && !engine.EffectOn(flower));
    for (auto *feature : all) feature->Shutdown();
    for (bool done : stopped) assert(done);
    for (bool on : applied) assert(!on);
    assert(save_stopped);
    std::puts("全 18 項目の結び付け・共有キー・通知・終了: 成功");
}

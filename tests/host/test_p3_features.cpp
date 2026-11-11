#include "features/pocket_items/pocket_items.hpp"
#include "features/hidden_items/hidden_items.hpp"
#include "features/drop_item/drop_item.hpp"
#include "game/pocket_items.hpp"
#include "game/item_names.hpp"
#include "game/hidden_icons.hpp"
#include "game/hook_batch.hpp"
#include "game/romfs.hpp"
#include "core/frame.hpp"
#include "menu/engine.hpp"
#include "menu_ids.hpp"
#include <cassert>
#include <cstring>
#include <thread>

namespace {
gohan::game::Result result = gohan::game::Result::Ok;
unsigned runs = 0, lastId = 0;
bool iconsSettled = false, iconsAvailable = false;
gohan::game::items::HiddenMode mode = gohan::game::items::HiddenMode::Hide;
std::thread::id gameThread;
bool Pause() { return false; }
void Resume(bool) {}
}
namespace gohan::game {
const HookBackend &NativeHookBackend() {
    static const HookBackend backend{nullptr, Pause, Resume, nullptr, nullptr, nullptr}; return backend;
}
namespace items {
bool WirePocket() { return true; }
void ShutdownPocket() {}
Result AddPocket(std::uint16_t id) {
    assert(std::this_thread::get_id() == gameThread);
    ++runs; lastId = id; return result;
}
bool WireHidden() { return true; }
void PollHidden() {}
bool HiddenIconsSettled() { return iconsSettled; }
bool HiddenIconsAvailable() { return iconsAvailable; }
void ShutdownHidden() { mode = HiddenMode::Hide; }
Result SetHidden(HiddenMode next) { assert(std::this_thread::get_id() == gameThread); mode = next; return Result::Ok; }
}
namespace romfs {
core::files::Buffer ReadAll(const char *path, std::size_t) {
    assert(std::strcmp(path, "Script/Str/STR_Item_name.umsbt") == 0);
    core::files::Buffer buffer{std::unique_ptr<std::uint8_t[]>(new std::uint8_t[100]{}), 100};
    auto *b = buffer.data.get();
    auto put = [b](unsigned at, unsigned value, unsigned n = 4) {
        for (unsigned i = 0; i < n; ++i) b[at + i] = value >> (i * 8);
    };
    put(0, 16); put(4, 84);
    std::memcpy(b + 16, "MsgStdBn", 8); put(24, 0xFEFF, 2); b[28] = 1; put(30, 1, 2);
    std::memcpy(b + 48, "TXT2", 4); put(52, 12); put(64, 1); put(68, 8); put(72, u'花', 2);
    return buffer;
}
}
}
int main() {
    using namespace gohan;
    gameThread = std::this_thread::get_id();
    menu::Engine engine(menu::kMenuItems, menu::kMenuItemCount);
    const auto id = [](menu::MenuId value) { return static_cast<menu::u16>(value); };
    for (auto bindings : {features::pocket_items::Bindings(), features::hidden_items::Bindings(), features::drop_item::Bindings()})
        assert(engine.BindAll(bindings.data(), bindings.size()));
    features::hidden_items::Bindings()[0].setting.applied(1);
    menu::Notice unsupported;
    assert(menu::TakeNotice(unsupported) && unsupported.red && mode == game::items::HiddenMode::Hide);
    for (auto *feature : {&features::pocket_items::Feature(), &features::hidden_items::Feature(), &features::drop_item::Feature()})
        assert(feature->Wire());
    const auto pocket = id(menu::MenuId::ItemsPocket), drop = id(menu::MenuId::ItemsDropItem), hidden = id(menu::MenuId::ItemsHidden);
    assert(engine.Def(pocket).behavior == menu::Behavior::Trigger && engine.Def(pocket).input == menu::InputKind::Hex);
    assert(engine.Def(pocket).def == 0 && engine.Def(pocket).max == 0x7FFF);
    assert(engine.Def(drop).def == 0xFFFF && engine.Def(drop).max == 0xFFFF);
    menu::s32 setting = 0;
    menu::SetSettingsEngine(&engine);
    assert(menu::ReadSetting(drop, setting) && setting == 0xFFFF);
    assert(engine.SetValue(drop, 0x2000) && menu::ReadSetting(drop, setting) && setting == 0xFFFF);
    assert(engine.Commit(drop, 1) && menu::ReadSetting(drop, setting) && setting == 0x2000);
    assert(!menu::ReadSetting(pocket, setting));
    for (int next : {1, 2, 0}) {
        if (next == 1) {
            std::thread restore([&] { assert(engine.SetValue(hidden, next) && engine.Commit(hidden, 1)); });
            restore.join();
        } else assert(engine.SetValue(hidden, next) && engine.Commit(hidden, 1));
        assert(mode != static_cast<game::items::HiddenMode>(next));
        core::frame::Dispatch();
        assert(mode == static_cast<game::items::HiddenMode>(next));
        if (next == 2) {
            menu::Notice missing;
            assert(!menu::TakeNotice(missing));
            iconsSettled = true;
            core::frame::Dispatch();
            assert(menu::TakeNotice(missing) && missing.red && std::strstr(missing.message, "アイコンが無い（名前だけ）"));
            core::frame::Dispatch();
            assert(!menu::TakeNotice(missing));
        }
    }
    engine.Open(1);
    assert(engine.Select(id(menu::MenuId::Items))); engine.Press(menu::key::A, 2);
    assert(engine.Select(pocket)); engine.Press(menu::key::A, 3);
    assert(engine.Ui().kind == menu::UiKind::TriggerInput);
    engine.SubmitUi(0x2000, 4);
    assert(runs == 1 && lastId == 0x2000); // 完了待ちを挟まず、その場でゲームの関数を呼ぶ。
    engine.Press(menu::key::A, 5); engine.SubmitUi(0x2000, 6);
    assert(runs == 2);
    // 読み込み中は名前を変えない。完了はゲームのスレッドで受け取る。
    assert(!game::items::SharedNames().CustomName(0x2000));
    std::thread worker([] { while (core::jobs::Global().WorkOne()) {} }); worker.join();
    core::jobs::Global().Deliver(); game::items::SharedNames().Poll();
    assert(game::items::SharedNames().Settled());
    core::frame::Dispatch();
    menu::Notice notice;
    assert(menu::TakeNotice(notice) && std::strstr(notice.message, "0x2000 花") && !notice.red);
    core::frame::Dispatch();
    assert(menu::TakeNotice(notice) && std::strstr(notice.message, "0x2000 花"));
    bool failed = false;
    engine.SetNoticeSink({[](void *p, const menu::Notice &notice) { *static_cast<bool *>(p) = notice.red; }, &failed});
    result = game::Result::NotReady;
    engine.Press(menu::key::A, 7); engine.SubmitUi(5, 8);
    assert(runs == 3 && failed);
    features::hidden_items::Feature().Shutdown();
    assert(mode == game::items::HiddenMode::Hide);
    features::pocket_items::Feature().Shutdown(); features::drop_item::Feature().Shutdown();
    game::items::SharedNames().Shutdown(); menu::SetSettingsEngine(nullptr);
    assert(!menu::ReadSetting(drop, setting));
    core::frame::Global().Reset();
}

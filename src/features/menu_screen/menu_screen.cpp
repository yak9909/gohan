#include "features/menu_screen/menu_screen.hpp"
#include "core/frame.hpp"
#include "core/mailbox.hpp"
#include "core/request.hpp"
#include "features/menu_screen/canvas_surface.hpp"
#include "menu/engine.hpp"
#include "menu/sample_items.hpp"
#include "menu/shell.hpp"
#include "menu_ids.hpp"
#include "ui/input/buttons.hpp"
#include "ui/renderer/renderer.hpp"

namespace gohan::features::menu_screen {
namespace {
namespace renderer = ui::renderer;
namespace input = ui::input;

constexpr renderer::Screen kScreen = renderer::Screen::Top;
// 入力の順序（menu の key）と ui の button が同じであること。
static_assert(menu::key::ZL == input::button::ZL && menu::key::L == input::button::L &&
              menu::key::R == input::button::R && menu::key::ZR == input::button::ZR &&
              menu::key::Up == input::button::Up && menu::key::Down == input::button::Down &&
              menu::key::Left == input::button::Left && menu::key::Right == input::button::Right &&
              menu::key::A == input::button::A && menu::key::B == input::button::B &&
              menu::key::X == input::button::X && menu::key::Y == input::button::Y &&
              menu::key::Select == input::button::Select && menu::key::Start == input::button::Start);

// 中身はゲームの描画スレッド（毎フレームの処理）だけが触る。
menu::Engine engine(menu::kMenuItems, menu::kMenuItemCount);
menu::Shell shell(engine, {400});
renderer::Canvas canvas;
bool enable_requested = false;
bool drawn = false;

core::Request<bool> toggle_request;
core::Mailbox<bool> active_box;

void FrameStep() {
    const input::Snapshot pad = input::Read();
    menu::Input in;
    in.held = pad.buttons;
    bool toggle = false;
    toggle_request.TryTake(toggle);
    const std::uint32_t now = input::NowMs();
    shell.Step(now, in, toggle);

    // 字形・UIだけを常駐させる。背景の借用は空でない描画から始まり、
    // 閉じ切った空の描画を確定した後、描画側がGPUの参照を排出して返す。
    // 競合した時は次のフレームで頼み直す。
    if (!enable_requested) enable_requested = renderer::SetEnabled(renderer::Client::Menu, true);
    const bool need = shell.NeedsDraw(now);
    if (!need && !drawn) return;
    // 自分の画面だけを描いて確定する（もう一方の画面を使う別の呼び出し元に触れない）。
    canvas.Begin(kScreen);
    if (need) {
        CanvasSurface surface(canvas, kScreen);
        shell.Draw(surface, now);
    }
    // 確定に失敗（競合）したら、次のフレームでもう一度描いて確定する。
    if (renderer::Commit(canvas, kScreen)) drawn = need;
}

bool Wire() {
    engine.SetReservedHotkeys(menu::key::Select);
#ifdef GOHAN_DEV
    engine.SetDevVisible(true);
#endif
    if (!engine.BindAll(menu::sample::kBindings, menu::sample::kBindingCount)) return false;
    const auto change = core::frame::Global().Add(FrameStep, 50);
    if (change.result != core::frame::Result::Queued && change.result != core::frame::Result::Unchanged)
        return false;
    active_box.TryPublish(true);
    return true;
}

void Shutdown() {
    core::frame::Global().Remove(FrameStep);
    active_box.TryPublish(false);
    // 描画の部品が先に止まっていても、要求だけは取り下げておく（失敗しても部品側が止める）。
    renderer::SetEnabled(renderer::Client::Menu, false);
}

const core::Feature feature{"menu.screen", Wire, Shutdown, core::ShutdownStage::Other};
} // namespace

const core::Feature &Feature() { return feature; }

bool RequestToggle() { return toggle_request.TrySubmit(true); }

bool ShouldOpenCtrpfMenu() {
    // 読み手は CTRPF のメニューのスレッドだけ。
    static bool active = false;
    static std::uint32_t generation = 0;
    active_box.ReadIfNew(active, generation);
    if (!active || (input::Read().buttons & input::button::Up) != 0) return true;
    return renderer::ReadStatus() != renderer::Status::Ready;
}
} // namespace gohan::features::menu_screen

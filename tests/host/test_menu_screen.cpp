// ゲーム中のメニュー（毎フレームの処理 → 入力 → メニューの画面 → ui::renderer の描画指示）を、
// 入力と時計とゲーム側の描画の代役で通して試験する。実機の代わりにはならない。
#include "core/frame.hpp"
#include "features/menu_screen/menu_screen.hpp"
#include "game/lyt/renderer.hpp"
#include "ui/renderer/renderer.hpp"
#include <CTRPluginFramework.hpp>
#include <cassert>
#include <cstdio>
#include <string>

namespace gohan::game::lyt {
namespace {
Drawing presented;
int presents = 0;
} // namespace
InstallResult InstallRenderer(const FontResource &, BackgroundAllocation *) { return InstallResult::Ready; }
bool Present(const Drawing &drawing) {
    presented = drawing;
    ++presents;
    return true;
}
bool DetachRenderer() { return true; }
void WaitMilliseconds(unsigned) {}
Drawing &Last() { return presented; }
int Presents() { return presents; }
} // namespace gohan::game::lyt

namespace {
using CTRPluginFramework::Fake;
namespace menu_screen = gohan::features::menu_screen;
namespace renderer = gohan::ui::renderer;
namespace lyt = gohan::game::lyt;

void Frames(int count, int step_ms = 16) {
    for (int i = 0; i < count; ++i) {
        Fake::nowMs += step_ms;
        gohan::core::frame::Dispatch();
    }
}
void Press(std::uint32_t keys) {
    Fake::keys = keys;
    Frames(2);
    Fake::keys = 0;
    Frames(2);
}

std::string Utf8(const char16_t *text, unsigned length) {
    std::string out;
    for (unsigned i = 0; i < length; ++i) {
        const char16_t c = text[i];
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) out += {static_cast<char>(0xC0 | (c >> 6)), static_cast<char>(0x80 | (c & 0x3F))};
        else out += {static_cast<char>(0xE0 | (c >> 12)), static_cast<char>(0x80 | ((c >> 6) & 0x3F)), static_cast<char>(0x80 | (c & 0x3F))};
    }
    return out;
}

bool TopHasText(const std::string &needle) {
    const auto &screen = lyt::Last().screens[0];
    for (std::size_t i = 0; i < screen.count; ++i)
        if (screen.commands[i].kind == lyt::Kind::Text &&
            Utf8(screen.commands[i].text, screen.commands[i].length).find(needle) != std::string::npos)
            return true;
    return false;
}
} // namespace

int main() {
    const auto &feature = menu_screen::Feature();
    assert(feature.id == "menu.screen" && feature.stage == gohan::core::ShutdownStage::Other);
    // 導入前は CTRPF のメニューを譲らない。
    assert(menu_screen::ShouldOpenCtrpfMenu());
    assert(renderer::RendererFeature().Wire() && feature.Wire());
    Frames(1);
    assert(renderer::ReadStatus() == renderer::Status::Ready);
    // 自前の描画が使える間は、Select 単独を自前のメニューに譲る。Select+十字上は CTRPF のメニュー。
    assert(!menu_screen::ShouldOpenCtrpfMenu());
    Fake::keys = CTRPluginFramework::Key::DPadUp;
    assert(menu_screen::ShouldOpenCtrpfMenu());
    Fake::keys = 0;
    assert(lyt::Last().screens[0].count == 0 && lyt::Last().screens[1].count == 0);

    // 上画面にだけ描き、下画面へのタッチは何も変えない。
    Press(CTRPluginFramework::Key::Select); Frames(30);
    const auto &top = lyt::Last().screens[0];
    assert(top.count > 0 && lyt::Last().screens[1].count == 0);
    assert(top.commands[0].kind == lyt::Kind::Image && top.commands[0].bounds.x == 4);
    assert(TopHasText("GOHAN") && TopHasText("見本"));
    Press(CTRPluginFramework::Key::A); Frames(15);
    assert(TopHasText("見本のフラグ") && TopHasText("OFF"));
    Fake::touching = true; Fake::touchX = 40; Fake::touchY = 70; Frames(3);
    Fake::touching = false; Frames(15);
    bool selectedFirst = false;
    for (std::size_t n = 0; n < top.count; ++n) {
        const auto &c = top.commands[n];
        selectedFirst |= c.kind == lyt::Kind::Rectangle && c.bounds.x == 26 && c.bounds.y == 29;
    }
    assert(selectedFirst && lyt::Last().screens[1].count == 0);
    // 下画面の別の描画元を使っても、メニューの上画面を上書きしない。
    const auto count = top.count;
    renderer::Canvas dev; dev.Begin(renderer::Screen::Bottom);
    dev.FrameRect({20, 20, 120, 40}, 0xFFFFFFFFu);
    assert(renderer::Commit(dev, renderer::Screen::Bottom) && renderer::SetEnabled(true)); Frames(3);
    assert(lyt::Last().screens[1].count == 4 && lyt::Last().screens[0].count == count);
    dev.Begin(renderer::Screen::Bottom);
    assert(renderer::Commit(dev, renderer::Screen::Bottom) && renderer::SetEnabled(false)); Frames(3);
    assert(lyt::Last().screens[1].count == 0 && renderer::ReadStatus() == renderer::Status::Ready);

    // 外（開発版の CTRPF の項目）からの要求で閉じる。閉じ切ったら空の描画を確定する。
    assert(menu_screen::RequestToggle());
    Frames(2);
    Frames(40);
    assert(lyt::Last().screens[0].count == 0 && lyt::Last().screens[1].count == 0);
    const int presents_after_close = lyt::Presents();
    Frames(5);
    assert(lyt::Presents() == presents_after_close + 5); // 描画の部品は毎フレーム、最新の確定を渡す

    // 終了。
    feature.Shutdown();
    renderer::RendererFeature().Shutdown();
    Frames(1);
    assert(menu_screen::ShouldOpenCtrpfMenu());
    std::puts("menu screen: ok");
    return 0;
}

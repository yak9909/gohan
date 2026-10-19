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
InstallResult InstallRenderer(const FontResource &) { return InstallResult::Ready; }
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

bool BottomHasText(const std::string &needle) {
    const auto &screen = lyt::Last().screens[1];
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

    // Select で開く。下画面に帯が出て、上画面には何も出さない。
    Press(CTRPluginFramework::Key::Select);
    Frames(30);
    const auto &bottom = lyt::Last().screens[1];
    assert(bottom.count > 0 && lyt::Last().screens[0].count == 0);
    assert(bottom.commands[0].kind == lyt::Kind::Rectangle && bottom.commands[0].bounds.x == 0 &&
           bottom.commands[0].bounds.width == 160 && bottom.commands[0].bounds.height == 240);
    assert(bottom.texts <= gohan::core::limits::RendererTexts && bottom.rectangles <= gohan::core::limits::RendererRects);
    assert(BottomHasText("-* GOHAN *-") && BottomHasText(">見本"));

    // A でフォルダに入る（ボタン）。行に触れて選ぶ（タッチ）。
    Press(CTRPluginFramework::Key::A);
    Frames(15);
    assert(BottomHasText("-* GOHAN *- 見本"));
    Fake::touching = true;
    Fake::touchX = 40;
    Fake::touchY = 25 + 2 * 18 + 6;
    Frames(2);
    Fake::touching = false;
    Frames(15);
    const auto &folder = lyt::Last().screens[1];
    bool moved = false; // 選択の帯が 3 行目へ動いた
    for (std::size_t i = 0; i < folder.count; ++i)
        moved = moved || (folder.commands[i].kind == lyt::Kind::Rectangle && folder.commands[i].bounds.width == 150 &&
                          folder.commands[i].bounds.y == 25 + 2 * 18);
    assert(moved);
    assert(folder.texts <= gohan::core::limits::RendererTexts);
    // 行と帯の描画指示には、行の領域の切り抜きが付く。
    unsigned clipped = 0;
    for (std::size_t i = 0; i < folder.count; ++i)
        clipped += folder.commands[i].clip.y == 25 && folder.commands[i].clip.height == 10 * 18 && folder.commands[i].clip.width == 158;
    assert(clipped >= 10);

    // 開発版の「自前描画の入・切」は上画面だけを使う。入れても下画面のメニューは消えず、
    // 切っても描画の導入は（メニューが使うので）外れない。
    const auto bottom_count = lyt::Last().screens[1].count;
    renderer::Canvas dev;
    dev.Begin(renderer::Screen::Top);
    dev.FrameRect({20, 20, 120, 40}, 0xFFFFFFFFu);
    assert(renderer::Commit(dev, renderer::Screen::Top) && renderer::SetEnabled(true));
    Frames(3);
    assert(lyt::Last().screens[0].count == 4 && lyt::Last().screens[1].count == bottom_count);
    dev.Begin(renderer::Screen::Top);
    assert(renderer::Commit(dev, renderer::Screen::Top) && renderer::SetEnabled(false));
    Frames(3);
    assert(lyt::Last().screens[0].count == 0 && lyt::Last().screens[1].count == bottom_count);
    assert(renderer::ReadStatus() == renderer::Status::Ready);

    // 外（開発版の CTRPF の項目）からの要求で閉じる。閉じ切ったら空の描画を確定する。
    assert(menu_screen::RequestToggle());
    Frames(2);
    Frames(40);
    assert(lyt::Last().screens[1].count == 0);
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

// ゲーム中のメニュー（毎フレームの処理 → 入力 → メニューの画面 → ui::renderer の描画指示）を、
// 入力と時計とゲーム側の描画の代役で通して試験する。実機の代わりにはならない。
#include "core/frame.hpp"
#include "core/input_block.hpp"
#include "features/menu_screen/menu_screen.hpp"
#include "game/lyt/renderer.hpp"
#include "ui/renderer/renderer.hpp"
#include <CTRPluginFramework.hpp>
#include <cassert>
#include <cstdio>
#include <string>

namespace gohan::core::input_block {
struct TableTestAccess { static SpinMutex &Mutex(Table &table) { return table.mutex_; } };
}

namespace gohan::game::lyt {
RendererMemory ReadRendererMemory() { return {}; }
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
namespace block = gohan::core::input_block;
block::Effective Demand() {
    block::Effective value; std::uint32_t generation = 0;
    assert(block::Global().Read(value, generation) == gohan::core::MailRead::Updated);
    return value;
}
void Blocked(bool open) {
    const auto value = Demand();
    assert(value.block == (block::part::Select | (open ? block::group::Buttons : 0)));
    assert(value.defer == block::part::Start);
}

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
    // 借用で競合した場合も、次のフレームで menu と settings の枠を取る。
    auto &mutex = block::TableTestAccess::Mutex(block::Global());
    mutex.Lock(); Frames(1); mutex.Unlock();
    assert(Demand().defer == 0);
    Frames(1);
    Blocked(false);
    assert(renderer::ReadStatus() == renderer::Status::Ready);
    // 自前の描画が使える間は、Select 単独を自前のメニューに譲る。Select+十字上は CTRPF のメニュー。
    assert(!menu_screen::ShouldOpenCtrpfMenu());
    Fake::keys = CTRPluginFramework::Key::DPadUp;
    assert(menu_screen::ShouldOpenCtrpfMenu());
    Fake::keys = 0;
    assert(lyt::Last().screens[0].count == 0 && lyt::Last().screens[1].count == 0);

    // 上画面にだけ描き、下画面へのタッチは何も変えない。
    // 開くフレームで Set が Busy なら古い要求が残り、次のフレームで置き直す。
    mutex.Lock(); Fake::keys = CTRPluginFramework::Key::Select; Frames(1); mutex.Unlock();
    Blocked(false);
    Fake::keys = 0; Frames(1); Blocked(true); Frames(30);
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

    // ダイアログ中も同じ枠が残る。取り消し後もメニューが開いているので残る。
    Press(CTRPluginFramework::Key::A);
    assert(menu_screen::RequestToggle()); Frames(15);
    assert(TopHasText("変更を適用")); Blocked(true);
    Press(CTRPluginFramework::Key::B); Frames(15); Blocked(true);

    // 外（開発版の CTRPF の項目）からの要求で閉じる。閉じ切ったら空の描画を確定する。
    assert(menu_screen::RequestToggle());
    Frames(15); Blocked(true);
    Press(CTRPluginFramework::Key::A); // はいで適用して閉じる。
    Blocked(false); // 退場の描画中も要求は外れる。
    Frames(40);
    assert(lyt::Last().screens[0].count == 0 && lyt::Last().screens[1].count == 0);
    const int presents_after_close = lyt::Presents();
    Frames(5);
    assert(lyt::Presents() == presents_after_close + 5); // 描画の部品は毎フレーム、最新の確定を渡す

    // CTRPF のメニューを SELECT で閉じる。閉じた SELECT は、ゲームが再開した時にもまだ押されている。
    // 自前のメニューは、開く時に SELECT を見ていなくても（十字上だけが見えていた）、その押下で開閉しない。
    using CTRPluginFramework::Key;
    Fake::keys = Key::DPadUp; Frames(2);
    Fake::ctrpfResumed = true;
    Fake::keys = Key::Select; Frames(3);
    assert(!Fake::ctrpfResumed); // 再開の知らせは一度だけ受け取る。
    Frames(30);
    assert(lyt::Last().screens[0].count == 0 && !TopHasText("GOHAN"));
    // 押したままの間は何度フレームが進んでも開かない。離せば、次の SELECT の押下で普通に開く。
    Fake::keys = 0; Frames(2);
    Press(Key::Select); Frames(30);
    assert(TopHasText("GOHAN") && TopHasText("見本"));
    // メニューが開いている間に CTRPF を開いて閉じる。メニューは前に閉じた時のフォルダの中から開く。
    // 再開の時に押されたままの B は、離すまでメニューに届かない（届けばフォルダから戻る）。
    assert(TopHasText("見本のフラグ"));
    Fake::ctrpfResumed = true;
    Fake::keys = Key::B; Frames(5);
    assert(TopHasText("見本のフラグ"));
    Fake::keys = 0; Frames(2);
    Press(Key::B); Frames(15);
    assert(!TopHasText("見本のフラグ"));
    // 知らせの無い時は、これまでどおり SELECT の押下で閉じる。
    Press(Key::Select); Frames(40);
    assert(lyt::Last().screens[0].count == 0);

    // C が無くても開く。通知は開いたときの一度だけで、毎フレーム積まない。
    Fake::inputReady = false;
    Press(CTRPluginFramework::Key::Select); Frames(30); Blocked(true);
    assert(TopHasText("入力の遮断") && TopHasText("遮断が無効"));
    Frames(400); assert(!TopHasText("遮断が無効"));

    // 終了。
    // 停止中のスレッドが表を持ったままでも終了は待たない。新しい Set はもう行わない。
    const auto before_shutdown = Demand();
    mutex.Lock(); feature.Shutdown(); mutex.Unlock();
    Frames(3);
    assert(Demand().generation == before_shutdown.generation);
    feature.Shutdown(); // 返却できるときは要求も枠も外す。
    const auto after = Demand();
    assert(after.block == block::part::Select && after.defer == 0);
    assert(menu_screen::RequestToggle()); Frames(3);
    assert(Demand().generation == after.generation);
    feature.Shutdown();
    renderer::RendererFeature().Shutdown();
    Frames(1);
    assert(menu_screen::ShouldOpenCtrpfMenu());
    std::puts("menu screen: ok");
    return 0;
}

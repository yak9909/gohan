#include "ui/renderer/renderer.hpp"
#include "core/frame.hpp"
#include "game/lyt/renderer.hpp"
#include <cassert>
#include <limits>
#include <string>

namespace {
using namespace gohan::ui::renderer;
constexpr std::uint32_t White = 0xFFFFFFFFu;

void Geometry() {
    Canvas canvas;
    canvas.Begin(Screen::Top);
    assert(canvas.FillRect({-5, -5, 10, 10}, White) == Result::Added);
    const auto &first = canvas.Data().screens[0].commands[0];
    assert(first.bounds.x == 0 && first.bounds.y == 0 && first.bounds.width == 5 && first.bounds.height == 5);
    assert(canvas.BeginClip({10, 10, 20, 20}));
    assert(!canvas.BeginClip({0, 0, 1, 1}));
    assert(canvas.FillRect({0, 0, 15, 15}, White) == Result::Added);
    const auto &second = canvas.Data().screens[0].commands[1];
    assert(second.bounds.x == 10 && second.bounds.width == 5 && second.bounds.height == 5);
    assert(canvas.Text(0, 10, "gohan", White) == Result::Added);
    const auto &text = canvas.Data().screens[0].commands[2];
    assert(text.kind == gohan::game::lyt::Kind::Text && text.bounds.x == 0 && text.clip.x == 10);
    assert(canvas.FillRect({0, 0, 10, 10}, White) == Result::Empty);
    canvas.EndClip();
    assert(canvas.FillRect({0, 0, std::numeric_limits<int>::max(), 10}, White) == Result::Added);
    assert(canvas.FillRect({std::numeric_limits<int>::max(), 0, 100, 10}, White) == Result::Empty);
    assert(canvas.FrameRect({std::numeric_limits<int>::max(), 0, 2, 2}, White) == Result::Invalid);
    assert(canvas.FrameRect({0, 0, 1, 1}, White) == Result::Added);
    assert(canvas.FillRect({0, 0, 1, 1}, 0) == Result::Empty);
    canvas.Begin(Screen::Bottom);
    assert(canvas.FillRect({319, 239, 2, 2}, White) == Result::Added);
    const auto &bottom = canvas.Data().screens[1].commands[0];
    assert(bottom.bounds.width == 1 && bottom.bounds.height == 1);
    assert(canvas.Data().screens[0].count == 5);
    canvas.Begin(Screen::Bottom);
    assert(canvas.BeginClip({1, 1, -1, 1}));
    assert(canvas.FillRect({0, 0, 10, 10}, White) == Result::Empty);
    canvas.Begin(Screen::Bottom);
    assert(canvas.FillRect({0, 0, 10, 10}, White) == Result::Added);
}

void BoundsAndOrder() {
    Canvas canvas;
    canvas.Begin(Screen::Top);
    for (unsigned i = 0; i < gohan::core::limits::RendererRects - 2; ++i)
        assert(canvas.FillRect({0, 0, 1, 1}, White) == Result::Added);
    auto count = canvas.Data().screens[0].count;
    assert(canvas.FrameRect({0, 0, 10, 10}, White) == Result::Full);
    assert(canvas.Data().screens[0].count == count);
    assert(canvas.Text(0, 0, "gohan", White) == Result::Added);
    assert(canvas.FillRect({0, 0, 10, 10}, White) == Result::Added);
    const auto &drawing = canvas.Data().screens[0];
    assert(drawing.commands[count].kind == gohan::game::lyt::Kind::Text);
    assert(drawing.commands[count + 1].kind == gohan::game::lyt::Kind::Rectangle);
    assert(drawing.estimatedBytes == 31 * 204 + 552 + 5 * 48);
}

void TextAndSlots() {
    Canvas canvas;
    canvas.Begin(Screen::Top);
    assert(MeasureText("gohan") == 20);
    assert(MeasureText("012", 1, Font::Numeric) == 15);
    assert(MeasureText("A\xF0\x9F\x98\x80") == 4);
    assert(canvas.Text(0, 0, "\xC0\x80", White) == Result::Invalid);
    assert(canvas.Text(0, 0, "gohan", White, 0) == Result::Invalid);
    assert(canvas.Text(0, 0, "gohan", White, 9) == Result::Invalid);
    assert(canvas.Text(0, 0, std::string(29, 'A'), White) == Result::Truncated);
    assert(canvas.Data().screens[0].commands[0].length == 28);
    assert(canvas.Data().screens[0].commands[0].text[28] == 0);
    canvas.Begin(Screen::Top);
    for (unsigned i = 0; i < gohan::core::limits::RendererTexts; ++i)
        assert(canvas.Text(0, 0, "A", White) == Result::Added);
    assert(canvas.Text(0, 0, "B", White) == Result::Full);
    const auto &drawing = canvas.Data().screens[0];
    assert(drawing.texts == gohan::core::limits::RendererTexts);
    for (std::size_t i = 0; i < drawing.count; ++i)
        for (std::size_t j = i + 1; j < drawing.count; ++j)
            assert(drawing.commands[i].slot != drawing.commands[j].slot);
    canvas.Begin(Screen::Top);
    for (int i = 0; i < 4; ++i)
        assert(canvas.Text(0, 0, std::string(28, 'A'), White) == Result::Added);
    assert(canvas.Text(0, 0, std::string(28, 'A'), White) == Result::Full);
    assert(canvas.Text(0, 0, "A", White) == Result::Added);
}

void Dispatch() {
    const auto &feature = RendererFeature();
    assert(feature.Wire());
    assert(ReadStatus() == Status::Waiting);
    Canvas canvas;
    canvas.Begin(Screen::Top);
    canvas.FrameRect({20, 20, 120, 40}, White);
    canvas.Text(32, 32, "gohan", White, 2);
    assert(Commit(canvas));
    assert(SetEnabled(true));
    assert(ReadStatus() == Status::Waiting);
    gohan::core::frame::Dispatch();
    assert(ReadStatus() == Status::MissingBindings);
    assert(SetEnabled(false));
    gohan::core::frame::Dispatch();
    assert(ReadStatus() == Status::Stopped);
    feature.Shutdown();
    gohan::core::frame::Dispatch();
    assert(ReadStatus() == Status::Stopped);
}
} // namespace

// 実機に依らない要求の伝達だけを確かめる。導入成功を仮定しない。
namespace gohan::game::lyt {
InstallResult InstallRenderer(const FontResource &font) {
    assert(font.atlasBytes == 65536 && font.glyphs == 695);
    return InstallResult::MissingBindings;
}
bool Present(const Drawing &) { assert(false); return false; }
void DetachRenderer() {}
} // namespace gohan::game::lyt

int main() {
    Geometry();
    BoundsAndOrder();
    TextAndSlots();
    Dispatch();
}

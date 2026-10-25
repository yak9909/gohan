#include "ui/renderer/renderer.hpp"
#include <cstdio>
#include "core/frame.hpp"
#include "game/lyt/renderer.hpp"
#include "ui/renderer/drawing_mailbox.hpp"
#include <cassert>
#include <limits>
#include <string>
#include <vector>
#include <cstring>

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

void DrawingChanges() {
    using namespace gohan;
    detail::DrawingMailbox box;
    Canvas canvas;
    game::lyt::ScreenDrawing read;
    std::uint32_t generation = 0;
    auto publish = [&] { assert(box.TryPublish(canvas.Data().screens[0])); };
    canvas.Begin(Screen::Top);
    assert(canvas.Text(1, 2, "gohan", White) == Result::Added);
    publish();
    assert(box.ReadIfNew(read, generation) == core::MailRead::Updated);
    const auto first = generation;
    // 再生側の改変は確定前の指示の比較へ混ぜない。
    read.commands[0].bounds.x = 99;
    publish();
    assert(box.ReadIfNew(read, generation) == core::MailRead::Unchanged && generation == first);
    // 長い字列→板→短い字列の順でも、使わない欄や末尾を比べない。
    canvas.Begin(Screen::Top); canvas.FillRect({1, 2, 20, 8}, White); publish();
    assert(box.ReadIfNew(read, generation) == core::MailRead::Updated);
    const auto rectangle = generation;
    Canvas other;
    other.Begin(Screen::Top); other.FillRect({1, 2, 20, 8}, White);
    assert(box.TryPublish(other.Data().screens[0]));
    assert(box.ReadIfNew(read, generation) == core::MailRead::Unchanged && generation == rectangle);
    canvas.Begin(Screen::Top); canvas.Text(1, 2, "go", White); publish();
    assert(box.ReadIfNew(read, generation) == core::MailRead::Updated);
    other.Begin(Screen::Top); other.Text(1, 2, "go", White);
    assert(box.TryPublish(other.Data().screens[0]));
    assert(box.ReadIfNew(read, generation) == core::MailRead::Unchanged);
    // 同じ個数でも文字・色・クリップ・画像の位相が変われば新しく確定する。
    other.Begin(Screen::Top); other.Text(1, 2, "GO", White);
    assert(box.TryPublish(other.Data().screens[0]));
    assert(box.ReadIfNew(read, generation) == core::MailRead::Updated);
    other.Begin(Screen::Top); other.Text(1, 2, "GO", 0xFF00FFFFu);
    assert(box.TryPublish(other.Data().screens[0]));
    assert(box.ReadIfNew(read, generation) == core::MailRead::Updated);
    other.Begin(Screen::Top); other.BeginClip({2, 2, 10, 10}); other.Text(1, 2, "GO", 0xFF00FFFFu);
    assert(box.TryPublish(other.Data().screens[0]));
    assert(box.ReadIfNew(read, generation) == core::MailRead::Updated);
    for (int phase : {0, 1}) {
        canvas.Begin(Screen::Top); canvas.Repeat({0, 0, 20, 20}, Texture::DashHorizontal, phase, 0); publish();
        assert(box.ReadIfNew(read, generation) == core::MailRead::Updated);
    }
    canvas.Begin(Screen::Top); publish();
    assert(box.ReadIfNew(read, generation) == core::MailRead::Updated && read.count == 0);
    publish();
    assert(box.ReadIfNew(read, generation) == core::MailRead::Unchanged);
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
    assert(drawing.estimatedBytes == (gohan::core::limits::RendererRects - 1) * 204 + 552 + 5 * 48);
}

void TitleFrames() {
    PrepareTitleBackground();
    const auto &font = Fonts();
    std::size_t resident = font.atlasBytes;
    for (std::size_t i = 0; i < font.textureCount; ++i)
        if (font.textures[i].gpuResident) resident += (font.textures[i].bytes + 127) / 128 * 128;
    assert(resident == 119296 && font.atlasBytes == 32768);
    assert(resident + gohan::core::limits::RendererGameAllocationSlack <= gohan::core::limits::RendererResidentHeapLimit);
    assert(!font.textures[3].gpuResident && !font.textures[4].gpuResident);
    Canvas canvas;
    for (auto screen : {Screen::Top, Screen::Bottom}) {
        canvas.Begin(screen);
        assert(canvas.Repeat({26, 29, 182, 11}, Texture::TitleSelection) == Result::Added);
    }
    Drawing drawing = canvas.Data();
    std::vector<std::uint8_t> expected(2048);
    for (unsigned frame = 0; frame <= 300; ++frame) {
        StepTitleAnimation(frame, drawing);
        assert(gohan::game::titlebg::BakeSelectionL4(TitleBackgroundSources(), 256, 16, 182, 11, expected.data(), frame));
        const auto &a = drawing.screens[0].commands[0], &b = drawing.screens[1].commands[0];
        assert(a.texture == b.texture && a.bounds.width == 182 && a.bounds.height == 11);
        assert(a.source.x == 0 && a.source.y == 0 && a.source.width == 182);
        const auto &image = font.textures[static_cast<unsigned>(a.texture)];
        assert(image.bytes == 2048 && std::memcmp(expected.data(), image.data, 2048) == 0);
        assert(drawing.screens[0].commands[1].bounds.width == 0);
    }
    std::puts("帯: 全150位相を2周・上下同位相・同じ合成式のL4全画素と照合");
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
    // 長さごとに、設計表で用意した枠を使い切った次の1件を拒否する。
    const auto slots_for = [](unsigned length) {
        unsigned count = 0;
        for (const auto capacity : gohan::core::limits::RendererSlotChars) count += capacity >= length;
        return count;
    };
    canvas.Begin(Screen::Top);
    for (unsigned i = 0; i < slots_for(28); ++i)
        assert(canvas.Text(0, 0, std::string(28, 'A'), White) == Result::Added);
    assert(canvas.Text(0, 0, std::string(28, 'A'), White) == Result::Full);
    assert(canvas.Text(0, 0, "A", White) == Result::Added);
    canvas.Begin(Screen::Top);
    for (unsigned i = 0; i < slots_for(17); ++i)
        assert(canvas.Text(0, 0, std::string(17, 'A'), White) == Result::Added);
    assert(canvas.Text(0, 0, std::string(17, 'A'), White) == Result::Full);
    // 画面ごとに数える。下画面は上画面の枠を使わない。
    canvas.Begin(Screen::Bottom);
    assert(canvas.Text(0, 0, std::string(28, 'A'), White) == Result::Added);
    assert(canvas.Data().screens[1].texts == 1 && canvas.Data().screens[0].texts == slots_for(17));
}

void ImagesAndSlices() {
    Canvas canvas;
    canvas.Begin(Screen::Top);
    assert(canvas.Image(10, 20, Part::Finger, 2, 0x80FFAA88u) == Result::Added);
    const auto &image = canvas.Data().screens[0].commands[0];
    assert(image.kind == gohan::game::lyt::Kind::Image && image.bounds.width == 26 && image.bounds.height == 16);
    assert(image.color == 0x80FFAA88u);
    assert(canvas.Image(0, 0, {-1, 0, 1, 1}) == Result::Invalid);
    assert(canvas.Image(0, 0, {255, 31, 2, 1}) == Result::Invalid);
    assert(canvas.Image(0, 0, Part::Finger, 0) == Result::Invalid);
    assert(canvas.Image(0, 0, Part::Count) == Result::Invalid);
    assert(canvas.Image(400, 0, Part::Finger) == Result::Empty);
    canvas.Begin(Screen::Top);
    assert(canvas.PartBox({4, 4, 228, 210}, Part::Window) == Result::Added);
    assert(canvas.Data().screens[0].count == 9);
    const auto &corner = canvas.Data().screens[0].commands[0];
    const auto &middle = canvas.Data().screens[0].commands[4];
    assert(corner.bounds.width == 4 && corner.bounds.height == 4);
    assert(middle.bounds.width == 220 && middle.bounds.height == 202);
    assert(middle.source.width == 1 && middle.source.height == 1);
    assert(canvas.PartBox({4, 4, 7, 7}, Part::Window) == Result::Invalid);
    assert(canvas.PartBox({4, 4, 40, 12}, Part::Band3) == Result::Invalid);
    assert(canvas.PartBox({4, 4, 40, 13}, Part::Band3) == Result::Added);
    assert(canvas.PartBox({4, 4, 5, 40}, Part::ScrollThumb) == Result::Added);
    assert(canvas.PartBox({4, 4, 10, 80}, Part::ScrollThumb, 2) == Result::Added);
    assert(canvas.Repeat({4, 4, 100, 1}, Texture::Ui) == Result::Invalid);
    assert(canvas.Repeat({4, 4, 100, 1}, Texture::DashHorizontal, -1, 9) == Result::Added);
    const auto &repeat = canvas.Data().screens[0].commands[canvas.Data().screens[0].count - 1];
    assert(repeat.kind == gohan::game::lyt::Kind::Repeat && repeat.source.x == 7 && repeat.source.y == 1);
    // 複数枚の部品は、途中で足りなくなったら全体を取り消す。
    canvas.Begin(Screen::Top);
    for (unsigned i = 0; i < gohan::core::limits::RendererRects - 8; ++i)
        assert(canvas.FillRect({0, 0, 1, 1}, White) == Result::Added);
    const auto count = canvas.Data().screens[0].count, bytes = canvas.Data().screens[0].estimatedBytes;
    assert(canvas.PartBox({0, 0, 100, 100}, Part::Window) == Result::Full);
    assert(canvas.Data().screens[0].count == count && canvas.Data().screens[0].estimatedBytes == bytes);
    canvas.Begin(Screen::Bottom);
    for (unsigned i = 0; i < gohan::core::limits::RendererBottomRects; ++i)
        assert(canvas.Image(0, 0, Part::Finger) == Result::Added);
    assert(canvas.Image(0, 0, Part::Finger) == Result::Full);
    assert(MeasureText("N") == 5 && MeasureText("/", 1, Font::Numeric) == 5);
}

// 描画の部品が呼ぶゲーム側の入口の代役（実機に依らない要求の伝達だけを確かめる）。
namespace stub {
using gohan::game::lyt::InstallResult;
InstallResult installResult = InstallResult::Ready;
int installs = 0, presents = 0, detaches = 0, busyLeft = 0, sleeps = 0;
bool presentOk = true;
bool installed = false;
gohan::game::lyt::Drawing lastPresented;
} // namespace stub

void Frame() { gohan::core::frame::Dispatch(); }

void Dispatch() {
    const auto &feature = RendererFeature();
    assert(feature.stage == gohan::core::ShutdownStage::GameHooks);
    assert(feature.Wire());
    assert(ReadStatus() == Status::Waiting);
    Canvas canvas;
    canvas.Begin(Screen::Top);
    canvas.FrameRect({20, 20, 120, 40}, White);
    canvas.Text(32, 32, "gohan", White, 2);
    assert(Commit(canvas, Screen::Top));
    assert(SetEnabled(true));
    assert(ReadStatus() == Status::Waiting);
    Frame();
    assert(ReadStatus() == Status::Ready && stub::installs == 1 && stub::presents == 1);
    // 上だけを確定した。下は空のまま。
    assert(stub::lastPresented.screens[0].count == 5 && stub::lastPresented.screens[1].count == 0);
    assert(stub::lastPresented.revisions[0] == 1 && stub::lastPresented.revisions[1] == 0);
    assert(Commit(canvas, Screen::Top));
    Frame();
    assert(stub::lastPresented.revisions[0] == 1);
    // 下の画面は別に確定できる。上の確定は変わらない。
    Canvas lower;
    lower.Begin(Screen::Bottom);
    lower.FillRect({0, 0, 160, 240}, White);
    assert(Commit(lower, Screen::Bottom));
    Frame();
    assert(stub::lastPresented.screens[0].count == 5 && stub::lastPresented.screens[1].count == 1);
    assert(stub::lastPresented.revisions[0] == 1 && stub::lastPresented.revisions[1] == 1);
    // 両方を一度に置き換える呼び方は今までどおり。
    canvas.Begin(Screen::Top);
    lower.Begin(Screen::Bottom);
    assert(Commit(canvas) && Commit(lower, Screen::Bottom));
    Frame();
    assert(stub::lastPresented.screens[0].count == 0 && stub::lastPresented.revisions[0] == 2);

    // 利用者が二人: 片方が取り下げても、もう片方が求めている間は取り外さない。
    assert(SetEnabled(Client::Menu, true));
    Frame();
    assert(SetEnabled(false));
    Frame();
    assert(ReadStatus() == Status::Ready && stub::detaches == 0 && stub::installs == 1);
    // 全員が取り下げたら、排出が終わるまで取り外し中。済むまで描画しない。
    stub::busyLeft = 3;
    const int presents = stub::presents;
    assert(SetEnabled(Client::Menu, false));
    Frame();
    assert(ReadStatus() == Status::Stopping && stub::detaches == 0);
    Frame();
    Frame();
    Frame();
    assert(ReadStatus() == Status::Stopping && stub::presents == presents && stub::detaches == 3);
    Frame();
    assert(ReadStatus() == Status::Stopped && stub::detaches == 4 && !stub::installed);
    const int detaches = stub::detaches;
    Frame();
    assert(stub::detaches == detaches && ReadStatus() == Status::Stopped);

    // 取り外しの最中に求められたら、排出が済んでから導入し直す。
    assert(SetEnabled(true));
    Frame();
    assert(ReadStatus() == Status::Ready && stub::installs == 2);
    const std::string background = TitleBackgroundStatusText();
    assert(background.find("常駐画像を確保できる空きがない") != std::string::npos);
    assert(background.find("合計 401184 / 最大の塊 399000 バイト") != std::string::npos);
    const std::string memory = MemoryStatusText();
    assert(memory.find("確保直前: 合計 738876 / 最大 603712") != std::string::npos);
    assert(memory.find("確保直後: 合計 640060 / 最大 504896") != std::string::npos);
    assert(memory.find("現在: 合計 430000 / 最大 300000") != std::string::npos);
    assert(memory.find("常駐: 画像 119296 / 管理分込み上限 120320 / 予算 120320") != std::string::npos);
    assert(memory.find("字形 32768 / UI（破線込み）33280 / 窓 32768") != std::string::npos);
    assert(memory.find("記録列等 427016") != std::string::npos);
    assert(memory.find("150コマ（常駐）150150") != std::string::npos);
    assert(memory.find("150 コマ・プラグイン") != std::string::npos);
    assert(memory.find("ゲーム・コピー＋排出") != std::string::npos);
    assert(memory.find("現在値は約30フレームごとに更新") != std::string::npos);
    stub::busyLeft = 2;
    assert(SetEnabled(false));
    Frame();
    assert(SetEnabled(true));
    Frame();
    assert(ReadStatus() == Status::Stopping && stub::installs == 2);
    Frame();
    assert(ReadStatus() == Status::Stopping && stub::installs == 2);
    Frame();
    assert(ReadStatus() == Status::Ready && stub::installs == 3 && stub::installed);

    // 描画の確定に失敗したら、排出してから失敗を知らせる。
    stub::presentOk = false;
    Frame();
    assert(ReadStatus() == Status::Stopping);
    Frame();
    assert(ReadStatus() == Status::PresentFailed && !stub::installed);
    stub::presentOk = true;
    assert(SetEnabled(false));
    Frame();
    assert(ReadStatus() == Status::Stopped);

    // 導入できなかった状態では、取り外す資源が無いので排出も呼ばない。
    stub::installResult = stub::InstallResult::ResourceFailed;
    assert(SetEnabled(true));
    Frame();
    assert(ReadStatus() == Status::ResourceFailed && !stub::installed);
    assert(std::string(StatusText()).find("確保") != std::string::npos);
    const int calls = stub::detaches;
    assert(SetEnabled(false));
    Frame();
    assert(ReadStatus() == Status::Stopped && stub::detaches == calls);
    stub::installResult = stub::InstallResult::MissingBindings;
    assert(SetEnabled(true));
    Frame();
    assert(ReadStatus() == Status::MissingBindings);
    assert(SetEnabled(false));
    Frame();
    stub::installResult = stub::InstallResult::Ready;
}

// 終了: 毎フレームのフックを止める前に、描画の部品が資源を排出して返す。
bool frameHookStopped = false;
bool drainedBeforeHook = false;
void Shutdown() {
    assert(SetEnabled(true));
    Frame();
    assert(ReadStatus() == Status::Ready);
    gohan::core::FeatureRegistry registry;
    static const gohan::core::Feature hook{
        "game.frame", [] { return true; },
        [] {
            // 毎フレームのフックが止まる時には、描画の部品はもう取り外されている。
            drainedBeforeHook = !stub::installed;
            frameHookStopped = true;
            gohan::core::frame::Global().Reset();
        },
        gohan::core::ShutdownStage::GameHooks};
    assert(registry.Add(hook) && registry.Add(RendererFeature()) && registry.WireAll());
    Frame(); // Wire は待機中と公開する。描画スレッドの代役が現在の状態を公開し直す。
    assert(ReadStatus() == Status::Ready);
    stub::busyLeft = 3;
    stub::sleeps = 0;
    registry.ShutdownAll();
    assert(frameHookStopped && drainedBeforeHook);
    // 待機のたびに描画スレッドの代役が 1 フレーム進む。
    assert(stub::sleeps >= 4 && ReadStatus() == Status::Stopped);
}
} // namespace

namespace gohan::game::lyt {
RendererMemory ReadRendererMemory() {
    return {{738876, 603712, true}, {640060, 504896, true}, {430000, 300000, true},
            119296, 120320, 831759,
            2 * (68 + gohan::core::limits::RendererRecordBytes + 28 * gohan::core::limits::RendererQueueEntries), 32768, 33280, 32768, 20480,
            {BackgroundResult::InsufficientSpace, 401184, 399000, true}};
}
InstallResult InstallRenderer(const FontResource &font, BackgroundAllocation *background) {
    if (background) *background = {BackgroundResult::InsufficientSpace, 401184, 399000, true};
    assert(font.atlasBytes == 32768 && font.glyphs == 703);
    ++stub::installs;
    stub::installed = stub::installResult == InstallResult::Ready;
    return stub::installResult;
}
bool Present(const Drawing &drawing) {
    assert(stub::installed);
    ++stub::presents;
    stub::lastPresented = drawing;
    return stub::presentOk;
}
bool DetachRenderer() {
    ++stub::detaches;
    if (stub::busyLeft > 0) {
        --stub::busyLeft;
        return false;
    }
    stub::installed = false;
    return true;
}
void WaitMilliseconds(unsigned) {
    ++stub::sleeps;
    Frame();
}
} // namespace gohan::game::lyt

int main() {
    DrawingChanges();
    Geometry();
    BoundsAndOrder();
    TextAndSlots();
    TitleFrames();
    ImagesAndSlices();
    Dispatch();
    Shutdown();
}

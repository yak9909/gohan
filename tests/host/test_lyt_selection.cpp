// 巡回面の公開、上下の記録の残存、8回の非参照待ち、キャッシュ命令を試す。
#include "game/lyt/engine.hpp"
#include "game/lyt/selection_frames.hpp"
#include "game/lyt/streaming_picture.hpp"
#include "lyt_font.hpp"
#include "lyt_game.hpp"
#include <array>
#include <cassert>
#include <cstring>
using namespace gohan::game::lyt;
using fakegame::Game;
namespace {
namespace limits = gohan::core::limits;
unsigned flushes = 0, draws = 0;
const std::uint8_t *uploadPixels = nullptr;
unsigned *publishedSlot = nullptr, slotBeforeFlush = 0;
bool ObserveFlush(void *data, std::size_t bytes) {
    if (uploadPixels) {
        // 排出の呼び出し時点で画素が揃い、呼び手への面番号はまだ公開されていない。
        assert(bytes == limits::RendererSelectionSurfaceBytes);
        std::array<std::uint8_t, limits::RendererSelectionSurfaceBytes> expected;
        expected.fill(0xA5);
        detail::CopySelectionFrame(expected.data(), uploadPixels);
        assert(std::memcmp(data, expected.data(), bytes) == 0);
        assert(*publishedSlot == slotBeforeFlush);
    }
    return Game::Flush(data, bytes);
}
unsigned *Flush(gohan::game::generated::NwLytDrawContext *, unsigned *cursor) {
    ++flushes;
    *cursor++ = 0xABC;
    *cursor++ = 0xDEF;
    return cursor;
}
unsigned *Draw(gohan::game::generated::NwLytPane *, unsigned *cursor,
               gohan::game::generated::NwLytDrawInfo *, gohan::game::generated::NwLytDrawContext *) {
    ++draws;
    assert(cursor[-2] == 0x10000 && cursor[-1] == 0x40080);
    *cursor++ = 0x123;
    *cursor++ = 0x456;
    return cursor;
}
void CacheCommands() {
    std::array<unsigned, 8> commands{};
    for (bool pending : {false, true}) {
        auto *end = detail::DrawStreamingPicture(nullptr, commands.data(), nullptr, nullptr, pending, Flush, Draw);
        assert(end == commands.data() + (pending ? 6 : 4));
        if (pending) assert(commands[0] == 0xABC && commands[1] == 0xDEF);
    }
    assert(flushes == 1 && draws == 2);
}
Drawing Reference(unsigned top, unsigned bottom) {
    Drawing d;
    for (unsigned s = 0; s < 2; ++s) {
        auto &screen = d.screens[s];
        auto &c = screen.commands[0];
        c.kind = Kind::Image;
        c.texture = static_cast<Texture>(static_cast<unsigned>(Texture::TitleSelection) + (s ? bottom : top));
        c.bounds = c.clip = c.source = {0, 0, 182, 11};
        c.scale = 1; c.color = 0xFFFFFFFFu;
        screen.count = screen.rectangles = 1;
        d.revisions[s] = 1;
    }
    return d;
}
}
int main() {
    CacheCommands();
    Game::Reset();
    fakegame::UiFont font;
    // 余白が初期値のまま残る事を見るため、面の初期値は0以外にする。
    font.pixels.fill(0xA5);
    std::array<std::uint8_t, limits::RendererSelectionFrameBytes> pixels{};
    auto api = fakegame::Api();
    api.flush = ObserveFlush;
    detail::Engine engine(api);
    unsigned slot = 99;
    assert(!engine.UpdateSelection(pixels.data(), slot));
    assert(engine.Install(font.resource) == InstallResult::Ready);
    assert(!engine.UpdateSelection(nullptr, slot));
    Drawing d;
    // 300フレーム連続して全150位相を2周する。同じ面を続けて公開しない。
    for (unsigned frame = 0; frame < 300; ++frame) {
        pixels.fill(static_cast<std::uint8_t>(frame));
        uploadPixels = pixels.data(); publishedSlot = &slot; slotBeforeFlush = slot;
        assert(engine.UpdateSelection(pixels.data(), slot));
        uploadPixels = nullptr;
        assert(slot == frame % limits::RendererSelectionSurfaces);
        const auto *gpu = static_cast<const std::uint8_t *>(engine.Resources().Atlas());
        // 実画素の96本の範囲は新しいコマ、それ以外は導入時の値のまま。
        std::array<std::uint8_t, limits::RendererSelectionSurfaceBytes> expected;
        expected.fill(0xA5);
        detail::CopySelectionFrame(expected.data(), pixels.data());
        assert(std::memcmp(gpu + TextureOffset(font.resource, 6 + slot), expected.data(), expected.size()) == 0);
        d = Reference(slot, slot); d.revisions[0] = d.revisions[1] = frame + 1;
        assert(engine.Present(d)); Game::EndPasses();
    }
    // 下の旧記録だけが面を保持する場合も、その面へ上書きしない。
    for (unsigned frame = 0; frame < 30; ++frame) {
        assert(engine.UpdateSelection(pixels.data(), slot) && slot != 9);
        d.screens[0] = Reference(slot, slot).screens[0]; ++d.revisions[0];
        assert(engine.Present(d)); Game::EndPasses();
    }
    // 木が登録中なら変更は延期され、旧面の参照も消えない。
    assert(engine.Present(d));
    const auto oldRevision = d.revisions[0];
    d = {}; d.revisions[0] = d.revisions[1] = oldRevision + 1;
    for (unsigned n = 0; n < 12; ++n) assert(engine.Present(d));
    Game::EndPasses();
    for (unsigned n = 0; n < limits::RendererSelectionIdlePresents - 1; ++n) {
        assert(engine.Present(d)); Game::EndPasses();
        assert(engine.UpdateSelection(pixels.data(), slot) && slot != 9);
    }
    assert(engine.Present(d)); Game::EndPasses();
    Game::flushFails = true;
    const auto previous = slot;
    assert(!engine.UpdateSelection(pixels.data(), slot) && slot == previous);
    Game::flushFails = false;
    bool reused = false;
    for (unsigned n = 0; n < limits::RendererSelectionSurfaces; ++n) {
        assert(engine.UpdateSelection(pixels.data(), slot));
        reused |= slot == 9;
    }
    assert(reused);
    for (unsigned n = 1; n < detail::Engine::DrainFrames; ++n) assert(!engine.Detach());
    assert(engine.Detach() && Game::Balanced());
    assert(!engine.UpdateSelection(pixels.data(), slot));
}

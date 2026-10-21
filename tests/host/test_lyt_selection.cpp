// 二重画像の公開済み世代・上下の記録・GPU の排出待ちを試す。
#include "game/lyt/engine.hpp"
#include "lyt_font.hpp"
#include "lyt_game.hpp"
#include <array>
#include <cassert>
#include <cstring>
using namespace gohan::game::lyt;
using fakegame::Game;
int main() {
    Game::Reset();
    fakegame::TestFont font;
    std::array<std::uint8_t, 2048> pixels{};
    std::array<TextureResource, 8> textures{};
    for (auto &t : textures) t = {pixels.data(), pixels.size(), 256, 16, TextureFormat::L4};
    font.resource.textures = textures.data(); font.resource.textureCount = textures.size(); font.resource.requiredTextureCount = 3;
    detail::Engine engine(fakegame::Api()); unsigned slot = 99;
    assert(!engine.UpdateSelection(pixels.data(), pixels.size(), slot));
    assert(engine.Install(font.resource) == InstallResult::Ready);
    assert(!engine.UpdateSelection(pixels.data(), pixels.size() - 1, slot));
    Drawing opening; opening.screens[0].count = 1;
    opening.screens[0].commands[0].kind = Kind::Image;
    // 窓だけを描いて背景を借り、帯の画像はまだ参照しない。
    opening.screens[0].commands[0].texture = Texture::TitleWindow;
    opening.screens[0].commands[0].bounds = opening.screens[0].commands[0].clip = {0, 0, 8, 8};
    opening.screens[0].commands[0].source = {0, 0, 8, 8};
    opening.screens[0].commands[0].scale = 1;
    opening.screens[0].commands[0].color = 0xFFFFFFFFu;
    assert(engine.Present(opening)); Game::EndPasses();
    pixels.fill(0x75);
    assert(engine.UpdateSelection(pixels.data(), pixels.size(), slot) && slot == 0);
    const auto *gpu = static_cast<const std::uint8_t *>(engine.Resources().Background());
    assert(std::memcmp(gpu + TextureOffset(font.resource, 6), pixels.data(), pixels.size()) == 0);
    Drawing d;
    for (unsigned s = 0; s < 2; ++s) {
        auto &screen = d.screens[s]; screen.count = screen.rectangles = 1;
        auto &c = screen.commands[0]; c.kind = Kind::Repeat; c.texture = Texture::TitleSelection;
        c.bounds = c.clip = {0, 0, 100, 11}; c.source = {0, 0, 256, 16}; c.scale = 1; c.color = 0xFFFFFFFFu;
        d.revisions[s] = 1;
    }
    assert(engine.Present(d)); Game::EndPasses();
    assert(engine.UpdateSelection(pixels.data(), pixels.size(), slot) && slot == 1);
    assert(!engine.UpdateSelection(pixels.data(), pixels.size(), slot));
    // 上だけを切り替えても、下の記録に残る旧画像を更新してはいけない。
    d.screens[0].commands[0].texture = Texture::TitleSelectionBack; ++d.revisions[0];
    for (unsigned n = 0; n < 12; ++n) { assert(engine.Present(d)); Game::EndPasses(); }
    assert(!engine.UpdateSelection(pixels.data(), pixels.size(), slot));
    d.screens[1].commands[0].texture = Texture::TitleSelectionBack; ++d.revisions[1];
    for (unsigned n = 0; n < detail::Engine::DrainFrames - 1; ++n) {
        assert(engine.Present(d)); Game::EndPasses();
        assert(!engine.UpdateSelection(pixels.data(), pixels.size(), slot));
    }
    assert(engine.Present(d)); Game::EndPasses();
    Game::flushFails = true;
    assert(!engine.UpdateSelection(pixels.data(), pixels.size(), slot));
    Game::flushFails = false;
    assert(engine.UpdateSelection(pixels.data(), pixels.size(), slot) && slot == 0);
    for (unsigned n = 0; n < detail::Engine::DrainFrames - 1; ++n) assert(!engine.Detach());
    assert(engine.Detach() && Game::Balanced());
    assert(!engine.UpdateSelection(pixels.data(), pixels.size(), slot));
}

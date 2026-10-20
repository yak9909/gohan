#include "ui/renderer/renderer.hpp"
#include "ui_values.hpp"
#include <cstdio>

namespace gohan::ui::renderer {
namespace {
namespace v = core::design::ca2;
static_assert(v::Titlebg::Source::Size[0] == 16 && v::Titlebg::Source::Size[1] == 16);
static_assert(v::Titlebg::Window::ImageSize[0] % 8 == 0 && v::Titlebg::Window::ImageSize[1] % 8 == 0);
static_assert(v::Titlebg::Selection::ImageSize[0] % 8 == 0 && v::Titlebg::Selection::ImageSize[1] % 8 == 0);
game::titlebg::Sources sources;
game::titlebg::Status backgroundStatus;
bool prepared = false;
// 読み込み後は書き換えない。ゲームのヒープを使うのは描画導入時の GPU 用コピーだけ。
constexpr unsigned WindowWidth = v::Titlebg::Window::ImageSize[0];
constexpr unsigned WindowHeight = v::Titlebg::Window::ImageSize[1];
// 最大の窓 228×210 を覆う最小の 2 の冪。RGBA8 では 262,144 バイトになる。
static_assert(WindowWidth == 256 && WindowHeight == 256);
std::uint8_t window[WindowWidth * WindowHeight / 2];
std::uint8_t alpha[16 * 16 * 4], luminance[16 * 16 * 4];
static_assert(sizeof(window) + sizeof(alpha) + sizeof(luminance) <= 40000);
#ifndef __3DS__
// 停止見本はプレビュー専用。実機の BSS・GPU 確保には含めない。
std::uint8_t selection[v::Titlebg::Selection::ImageSize[0] * v::Titlebg::Selection::ImageSize[1] * 4];
#endif
game::lyt::TextureResource images[] = {
    {alpha, sizeof(alpha), 16, 16}, {luminance, sizeof(luminance), 16, 16},
    {window, sizeof(window), WindowWidth, WindowHeight, game::lyt::TextureFormat::L4,
     0xFF000000u | v::Window::Overlay::Color[0], 0xFF000000u | v::Window::Overlay::Color[1],
     unsigned(v::Window::Overlay::Alpha * 255 + .5)},
#ifndef __3DS__
    {selection, sizeof(selection), v::Titlebg::Selection::ImageSize[0], v::Titlebg::Selection::ImageSize[1]},
#endif
};
}
void PrepareTitleBackground() {
    if (prepared) return;
    backgroundStatus = game::titlebg::LoadSelf(sources);
    game::titlebg::BakeWindowL4(backgroundStatus.ready ? &sources : nullptr, WindowWidth, WindowHeight, window);
    if (!backgroundStatus.ready) images[2].opacity = 0;
#ifndef __3DS__
    game::titlebg::Bake(backgroundStatus.ready ? &sources : nullptr, game::titlebg::Palette::Green,
                       images[3].width, images[3].height, selection);
#endif
    // A4 は形の不透明度、L4 は不透明の輝度。CPU の上から順を GPU の順に登録する。
    for (unsigned y = 0; y < 16; ++y) {
        for (unsigned x = 0; x < 16; ++x) {
            unsigned n = (y / 8 * 2 + x / 8) * 64;
            for (unsigned b = 0; b < 3; ++b)
                n += ((x >> b & 1) << (2 * b)) + ((y >> b & 1) << (2 * b + 1));
            alpha[n * 4] = sources.alpha[y * 16 + x];
            alpha[n * 4 + 1] = alpha[n * 4 + 2] = alpha[n * 4 + 3] = 255;
            luminance[n * 4] = 255;
            luminance[n * 4 + 1] = luminance[n * 4 + 2] = luminance[n * 4 + 3] = sources.luminance[y * 16 + x];
        }
    }
    prepared = true;
}
const game::titlebg::Sources *TitleBackgroundSources() { return backgroundStatus.ready ? &sources : nullptr; }
const game::lyt::TextureResource &TitleBackgroundImage(unsigned index) { return images[index]; }
const char *TitleBackgroundStatusText() {
    static char text[768];
    const auto &s = backgroundStatus;
    std::snprintf(text, sizeof(text), "背景: %s\n更新: %s / Result=%08lX\n本体: %s / Result=%08lX\n%s",
                  s.ready ? (s.fromUpdate ? "更新から取得" : "本体から取得") : "代わりの色",
                  s.update.attempted ? game::titlebg::FailureText(s.update.failure) : "未試行",
                  static_cast<unsigned long>(s.update.result),
                  s.base.attempted ? game::titlebg::FailureText(s.base.failure) : "未試行",
                  static_cast<unsigned long>(s.base.result), BackgroundAllocationStatusText());
    return text;
}
} // namespace gohan::ui::renderer

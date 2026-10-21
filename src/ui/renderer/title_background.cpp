#include "ui/renderer/renderer.hpp"
#include "ui_values.hpp"
#include "ui/renderer/selection_frames.hpp"
#include "ui/renderer/stopwatch.hpp"
#include "game/lyt/selection_frame_cache.hpp"
#include <cstdio>
#include <cstring>

namespace gohan::ui::renderer {
namespace {
namespace v = core::design::ca2;
static_assert(v::Titlebg::Source::Size[0] == 16 && v::Titlebg::Source::Size[1] == 16);
static_assert(v::Titlebg::Window::ImageSize[0] % 8 == 0 && v::Titlebg::Window::ImageSize[1] % 8 == 0);
static_assert(v::Titlebg::Selection::ImageSize[0] % 8 == 0 && v::Titlebg::Selection::ImageSize[1] % 8 == 0);
game::titlebg::Sources sources;
game::titlebg::Status backgroundStatus;
bool prepared = false;
// 帯の150コマは導入時に一度だけ合成し、プラグイン側のメモリ（.bss、150,150 B）に持つ。
// ゲームの描画用ヒープには置かない。毎フレームはその時のコマを小さな面へコピーするだけ。
game::lyt::detail::SelectionFrameCache frames;
unsigned prepareMicros = 0;
// 窓は不変。帯だけを小さな面へ巡回して使う。
constexpr unsigned WindowWidth = v::Titlebg::Window::ImageSize[0];
constexpr unsigned WindowHeight = v::Titlebg::Window::ImageSize[1];
static_assert(WindowWidth * WindowHeight / 2 == core::limits::RendererWindowTextureBytes);
constexpr unsigned SelectionWidth = v::Titlebg::Selection::ImageSize[0];
constexpr unsigned SelectionHeight = v::Titlebg::Selection::ImageSize[1];
static_assert(SelectionWidth * SelectionHeight / 2 == core::limits::RendererSelectionSurfaceBytes);
std::uint8_t window[core::limits::RendererWindowTextureBytes];
std::uint8_t alpha[16 * 16 * 4], luminance[16 * 16 * 4];
// 導入時にゲームの描画用ヒープへ複製される初期値。余白は0のまま変わらない。
std::uint8_t selection[core::limits::RendererSelectionSurfaces][core::limits::RendererSelectionSurfaceBytes];
game::lyt::TextureResource images[] = {
    {alpha, sizeof(alpha), 16, 16, game::lyt::TextureFormat::Rgba8, 0, 0xFFFFFFFFu, 255, false},
    {luminance, sizeof(luminance), 16, 16, game::lyt::TextureFormat::Rgba8, 0, 0xFFFFFFFFu, 255, false},
    {window, sizeof(window), WindowWidth, WindowHeight, game::lyt::TextureFormat::L4,
     0xFF000000u | v::Window::Overlay::Color[0], 0xFF000000u | v::Window::Overlay::Color[1],
     unsigned(v::Window::Overlay::Alpha * 255 + .5)},
    {selection[0], sizeof(selection[0]), SelectionWidth, SelectionHeight, game::lyt::TextureFormat::L4,
     v::Selection::Color[0], v::Selection::Color[1]},
    {selection[1], sizeof(selection[0]), SelectionWidth, SelectionHeight, game::lyt::TextureFormat::L4,
     v::Selection::Color[0], v::Selection::Color[1]},
    {selection[2], sizeof(selection[0]), SelectionWidth, SelectionHeight, game::lyt::TextureFormat::L4,
     v::Selection::Color[0], v::Selection::Color[1]},
    {selection[3], sizeof(selection[0]), SelectionWidth, SelectionHeight, game::lyt::TextureFormat::L4,
     v::Selection::Color[0], v::Selection::Color[1]},
    {selection[4], sizeof(selection[0]), SelectionWidth, SelectionHeight, game::lyt::TextureFormat::L4,
     v::Selection::Color[0], v::Selection::Color[1]},
    {selection[5], sizeof(selection[0]), SelectionWidth, SelectionHeight, game::lyt::TextureFormat::L4,
     v::Selection::Color[0], v::Selection::Color[1]},
    {selection[6], sizeof(selection[0]), SelectionWidth, SelectionHeight, game::lyt::TextureFormat::L4,
     v::Selection::Color[0], v::Selection::Color[1]},
    {selection[7], sizeof(selection[0]), SelectionWidth, SelectionHeight, game::lyt::TextureFormat::L4,
     v::Selection::Color[0], v::Selection::Color[1]},
    {selection[8], sizeof(selection[0]), SelectionWidth, SelectionHeight, game::lyt::TextureFormat::L4,
     v::Selection::Color[0], v::Selection::Color[1]},
    {selection[9], sizeof(selection[0]), SelectionWidth, SelectionHeight, game::lyt::TextureFormat::L4,
     v::Selection::Color[0], v::Selection::Color[1]},
};
}
void PrepareTitleBackground() {
    if (prepared) return;
    backgroundStatus = game::titlebg::LoadSelf(sources);
    game::titlebg::BakeWindowL4(backgroundStatus.ready ? &sources : nullptr, WindowWidth, WindowHeight, window);
    if (!backgroundStatus.ready) images[2].opacity = 0;
    {
        const detail::Stopwatch watch;
        frames.Prepare(backgroundStatus.ready ? &sources : nullptr);
        prepareMicros = watch.ElapsedMicros();
    }
    // 全面を位相0のコマで始める。以後は実画素の範囲だけを書き換える。
    for (unsigned slot = 0; slot < core::limits::RendererSelectionSurfaces; ++slot)
        game::lyt::detail::CopySelectionFrame(selection[slot], frames.Frame(0));
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
namespace detail {
const std::uint8_t *SelectionFrame(unsigned phase) { return frames.Frame(phase); }
void StoreSelection(unsigned slot, const std::uint8_t *packed) {
    game::lyt::detail::CopySelectionFrame(selection[slot], packed);
}
unsigned SelectionPrepareMicros() { return prepareMicros; }
} // namespace detail
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

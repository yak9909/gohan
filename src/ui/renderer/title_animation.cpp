#include "ui/renderer/renderer.hpp"
#include "ui_values.hpp"

namespace gohan::ui::renderer {
// 全コマを導入前に合成済み。実行中は GPU の参照範囲だけを変える。
void StartTitleAnimation() {}
void StopTitleAnimation() {}
void StepTitleAnimation(std::uint32_t frame, Drawing &drawing) {
    namespace v = core::design::ca2;
    const unsigned at = frame / v::Titlebg::Selection::UpdateFrames % v::Titlebg::Loop;
    const int x = at % v::Titlebg::Selection::Columns * v::Titlebg::Selection::CellSize[0];
    const int y = at / v::Titlebg::Selection::Columns * v::Titlebg::Selection::CellSize[1];
    for (unsigned i = 0; i < 2; ++i) {
        auto &screen = drawing.screens[i];
        bool changed = false;
        for (std::size_t n = 0; n < screen.count; ++n) {
            auto &c = screen.commands[n];
            if (c.texture != Texture::TitleSelection && c.texture != Texture::TitleSelectionBack) continue;
            changed |= c.kind != game::lyt::Kind::Image || c.source.x != x || c.source.y != y;
            c.kind = game::lyt::Kind::Image;
            c.texture = Texture::TitleSelection;
            c.source = {x, y, c.bounds.width, c.bounds.height};
        }
        if (changed) ++drawing.revisions[i];
    }
}
} // namespace gohan::ui::renderer

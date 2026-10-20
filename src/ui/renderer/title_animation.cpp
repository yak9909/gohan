#include "ui/renderer/renderer.hpp"
#include "ui_values.hpp"
#include "ui/renderer/selection_frames.hpp"
#include <algorithm>

namespace gohan::ui::renderer {
// 全コマを導入前に合成済み。実行中は GPU の参照範囲だけを変える。
void StartTitleAnimation() {}
void StopTitleAnimation() {}
void StepTitleAnimation(std::uint32_t frame, Drawing &drawing) {
    namespace v = core::design::ca2;
    const unsigned at = frame / v::Titlebg::Selection::UpdateFrames % v::Titlebg::Loop;
    const unsigned offset = at * v::Titlebg::Selection::CellSize[0];
    for (unsigned i = 0; i < 2; ++i) {
        auto &screen = drawing.screens[i];
        bool changed = false;
        for (std::size_t n = 0; n + 1 < screen.count; ++n) {
            auto &a = screen.commands[n], &b = screen.commands[n + 1];
            if (a.kind != game::lyt::Kind::Image || b.kind != game::lyt::Kind::Image ||
                a.slot != detail::SelectionFirst || b.slot != detail::SelectionSecond ||
                (a.texture != Texture::TitleSelection && a.texture != Texture::TitleSelectionBack)) continue;
            const int width = a.bounds.width + b.bounds.width;
            const auto start = detail::SelectionAt(offset);
            const int firstWidth = std::min(width, v::Titlebg::Selection::AtlasSize[0] - int(start.x));
            const auto end = detail::SelectionAt(offset + firstWidth);
            const auto oldSource = a.source;
            a.kind = b.kind = game::lyt::Kind::Image;
            a.texture = start.page ? Texture::TitleSelectionBack : Texture::TitleSelection;
            b.texture = end.page ? Texture::TitleSelectionBack : Texture::TitleSelection;
            a.bounds.width = firstWidth;
            b.bounds.x = a.bounds.x + firstWidth;
            b.bounds.width = width - firstWidth;
            a.source = {int(start.x), int(start.y), firstWidth, a.bounds.height};
            b.source = {int(end.x), int(end.y), b.bounds.width, b.bounds.height};
            changed |= oldSource.x != a.source.x || oldSource.y != a.source.y;
            ++n;
        }
        if (changed) ++drawing.revisions[i];
    }
}
} // namespace gohan::ui::renderer

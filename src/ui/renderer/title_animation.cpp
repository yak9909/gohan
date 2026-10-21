#include "ui/renderer/renderer.hpp"
#include "ui/renderer/selection_frames.hpp"
#include "ui/renderer/stopwatch.hpp"
#include "game/lyt/renderer.hpp"
#include "ui_values.hpp"
#include <algorithm>

namespace gohan::ui::renderer {
namespace {
detail::SelectionTiming timing;
unsigned selected = 0;
bool active = false;
}
namespace detail {
SelectionTiming ReadSelectionTiming() {
    auto result = timing;
    result.prepareMicros = SelectionPrepareMicros();
    return result;
}
}
void StartTitleAnimation() { active = true; }
void StopTitleAnimation() { active = false; }
void StepTitleAnimation(std::uint32_t frame, Drawing &drawing) {
    namespace v = core::design::ca2;
    bool needed = false;
    for (const auto &screen : drawing.screens)
        for (std::size_t n = 0; n < screen.count; ++n)
            needed |= screen.commands[n].kind == game::lyt::Kind::Image &&
                      screen.commands[n].slot == detail::SelectionFirst;
    if (!needed) return;
    const unsigned phase = frame / v::Titlebg::Selection::UpdateFrames % v::Titlebg::Loop;
    // 導入時に合成済みのコマを選ぶだけ。ここで色や座標の計算はしない。
    const auto *packed = detail::SelectionFrame(phase);
    const detail::Stopwatch watch;
    bool updated = true;
#ifdef __3DS__
    unsigned slot = selected;
    if (active && game::lyt::UpdateSelection(packed, slot)) selected = slot;
    else updated = false;
#else
    (void)active;
    selected = (selected + 1) % core::limits::RendererSelectionSurfaces;
    detail::StoreSelection(selected, packed);
#endif
    // 測るのは、コマの実画素のコピーと（実機では）データキャッシュの排出。
    const auto micros = watch.ElapsedMicros();
    timing.lastMicros = micros;
    timing.maxMicros = std::max(timing.maxMicros, micros);
    timing.totalMicros += micros;
    ++timing.samples;
    if (!updated) ++timing.unavailable;
    for (unsigned i = 0; i < 2; ++i) {
        auto &screen = drawing.screens[i];
        bool changed = false;
        for (std::size_t n = 0; n + 1 < screen.count; ++n) {
            auto &a = screen.commands[n], &b = screen.commands[n + 1];
            if (a.kind != game::lyt::Kind::Image || a.slot != detail::SelectionFirst ||
                b.slot != detail::SelectionSecond) continue;
            a.texture = static_cast<Texture>(static_cast<unsigned>(Texture::TitleSelection) + selected);
            a.bounds.width += b.bounds.width;
            a.source = {0, 0, a.bounds.width, a.bounds.height};
            b.bounds.x = a.bounds.x + a.bounds.width;
            b.bounds.width = 0;
            b.source.width = 0;
            changed = true;
            ++n;
        }
        if (changed) ++drawing.revisions[i];
    }
}
} // namespace gohan::ui::renderer

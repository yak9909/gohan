#include "ui/renderer/renderer.hpp"
#include "ui/renderer/selection_frames.hpp"
#include "game/lyt/renderer.hpp"
#include "ui_values.hpp"
#include <algorithm>
#ifdef __3DS__
#include <CTRPluginFramework.hpp>
#else
#include <chrono>
#endif

namespace gohan::ui::renderer {
namespace {
detail::SelectionTiming timing;
unsigned selected = 0;
bool active = false;
}
namespace detail {
SelectionTiming ReadSelectionTiming() { return timing; }
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
#ifdef __3DS__
    const CTRPluginFramework::Clock clock;
#else
    const auto start = std::chrono::steady_clock::now();
#endif
    const auto *pixels = detail::ComposeSelection(phase);
#ifdef __3DS__
    const auto micros = unsigned(clock.GetElapsedTime().AsMicroseconds());
#else
    const auto micros = unsigned(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count());
#endif
    timing.lastMicros = micros;
    timing.maxMicros = std::max(timing.maxMicros, micros);
    timing.totalMicros += micros;
    ++timing.samples;
#ifdef __3DS__
    unsigned slot = selected;
    if (!active || !game::lyt::UpdateSelection(pixels, core::limits::RendererSelectionSurfaceBytes, slot)) {
        ++timing.unavailable;
    } else {
        selected = slot;
        detail::StoreSelection(selected, pixels);
    }
#else
    (void)active;
    selected = (selected + 1) % core::limits::RendererSelectionSurfaces;
    detail::StoreSelection(selected, pixels);
#endif
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

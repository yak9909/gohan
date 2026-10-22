#pragma once
#include "game/lyt/selection_compositor.hpp"
#include "game/lyt/selection_frames.hpp"

namespace gohan::game::lyt::detail {
class SelectionFrameCache {
  public:
    void Prepare(const titlebg::Sources *sources) {
        namespace v = core::design::ca2;
        static_assert(v::Titlebg::Loop == core::limits::RendererSelectionPhases);
        SelectionCompositor compositor;
        compositor.Prepare(sources);
        std::array<std::uint8_t, core::limits::RendererSelectionSurfaceBytes> surface{};
        for (unsigned phase = 0; phase < core::limits::RendererSelectionPhases; ++phase) {
            compositor.Bake(phase, surface.data());
            PackSelectionFrame(frames_[phase], surface.data());
        }
    }
    const std::uint8_t *Frame(unsigned phase) const { return frames_[phase % core::limits::RendererSelectionPhases]; }

  private:
    std::uint8_t frames_[core::limits::RendererSelectionPhases][core::limits::RendererSelectionFrameBytes]{};
};
static_assert(sizeof(SelectionFrameCache) == core::limits::RendererSelectionCacheBytes);
} // namespace gohan::game::lyt::detail

#include "game/lyt/engine.hpp"
#include "game/lyt/selection_frames.hpp"
#include <initializer_list>

namespace gohan::game::lyt::detail {
InstallResult Engine::Install(const FontResource &font) {
    if (installed_)
        return InstallResult::Ready;
    if (!api_.manager() || !api_.textReady() || !scene_.Prepare(api_, font))
        return InstallResult::ResourceFailed;
    if (!resources_.Acquire(font))
        return InstallResult::ResourceFailed;
    font_ = &font;
    RebuildScene();
    installed_ = true;
    protectedSelection_ = core::limits::RendererSelectionSurfaces;
    nextSelection_ = 0;
    selectionReferences_[0] = selectionReferences_[1] = 0;
    for (auto &idle : selectionIdle_) idle = core::limits::RendererSelectionIdlePresents;
    stopping_ = false;
    idle_ = 0;
    return InstallResult::Ready;
}

void Engine::RebuildScene() {
    scene_.Build(api_, *font_, resources_.Atlas());
    generated::NwLytPane *roots[2] = {scene_.Root(Screen::Top), scene_.Root(Screen::Bottom)};
    const unsigned lists[2] = {resources_.List(Screen::Top), resources_.List(Screen::Bottom)};
    nodes_.Build(api_, roots, lists);
    built_[0] = built_[1] = 0;
    haveBuilt_[0] = haveBuilt_[1] = false;
}

bool Engine::Present(const Drawing &drawing) {
    if (!installed_)
        return false;
    if (stopping_)
        return true;
    for (const Screen screen : {Screen::Top, Screen::Bottom}) {
        const unsigned index = static_cast<unsigned>(screen);
        // 木はノードがゲームのリストに載っていない間にだけ組み替える。載っていたら次のフレームに回す。
        if ((!haveBuilt_[index] || built_[index] != drawing.revisions[index]) && !nodes_.Linked(screen)) {
            scene_.Apply(screen, drawing.screens[index]);
            nodes_.MarkDirty(screen);
            built_[index] = drawing.revisions[index];
            haveBuilt_[index] = true;
            selectionReferences_[index] = 0;
            for (std::size_t n = 0; n < drawing.screens[index].count && n < core::limits::RendererCommands; ++n) {
                const auto &c = drawing.screens[index].commands[n];
                if (c.kind != Kind::Image && c.kind != Kind::Repeat) continue;
                const auto texture = static_cast<unsigned>(c.texture);
                const auto first = static_cast<unsigned>(Texture::TitleSelection);
                if (texture >= first && texture < first + core::limits::RendererSelectionSurfaces && c.bounds.width > 0 && c.bounds.height > 0)
                    selectionReferences_[index] |= 1u << (texture - first);
            }
        }
    }
    for (unsigned slot = 0; slot < core::limits::RendererSelectionSurfaces; ++slot) {
        if ((selectionReferences_[0] | selectionReferences_[1]) & (1u << slot)) selectionIdle_[slot] = 0;
        else if (selectionIdle_[slot] < core::limits::RendererSelectionIdlePresents) ++selectionIdle_[slot];
    }
    for (const Screen screen : {Screen::Top, Screen::Bottom}) {
        const unsigned index = static_cast<unsigned>(screen);
        if (haveBuilt_[index] && scene_.Items(screen) > 0) nodes_.Register(screen);
    }
    return true;
}

bool Engine::UpdateSelection(const std::uint8_t *packed, unsigned &slot) {
    constexpr std::size_t bytes = core::limits::RendererSelectionSurfaceBytes;
    if (!installed_ || stopping_ || !resources_.Atlas() || !font_ || !packed) return false;
    for (unsigned n = 0; n < core::limits::RendererSelectionSurfaces; ++n) {
        const unsigned i = (nextSelection_ + n) % core::limits::RendererSelectionSurfaces;
        const auto index = static_cast<unsigned>(Texture::TitleSelection) + i;
        if (i == protectedSelection_ || selectionIdle_[i] < core::limits::RendererSelectionIdlePresents || index >= font_->textureCount || bytes != font_->textures[index].bytes) continue;
        auto *target = static_cast<std::uint8_t *>(resources_.Atlas()) + TextureOffset(*font_, index);
        // 余白は導入時の0のまま。実画素の96本の範囲だけを書き換え、面ごと排出する。
        CopySelectionFrame(target, packed);
        if (!api_.flush(target, bytes)) return false;
        protectedSelection_ = slot = i;
        nextSelection_ = (i + 1) % core::limits::RendererSelectionSurfaces;
        return true;
    }
    return false;
}

bool Engine::Detach() {
    if (!installed_)
        return true;
    stopping_ = true;
    if (nodes_.AnyLinked()) {
        idle_ = 0;
        return false;
    }
    if (++idle_ < DrainFrames)
        return false;
    if (!resources_.ReleaseIdle())
        return false;
    installed_ = false;
    stopping_ = false;
    font_ = nullptr;
    return true;
}
} // namespace gohan::game::lyt::detail

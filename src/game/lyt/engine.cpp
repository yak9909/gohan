#include "game/lyt/engine.hpp"
#include <initializer_list>

namespace gohan::game::lyt::detail {
InstallResult Engine::Install(const FontResource &font) {
    if (installed_)
        return InstallResult::Ready;
    if (!api_.manager() || !api_.textReady() || !scene_.Prepare(api_, font))
        return InstallResult::ResourceFailed;
    if (!resources_.Acquire(font))
        return InstallResult::ResourceFailed;
    scene_.Build(api_, font, resources_.Atlas());
    generated::NwLytPane *roots[2] = {scene_.Root(Screen::Top), scene_.Root(Screen::Bottom)};
    const unsigned lists[2] = {resources_.List(Screen::Top), resources_.List(Screen::Bottom)};
    nodes_.Build(api_, roots, lists);
    font_ = &font;
    built_[0] = built_[1] = 0;
    haveBuilt_[0] = haveBuilt_[1] = false;
    installed_ = true;
    stopping_ = false;
    idle_ = 0;
    return InstallResult::Ready;
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
        }
        if (haveBuilt_[index] && scene_.Items(screen) > 0)
            nodes_.Register(screen);
    }
    return true;
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

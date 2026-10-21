#include "game/lyt/engine.hpp"
#include <initializer_list>
#include <cstring>

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
    backgroundAttempted_ = false;
    backgroundIdle_ = 0;
    installed_ = true;
    protectedSelection_ = 2;
    selectionReferences_[0] = selectionReferences_[1] = 0;
    selectionIdle_[0] = selectionIdle_[1] = DrainFrames;
    stopping_ = false;
    idle_ = 0;
    return InstallResult::Ready;
}

void Engine::RebuildScene() {
    scene_.Build(api_, *font_, resources_.Atlas(), resources_.Background());
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
    bool needsBackground = false;
    for (const auto &screen : drawing.screens)
        for (std::size_t i = 0; i < screen.count && i < core::limits::RendererCommands; ++i) {
            const auto &command = screen.commands[i];
            const auto texture = static_cast<unsigned>(command.texture);
            if ((command.kind == Kind::Image || command.kind == Kind::Repeat) &&
                texture >= font_->requiredTextureCount && texture < font_->textureCount && font_->textures[texture].gpuResident)
                needsBackground = true;
        }
    if (needsBackground) {
        backgroundIdle_ = 0;
        // 一度の開閉につき一回だけ試す。空き不足時は単色の帯で描き続ける。
        // 閉じる途中で開き直した場合は、排出待ちの画像をそのまま再利用する。
        if (!backgroundAttempted_ && !nodes_.AnyLinked()) {
            if (resources_.AcquireBackground(*font_)) RebuildScene();
            backgroundAttempted_ = true;
        }
    }
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
                if (c.texture == Texture::TitleSelection) selectionReferences_[index] |= 1;
                if (c.texture == Texture::TitleSelectionBack) selectionReferences_[index] |= 2;
            }
        }
    }
    for (unsigned slot = 0; slot < 2; ++slot) {
        if ((selectionReferences_[0] | selectionReferences_[1]) & (1u << slot)) selectionIdle_[slot] = 0;
        else if (selectionIdle_[slot] < DrainFrames) ++selectionIdle_[slot];
    }
    // 背景を含まない画面を両方に適用し、前の登録が消えた後にGPUの参照を待つ。
    // 木の更新を待っている画面があれば、前の画像を解放しない。
    // 別の描画元が単色の枠等を出していても、背景の借用を引き延ばさない。
    if (!needsBackground && !nodes_.AnyLinked() &&
        haveBuilt_[0] && haveBuilt_[1] && built_[0] == drawing.revisions[0] && built_[1] == drawing.revisions[1]) {
        if (backgroundIdle_ < DrainFrames) ++backgroundIdle_;
        if (backgroundIdle_ == DrainFrames && resources_.ReleaseBackgroundIdle()) {
            backgroundAttempted_ = false;
            scene_.ForgetBackground();
        }
    } else if (!needsBackground) backgroundIdle_ = 0;
    for (const Screen screen : {Screen::Top, Screen::Bottom}) {
        const unsigned index = static_cast<unsigned>(screen);
        if (haveBuilt_[index] && scene_.Items(screen) > 0) nodes_.Register(screen);
    }
    return true;
}

bool Engine::UpdateSelection(const std::uint8_t *data, std::size_t bytes, unsigned &slot) {
    if (!installed_ || stopping_ || !resources_.Background() || !font_ || !data) return false;
    for (unsigned i = 0; i < 2; ++i) {
        const auto index = static_cast<unsigned>(Texture::TitleSelection) + i;
        if (i == protectedSelection_ || selectionIdle_[i] < DrainFrames || index >= font_->textureCount || bytes != font_->textures[index].bytes) continue;
        auto *target = static_cast<std::uint8_t *>(resources_.Background()) + TextureOffset(*font_, index);
        std::memcpy(target, data, bytes);
        if (!api_.flush(target, bytes)) return false;
        protectedSelection_ = slot = i;
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

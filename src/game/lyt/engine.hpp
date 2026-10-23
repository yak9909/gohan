#pragma once
#include "game/lyt/nodes.hpp"
#include "game/lyt/recording_resources.hpp"
#include "game/lyt/renderer.hpp"
#include "game/lyt/scene.hpp"

namespace gohan::game::lyt::detail {
// 資源の確保・毎フレームの反映・排出つきの取り外しを持つ。ゲームの描画スレッドだけが使う。
// 照合（VerifyRenderer）は呼び出し側が済ませる。
class Engine {
  public:
    // 取り外しで、ノードがリストから外れたあと GPU の参照が終わるまで待つフレーム数。
    static constexpr unsigned DrainFrames = 8;

    explicit Engine(const ResourceApi &api) : api_(api), resources_(api) {}
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    // 全画像を導入時に確保する。開閉では資源を変更しない。
    InstallResult Install(const FontResource &font);
    // 登録が必要なノードをゲームのリストに載せる。導入済みでなければ false。
    bool Present(const Drawing &drawing);
    // 描画スレッドから。導入時に合成済みの1コマ（1,001 B）を、使える面の実画素だけへコピーして排出する。
    // 公開済み・記録中・排出待ちの面へは書かない。
    bool UpdateSelection(const std::uint8_t *packed, unsigned &slot);
    // ノードの登録をやめ、リストから外れ GPU が使い終えるのを待って資源を返す。
    // 完全に返せたら true。false の間は毎フレーム呼ぶ。導入していなければ true。
    bool Detach();
    bool Installed() const { return installed_; }
    bool Stopping() const { return stopping_; }
    const RecordingResources &Resources() const { return resources_; }
    const LayoutNodes &Nodes() const { return nodes_; }

  private:
    void RebuildScene();
    const ResourceApi &api_;
    unsigned selectionIdle_[core::limits::RendererSelectionSurfaces]{};
    unsigned protectedSelection_ = core::limits::RendererSelectionSurfaces;
    unsigned nextSelection_ = 0;
    unsigned selectionReferences_[2] = {};
    RecordingResources resources_;
    Scene scene_;
    LayoutNodes nodes_;
    const FontResource *font_ = nullptr;
    bool installed_ = false;
    bool stopping_ = false;
    unsigned idle_ = 0;
    std::uint32_t built_[2]{};
    bool haveBuilt_[2]{};
};
} // namespace gohan::game::lyt::detail

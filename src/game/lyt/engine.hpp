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

    // 必須資源の確保に失敗したら拒否する。背景だけの失敗時は代わりの色で導入する。
    InstallResult Install(const FontResource &font);
    // 登録が必要なノードをゲームのリストに載せる。導入済みでなければ false。
    bool Present(const Drawing &drawing);
    // プラグインのスレッドから。公開済み・記録中・排出待ちの画像は拒否する。
    bool UpdateSelection(const std::uint8_t *data, std::size_t bytes, unsigned &slot);
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
    unsigned selectionIdle_[2] = {};
    unsigned protectedSelection_ = 2;
    unsigned selectionReferences_[2] = {};
    RecordingResources resources_;
    Scene scene_;
    LayoutNodes nodes_;
    const FontResource *font_ = nullptr;
    bool installed_ = false;
    bool stopping_ = false;
    unsigned idle_ = 0;
    bool backgroundAttempted_ = false;
    unsigned backgroundIdle_ = 0;
    std::uint32_t built_[2]{};
    bool haveBuilt_[2]{};
};
} // namespace gohan::game::lyt::detail

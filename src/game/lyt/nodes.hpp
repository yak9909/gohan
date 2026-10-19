#pragma once
#include "game/lyt/drawing.hpp"
#include "game/lyt/resource_api.hpp"
#include "game/lyt/scene.hpp"

namespace gohan::game::lyt::detail {
// 上・下の画面それぞれの Layout ノード。ゲームの管理者の上下のリストに毎フレーム登録する
// （ゲームは描画のあと、リストの全ノードを外す）。ノードと仮想表はプラグインのメモリに置く。
class LayoutNodes {
  public:
    // root は画面ごとの Pane の木の根、list は記録用コマンドリストの ID。
    void Build(const ResourceApi &api, generated::NwLytPane *root[2], const unsigned list[2]);
    // 木を替えたので、記録し直させる。
    void MarkDirty(Screen screen);
    // 画面のリストに載っていなければ登録する。管理者が無いなど登録できなければ false。
    bool Register(Screen screen);
    bool Linked(Screen screen) const;
    bool AnyLinked() const { return Linked(Screen::Top) || Linked(Screen::Bottom); }
    const generated::SsysMaLayoutNode &Node(Screen screen) const {
        return nodes_[static_cast<unsigned>(screen)].value;
    }
    const generated::SsysMaLayoutNodeVtable &Table() const { return table_.value; }

  private:
    const ResourceApi *api_ = nullptr;
    Aligned<generated::SsysMaLayoutNodeVtable> table_;
    Aligned<generated::SsysMaLayoutNode> nodes_[2];
};
} // namespace gohan::game::lyt::detail

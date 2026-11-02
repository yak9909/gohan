#pragma once
#include "core/limits.hpp"
#include "menu/engine.hpp"
#include "menu/motion.hpp"

// 仮のフォルダの並べ替え（お気に入り・固定中の項目。docs/design/menu-item-actions.md の 4.4・M2、docs/design/menu-ui.md の D.12.3）。
// R の操作のリスト（ActionList）と同じく Shell が持ち、エンジンの UiKind::Reorder の要求を受けて始まる。
// Begin で今のフォルダの種類（お気に入りか固定中の項目）を覚え、その並びの写しと持ち上げた行の位置だけを持つ。
// 上下では写しの中のとなりと入れ替える（端では止まる・リピートも効く）。
// A で覚えた種類に応じて Engine::SetFavoriteOrder か SetFixedOrder へ写しを書き戻し、B で写しを捨てる。
// どちらも要求を取り消し、持ち上げた行を下ろす。お気に入りと固定の並びは別で、一方の並べ替えは他方を変えない。
// 並べ替えの間は、エンジンが要求の間は入力を渡さないので、R・X・L・Y・左右・START・SELECT は受け付けない。
namespace gohan::menu {
class Reorder {
  public:
    // 並べ替えの間（入力を受けている）。モーダルに数える。
    bool Open() const { return open_; }
    // 並べ替えの間か、持ち上げ・下ろしの動きの途中。行の描き方と影を決める。
    bool Showing(u32 now) const { return open_ || !lift_.Done(now); }
    bool Animating(u32 now) const { return !lift_.Done(now); }
    // 持ち上げた行と、写しの並び（Showing の間に使う）。
    u16 Item() const { return item_; }
    const u16 *Items() const { return items_; }
    std::size_t Count() const { return count_; }
    // 持ち上げた行の位置（写しの中の添え字）。
    std::size_t Index() const { return index_; }
    // 持ち上げた量（画素。0 と ca2.fav.reorder.lift の間を動く）。
    float Lift(u32 now) const { return lift_.At(now); }
    // エンジンが UiKind::Reorder の要求を出した時に呼ぶ。held は今押されているボタン（押したままの物は新しい入力にしない）。
    void Begin(const Engine &engine, u16 held, u32 now);
    // 毎フレーム、エンジンの入力（Frame）の後に呼ぶ。開いていない間は何もしない。
    void Step(Engine &engine, u32 now, u16 held);

  private:
    void Swap(std::size_t to);
    // 写しを、覚えた仮のフォルダの並びから取る。また、写しを同じ並びへ書き戻す（A。版の数は並びが変わった時だけ増える）。
    void Copy(const Engine &engine);
    void Commit(Engine &engine);
    // 閉じる（A・B・要求の消失）。写しは覚えた仮のフォルダの並びに戻し、持ち上げた行を下ろす。
    void Close(const Engine &engine, u32 now);
    // 上下の押した瞬間とリピート。発火した方向（上・下の順のビット）を返す（一覧と同じ）。
    unsigned Fired(u16 held, u16 pressed, u32 now, bool *repeated);
    bool open_ = false;
    motion::Tween lift_; // 0 ↔ ca2.fav.reorder.lift（ca2.time.select）
    ListKind list_ = ListKind::None; // 並べ替えている仮のフォルダの種類（Begin で覚える）
    u16 items_[core::limits::MenuItems]{};
    std::size_t count_ = 0;
    std::size_t index_ = 0;
    u16 item_ = kNoItem;
    u16 prev_held_ = 0;
    u32 repeat_at_[2]{};
};
} // namespace gohan::menu

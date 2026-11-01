#pragma once
#include "menu/engine.hpp"
#include "menu/motion.hpp"
#include "menu/surface.hpp"

// R の操作のリスト（docs/design/menu-item-actions.md の 2.3、docs/design/menu-ui.md の D.12.1）。入力の枠（Entry）と同じく Shell が持つ。
// エンジンの UiKind::Actions の要求を受けて開く。上下でえらぶ、A・Y で実行して閉じる、B・R で取り消して閉じる。
// 開閉は 160 ms の動き。閉じる途中に R で開けば、今の位相から開き直す。
namespace gohan::menu {
class ActionList {
  public:
    // 開いていて入力を受けている（閉じる途中は偽）。モーダルに数える。
    bool Open() const { return open_; }
    // 開いているか、閉じる動きの途中。説明の窓や通知の重なりを決める。
    bool Showing(u32 now) const { return open_ || !appear_.Done(now); }
    bool Animating(u32 now) const { return !appear_.Done(now); }
    // エンジンが UiKind::Actions の要求を出した時に呼ぶ。held は今押されているボタン（押したままの物は新しい入力にしない）。
    void Begin(const Engine &engine, u16 held, u32 now);
    // 毎フレーム、エンジンの入力（Frame）の後に呼ぶ。開いていない間は何もしない。
    void Step(Engine &engine, u32 now, u16 held);
    // row_y は選んだ行の y（一覧の現在の位置）。
    void Draw(Surface &surface, u32 now, int row_y) const;

  private:
    void Close(u32 now);
    // 十字キーの押した瞬間とリピート。発火した方向を上・下・左・右の順のビットで返す（一覧と同じ）。
    unsigned Fired(u16 held, u16 pressed, u32 now, bool *repeated);
    bool open_ = false;
    motion::Tween appear_; // 0 → 1 で開く、1 → 0 で閉じる（時間・横移動は ca2.time.actions）
    ItemAction actions_[kMaxActions]{};
    std::size_t count_ = 0;
    std::size_t index_ = 0;
    u16 item_ = kNoItem;
    const char *title_ = "";
    u16 prev_held_ = 0;
    u32 repeat_at_[4]{};
};
} // namespace gohan::menu

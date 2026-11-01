#pragma once
#include "core/limits.hpp"
#include "menu/engine.hpp"
#include "menu/motion.hpp"
#include "menu/reorder.hpp"
#include "menu/surface.hpp"

// メニューの帯（題・行・足）と説明欄の表示。エンジンの状態を読み、見た目の動き
// （出入り・選択の帯・スクロール・跳ね・並べ替えの持ち上げ）だけを持つ。入力の解釈はしない。
namespace gohan::menu {
class Panel {
  public:
    // 毎フレーム、エンジンの状態が変わった後に呼ぶ。並べ替えの間は、写しの順で行を集める。
    void Sync(const Engine &engine, u32 now, const Reorder &reorder);
    // 描画の予算から決めた行数を知らせる（変わったらスクロールの目標を取り直す）。
    void SetRows(int rows, u32 now);
    // 跳ね。左右（値）・決定（A）。
    void NudgeValue(u32 now, int direction) { value_.Fire(now, direction); }
    void NudgeAction(u32 now) { action_.Fire(now, 1); }
    bool Visible(u32 now) const { return open_.At(now) > 0.0f || open_.to > 0.0f; }
    bool BarVisible(u32 now) const { return bar_.At(now) > 0.f || bar_.to > 0.f; }
    float OpenAmount(u32 now) const { return open_.At(now); }
    // 選んだ行の y（スクロールと選択の動きの途中でも、今描いている位置）。R の操作のリストの窓の位置に使う。
    int SelectedRowY(u32 now) const;
    bool Animating(u32 now) const;
    float ViewStart(u32 now) const { return view_.At(now); }
    int Rows() const { return rows_; }
    int Count() const { return count_; }
    u16 ItemAt(int index) const { return index >= 0 && index < count_ ? items_[index] : kNoItem; }
    float MaxViewStart() const { return count_ > rows_ ? static_cast<float>(count_ - rows_) : 0.0f; }
    // modal: ダイアログや入力の枠が出ている間（足と説明は描かない）。
    void Draw(Surface &surface, const Engine &engine, u32 now, bool modal);
    // 確認の操作案内は退場の写しでも同じ内容と出現量を使う。
    void DrawBar(Surface &surface, const Engine &engine, u32 now, bool dialog = false, float appear = 1.f);
    // 右の領域の説明。予算の残りで描く。
    void DrawDescription(Surface &surface, const Engine &engine, u32 now);

  private:
    int MenuX(u32 now) const;
    void DrawFrame(Surface &surface, const Engine &engine, int menu_x, u32 now);
    void DrawHeading(Surface &surface, const Engine &engine, int menu_x, const u16 *path, int depth, int count, int selected_index);
    void DrawRows(Surface &surface, const Engine &engine, int menu_x, u32 now);
    void DrawRowLayer(Surface &surface, const Engine &engine, int menu_x, u32 now,
                      const u16 *items, int count, float start, float selected, u16 selected_item);
    // 並べ替えで持ち上げた行（帯・影・字・印）。行の層の最後に、ほかの行より上に描く（D.12.3）。
    void DrawLifted(Surface &surface, const Engine &engine, int menu_x, u32 now, float start, float selected_index);
    void DrawRow(Surface &surface, const Engine &engine, u16 item, int menu_x, int y, int offset, bool selected, float weight, u32 now);
    void DrawScrollBar(Surface &surface, int menu_x, u32 now);
    void DrawFooter(Surface &surface, const Engine &engine, int menu_x, bool dialog);
    void DrawHoldBar(Surface &surface, const Engine &engine, int menu_x, u32 now);
    float TargetFor(int index) const;

    motion::Tween open_;
    motion::Tween bar_;
    struct SwitchMotion {
        motion::Tween phase;
        motion::Tween label_phase;
        unsigned label_from = 0, label_to = 0;
        float label_alpha = 1.f;
        float from = 0, to = 0;
        u32 fill = 0, edge = 0, knob = 0;
        u32 next_fill = 0, next_edge = 0, next_knob = 0;
        bool ready = false;
    } switches_[core::limits::MenuItems];
    motion::Tween selection_;
    motion::Tween view_;
    motion::Tween folder_phase_;
    motion::Bounce value_;
    motion::Bounce action_;
    bool was_visible_ = false;
    bool keep_view_ = false;
    u16 folder_ = kNoItem;
    u16 selected_ = kNoItem;
    int rows_ = style::kVisibleRows;
    int count_ = 0;
    u16 items_[core::limits::MenuItems + 1]{};
    u16 old_items_[core::limits::MenuItems + 1]{};
    u16 old_folder_ = kNoItem;
    u16 old_selected_ = kNoItem;
    int old_count_ = 0;
    float old_view_ = 0.f;
    float old_selection_ = 0.f;
    int folder_direction_ = 1;
    // 道筋（根からの入った順のフォルダ）。今の分と、フォルダを変える前の分（旧い行の題に使う）。
    u16 path_[timing::kMaxDepth]{};
    int path_depth_ = 0;
    u16 old_path_[timing::kMaxDepth]{};
    int old_path_depth_ = 0;
    // 並べ替えで持ち上げた行（選んでいる行で、並べ替えの間か下ろす動きの途中）と、持ち上げた量（画素）。
    u16 lifted_ = kNoItem;
    float lift_ = 0.f;
    // 並べ替えの間か（説明の状態「並べ替え中」は、これが真の間だけ。下ろす動きの途中は出さない）。
    bool reordering_ = false;
    int last_index_ = -1;
};
} // namespace gohan::menu

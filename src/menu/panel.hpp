#pragma once
#include "core/limits.hpp"
#include "menu/engine.hpp"
#include "menu/motion.hpp"
#include "menu/surface.hpp"

// メニューの帯（題・行・足）と説明欄の表示。エンジンの状態を読み、見た目の動き
// （出入り・選択の帯・スクロール・跳ね）だけを持つ。入力の解釈はしない。
namespace gohan::menu {
class Panel {
  public:
    // 毎フレーム、エンジンの状態が変わった後に呼ぶ。
    void Sync(const Engine &engine, u32 now);
    // 描画の予算から決めた行数を知らせる（変わったらスクロールの目標を取り直す）。
    void SetRows(int rows, u32 now);
    // 跳ね。左右（値）・決定（A）。
    void NudgeValue(u32 now, int direction) { value_.Fire(now, direction); }
    void NudgeAction(u32 now) { action_.Fire(now, 1); }
    bool Visible(u32 now) const { return open_.At(now) > 0.0f || open_.to > 0.0f; }
    bool BarVisible(u32 now) const { return bar_.At(now) > 0.f || bar_.to > 0.f; }
    float OpenAmount(u32 now) const { return open_.At(now); }
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
    void DrawHeading(Surface &surface, const Engine &engine, int menu_x, u16 folder, int count, int selected_index);
    void DrawRows(Surface &surface, const Engine &engine, int menu_x, u32 now);
    void DrawRowLayer(Surface &surface, const Engine &engine, int menu_x, u32 now,
                      const u16 *items, int count, float start, float selected, u16 selected_item);
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
    u16 items_[core::limits::MenuItems]{};
    u16 old_items_[core::limits::MenuItems]{};
    u16 old_folder_ = kNoItem;
    u16 old_selected_ = kNoItem;
    int old_count_ = 0;
    float old_view_ = 0.f;
    float old_selection_ = 0.f;
    int folder_direction_ = 1;
};
} // namespace gohan::menu

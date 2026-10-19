#pragma once
#include "core/limits.hpp"
#include "menu/engine.hpp"
#include "menu/motion.hpp"
#include "menu/surface.hpp"

// メニューの帯（題・行・足）と説明欄の表示。エンジンの状態を読み、見た目の動き
// （出入り・選択の帯・スクロール・跳ね）だけを持つ。入力の解釈はしない。
namespace gohan::menu {
// タッチで押せる範囲（描いた時に更新する）。key は押したのと同じ扱いにするボタン。
struct Hit {
    Rect bounds;
    u16 key = 0;
};

class Panel {
  public:
    static constexpr int kMaxHits = 8;

    // 毎フレーム、エンジンの状態が変わった後に呼ぶ。
    void Sync(const Engine &engine, u32 now);
    // 描画の予算から決めた行数を知らせる（変わったらスクロールの目標を取り直す）。
    void SetRows(int rows, u32 now);
    // 跳ね。左右（値）・決定（A）。
    void NudgeValue(u32 now, int direction) { value_.Fire(now, direction); }
    void NudgeAction(u32 now) { action_.Fire(now, 1); }
    // タッチで選んだ時は、スクロールを選んだ行に合わせ直さない。
    void KeepViewOnNextSelect() { keep_view_ = true; }
    // 指で動かしている間の表示位置（行単位）。
    void JumpView(float start, u32 now) { view_.Jump(start, now); }

    bool Visible(u32 now) const { return open_.At(now) > 0.0f || open_.to > 0.0f; }
    float OpenAmount(u32 now) const { return open_.At(now); }
    bool Animating(u32 now) const;
    float ViewStart(u32 now) const { return view_.At(now); }
    int Rows() const { return rows_; }
    int Count() const { return count_; }
    u16 ItemAt(int index) const { return index >= 0 && index < count_ ? items_[index] : kNoItem; }
    // 行の領域の y から、行の番号（無ければ -1）。
    int RowAt(int y, u32 now) const;
    float MaxViewStart() const { return count_ > rows_ ? static_cast<float>(count_ - rows_) : 0.0f; }
    // 触れた位置が帯の上か。
    bool InPanel(int x, u32 now) const;
    bool InList(int x, int y, u32 now) const;
    const Hit *Hits(int *count) const {
        *count = hit_count_;
        return hits_;
    }

    // modal: ダイアログや入力の枠が出ている間（足と説明は描かない）。
    void Draw(Surface &surface, const Engine &engine, u32 now, bool modal);
    // 右の領域の説明。予算の残りで描く。
    void DrawDescription(Surface &surface, const Engine &engine, u32 now);

  private:
    int MenuX(u32 now) const;
    void DrawFrame(Surface &surface, const Engine &engine, int menu_x, u32 now);
    void DrawRows(Surface &surface, const Engine &engine, int menu_x, u32 now);
    void DrawRow(Surface &surface, const Engine &engine, u16 item, int menu_x, int y, int offset, bool selected);
    void DrawScrollBar(Surface &surface, int menu_x, u32 now);
    void DrawFooter(Surface &surface, const Engine &engine, int menu_x);
    void DrawHoldBar(Surface &surface, const Engine &engine, int menu_x, u32 now);
    void AddHit(Rect bounds, u16 key);
    float TargetFor(int index) const;

    motion::Tween open_;
    motion::Tween selection_;
    motion::Tween view_;
    motion::Bounce value_;
    motion::Bounce action_;
    bool was_visible_ = false;
    bool keep_view_ = false;
    u16 folder_ = kNoItem;
    u16 selected_ = kNoItem;
    int rows_ = style::kVisibleRows;
    int count_ = 0;
    u16 items_[core::limits::MenuItems]{};
    Hit hits_[kMaxHits]{};
    int hit_count_ = 0;
};
} // namespace gohan::menu

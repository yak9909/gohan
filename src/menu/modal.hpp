#pragma once
#include "menu/engine.hpp"
#include "menu/surface.hpp"

// 確認は上画面の中央、値・選択・ボタンの割り当ての枠は右上に出す。
namespace gohan::menu {
struct Pointer {
    bool down = false;
    int x = 0, y = 0;
};
// 1 フレームぶんの入力。held はボタンの水準（hotkey.hpp の key のビット）。
struct Input {
    u16 held = 0;
    Pointer touch;
};

// ---- 確認のダイアログ ----
// 確認の位置と大きさは一定。
Rect DialogBox(int screen_width, bool menu_open);
Rect DialogOption(const Rect &box, int index);
// 互換用。上画面のタッチは常に無視する。
int DialogOptionAt(int screen_width, bool menu_open, int x, int y);
// menu_amount: メニューの出現量（位置の補間）。appear: ダイアログ自身の出現量。
void DrawDialog(Surface &surface, const Engine &engine, float menu_amount, float appear);

// ---- 入力の枠 ----
// エンジンが出した入力の要求（Number・Slider・TriggerInput・List・CheckList・Key）に答える。
class Entry {
  public:
    bool Active() const { return active_; }
    // 要求が出たフレームに呼ぶ。held はその時に押されているボタン（押したままの物は新しい入力にしない）。
    void Begin(const Engine &engine, const Input &input, int screen_width);
    void End() { active_ = false; }
    // 以後、毎フレーム。結果は engine.SubmitUi / CancelUi で返す。
    void Step(Engine &engine, u32 now, const Input &input, bool menu_open);
    void Draw(Surface &surface, float menu_amount, float appear, bool menu_open) const;

  private:
    enum class Phase : u8 { Release, Wait, Record };
    Rect Box(bool menu_open) const;
    void Finish(Engine &engine, s32 value, u32 now);
    void Cancel(Engine &engine);
    // 十字キーの押した瞬間とリピート。発火した方向を上・下・左・右の順のビットで返す。
    unsigned Fired(u16 held, u16 pressed, u32 now, bool *repeated);
    void StepNumber(Engine &engine, u32 now, const Input &input, u16 pressed);
    void StepList(Engine &engine, u32 now, const Input &input, u16 pressed);
    void StepKey(Engine &engine, u32 now, const Input &input);
    void SetValue(s32 value);
    Format DisplayFormat() const; // 実行の入力が 16 進なら 16 進で出す
    void KeepVisible();
    int Rows() const;
    void DrawNumber(Surface &surface, const Rect &box, float appear) const;
    void DrawList(Surface &surface, const Rect &box, float appear) const;
    void DrawKey(Surface &surface, const Rect &box, float appear) const;

    bool active_ = false;
    int screen_width_ = 400;
    UiKind kind_ = UiKind::None;
    const ItemDef *def_ = nullptr;
    u16 item_ = kNoItem;
    u16 prev_held_ = 0;
    u32 repeat_at_[4]{};
    u32 select_at_ = 0;
    // 数値
    s32 value_ = 0, min_ = 0, max_ = 0, step_ = 1;
    // 選択
    int index_ = 0, count_ = 0, top_ = 0;
    u32 mask_ = 0;
    // 割り当て
    Phase phase_ = Phase::Release;
    u16 captured_ = 0;
};
} // namespace gohan::menu

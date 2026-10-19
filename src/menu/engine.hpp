#pragma once
#include "core/limits.hpp"
#include "menu/binding.hpp"
#include "menu/hotkey.hpp"
#include "menu/notice.hpp"
#include "menu/timing.hpp"
#include "menu/types.hpp"

// メニューのエンジン。フォルダの移動・選択・編集・適用待ち・適用と差し戻し・ホットキー・待機・確認・排他を持つ。
// 描画・コントローラ・スレッド・時計に依存しない。入力（held）と時刻（now、ミリ秒）は引数で渡す。
// 呼び出しは 1 つのスレッドから行う（ゲームの描画スレッドを想定）。
namespace gohan::menu {
// 項目ごとの状態。編集値 value と適用値 applied を分ける（機能が読むのは applied）。
struct ItemState {
    s32 value = 0;
    s32 applied = 0;
    u16 hotkey = 0;
    u16 applied_hotkey = 0;
    s32 fixed_value = 0;
    u32 feedback_until = 0;
    bool effect = false;   // 効果が入っているか（Patch に is_active が無い時の記録）
    bool seen = false;     // 通知済みの効果
    bool available = true; // ゲームの値を読めた（source が game の項目）
    bool fixed = false;
    bool mode_active = false;
    bool hold_active = false;
    bool feedback = false; // 「実行した」表示の間
};

enum class DialogKind : u8 { None, Close, ApplyAll, RevertAll, Action };
// 画面側が用意する入力の要求。結果は SubmitUi / CancelUi で返す。
enum class UiKind : u8 { None, Number, Slider, List, CheckList, Key, TriggerInput };
struct UiRequest {
    UiKind kind = UiKind::None;
    u16 item = kNoItem;
};

// 行の見た目。描画はこれを読むだけにする。
struct RowView {
    const ItemDef *def = nullptr;
    char value[24]{};     // 値欄の文字（OFF／待機／ON／予約／OK／動作中 など）
    bool button = false;  // 実行の項目（ボタンの見た目）
    bool pending = false; // 適用待ち（黄色）
    bool disabled = false; // 灰色。カーソルは置ける
    bool locked = false;  // 値の固定中（値欄を水色）
};

struct NoticeSink {
    void (*emit)(void *user, const Notice &notice) = nullptr;
    void *user = nullptr;
};

class Engine {
  public:
    Engine(const ItemDef *defs, std::size_t count);

    // ---- 結び付け ----
    bool Bind(const Bound &bound); // 範囲外・振る舞いの不一致・二重は false
    bool BindAll(const Bound *bounds, std::size_t count);
    void SetNoticeSink(NoticeSink sink) { sink_ = sink; }
    void SetDevVisible(bool visible) { dev_visible_ = visible; }
    void SetReservedHotkeys(u16 mask) { reserved_ = mask; } // メニューの開閉などに使うボタン。ホットキーに使えない

    // ---- 入力と時刻 ----
    void Open(u32 now);
    bool RequestClose(u32 now); // 適用待ちがあれば確認ダイアログを出して false
    void Frame(u32 now, u16 held);
    void Press(u16 key, u32 now, bool repeated = false);
    // タッチで行を選ぶ。今のフォルダの見える項目で、ダイアログと入力 UI が無い時だけ。
    bool Select(u16 item);

    // ---- 確認ダイアログ ----
    DialogKind Dialog() const { return dialog_; }
    bool DialogYes() const { return dialog_yes_; }
    u16 DialogItem() const { return dialog_item_; } // Action の確認の対象（無ければ kNoItem）
    void Confirm(bool yes, u32 now);

    // ---- 入力 UI ----
    UiRequest Ui() const { return ui_; }
    void SubmitUi(s32 value, u32 now);
    void CancelUi();

    // ---- 編集 ----
    bool SetValue(u16 item, s32 value);
    bool SetHotkey(u16 item, u16 mask);
    bool Commit(u16 item, u32 now);
    bool CommitAll(u32 now);
    void Discard(u16 item);
    void DiscardAll();
    bool SetFixed(u16 item, bool on, u32 now);

    // ---- 照会 ----
    bool Visible() const { return visible_; }
    std::size_t Count() const { return count_; }
    const ItemDef &Def(u16 item) const { return defs_[item]; }
    const ItemState &State(u16 item) const { return state_[item]; }
    s32 Applied(u16 item) const { return state_[item].applied; }
    std::size_t Depth() const { return depth_; }
    u16 Folder() const { return stack_[depth_]; }
    u16 Selected() const { return selection_[depth_]; }
    u16 FirstVisible(u16 folder) const;
    u16 NextVisible(u16 item) const;
    bool Hidden(u16 item) const;
    bool Disabled(u16 item) const;
    bool Dirty(u16 item) const;
    std::size_t DirtyCount() const;
    bool EffectOn(u16 item) const; // 効果の ON/OFF（項目の ON/OFF とは別）
    RowView View(u16 item) const;
    // 長押し中（X／L）の進み具合。千分率。muted は 2/5 未満（灰色で描く）。
    bool HoldProgress(u32 now, u32 *permille, bool *muted, u16 *key) const;

  private:
    // engine_core.cpp
    bool Has(u16 item) const { return item < count_; }
    bool HasValue(u16 item) const;
    bool CondHolds(const Cond &cond) const;
    u16 PrevVisible(u16 item) const;
    void Emit(const Notice &notice);
    void Move(int direction, bool wrap);
    void Back(u32 now);
    void Activate(u16 item, u32 now);
    void Adjust(u16 item, int direction);
    bool ReadGame(u16 item);
    void ReadGameValues();
    // engine_edit.cpp
    bool CommitBody(u16 item, u32 now);
    bool ApplyValue(u16 item);
    void DriveFixed();
    // engine_action.cpp
    Result SetEffect(u16 item, bool on);
    void Deactivate(u16 item, u32 now);
    void DeactivateGroup(u16 item, u32 now);
    void RunTrigger(u16 item, u32 input, u32 now);
    void ToggleMode(u16 item, u32 now);
    void Fire(u16 item, u32 now);
    void Tick(u32 now, u16 held);
    void Poll();
    void OpenDialog(DialogKind kind, u16 item);
    // engine_input.cpp
    void Repeat(u16 held, u16 pressed, u32 now);
    void PressMenu(u16 key, u32 now, bool repeated);
    void PressDialog(u16 key, u32 now);
    void FinishHold(u32 now, bool released);

    const ItemDef *defs_;
    std::size_t count_;
    ItemState state_[core::limits::MenuItems]{};
    Bound bound_[core::limits::MenuItems]{};
    bool is_bound_[core::limits::MenuItems]{};
    NoticeSink sink_{};
    bool dev_visible_ = false;
    u16 reserved_ = 0;

    bool visible_ = false;
    std::size_t depth_ = 0;
    u16 stack_[timing::kMaxDepth]{};
    u16 selection_[timing::kMaxDepth]{};
    DialogKind dialog_ = DialogKind::None;
    bool dialog_yes_ = true;
    u16 dialog_item_ = kNoItem;
    UiRequest ui_{};
    u16 prev_held_ = 0;
    u32 repeat_at_[4]{};

    bool hold_active_ = false;
    u16 hold_key_ = 0;
    u32 hold_start_ = 0;
};
} // namespace gohan::menu
